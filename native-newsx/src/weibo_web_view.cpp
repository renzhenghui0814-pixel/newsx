#include "weibo_web_view.h"
#include "weibo_session.h"
#include "settings.h"
#include <QQuickWindow>
#include <QPlatformSurfaceEvent>
#include <QFocusEvent>
#include <QTimer>
#include <QDir>
#include <QDateTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QWidget>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <vector>
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace newsx {
namespace {
// Check for readable content without holding up unfamiliar page layouts.
constexpr int readiness_retries = 24;
constexpr int readiness_interval_ms = 50;
constexpr auto document_ready_script = LR"JS((()=>{
    if(location.hostname!=='m.weibo.cn') return true;
    const content=/^\/(detail|status)\/[^/]+/.test(location.pathname)
        ? '.weibo-text,.lite-page-comment .lite-page-list'
        : '.weibo-text,.card-main :is(a[href],.m-text-box,.m-item-box)';
    return !!document.querySelector(content+',.empty-failed,.mv-toast .m-font-line-close')
        || [...document.querySelectorAll('.m-tips:not(:has(.m-loading))')].some(tip=>tip.textContent.trim());
})())JS";
QString take(LPWSTR value) {
    const auto result = QString::fromWCharArray(value ? value : L"");
    CoTaskMemFree(value);
    return result;
}
bool web_url(const QUrl& url) {
    return url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")
        || url == QUrl(QStringLiteral("about:blank"));
}
bool auth_page(const QUrl& url) {
    const auto host = url.host().toLower();
    return url == QUrl(QStringLiteral("about:blank")) || host.startsWith(QStringLiteral("passport."))
        || host == QStringLiteral("login.sina.com.cn") || url.path().startsWith(QStringLiteral("/login"))
        || url.path().startsWith(QStringLiteral("/signin")) || url.path().startsWith(QStringLiteral("/sso/"))
        || url.path().startsWith(QStringLiteral("/wbsso/"));
}
bool weibo_sign_in(const QUrl& url) {
    const auto host = url.host().toLower();
    const auto path = url.path().toLower();
    if (host == QStringLiteral("login.sina.com.cn")) return true;
    if (host != QStringLiteral("weibo.com") && !host.endsWith(QStringLiteral(".weibo.com"))
        && host != QStringLiteral("weibo.cn") && !host.endsWith(QStringLiteral(".weibo.cn"))) return false;
    return path == QStringLiteral("/login") || path == QStringLiteral("/login.php")
        || path.startsWith(QStringLiteral("/login/")) || path == QStringLiteral("/signin")
        || path.startsWith(QStringLiteral("/signin/")) || path == QStringLiteral("/sso/signin")
        || path.startsWith(QStringLiteral("/sso/signin/")) || path == QStringLiteral("/sso/login")
        || path.startsWith(QStringLiteral("/sso/login/")) || path.startsWith(QStringLiteral("/sso/v2/login"));
}
QList<QNetworkCookie> cookie_list(ICoreWebView2CookieList* list) {
    QList<QNetworkCookie> result;
    UINT count = 0;
    list->get_Count(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<ICoreWebView2Cookie> item;
        if (FAILED(list->GetValueAtIndex(i, &item)) || !item) continue;
        LPWSTR raw = nullptr;
        item->get_Domain(&raw);
        const auto domain = take(raw).toLower();
        const auto host = domain.startsWith('.') ? domain.mid(1) : domain;
        if (host != QStringLiteral("weibo.cn") && !host.endsWith(QStringLiteral(".weibo.cn"))
            && host != QStringLiteral("weibo.com") && !host.endsWith(QStringLiteral(".weibo.com"))) continue;
        raw = nullptr; item->get_Name(&raw); const auto name = take(raw).toUtf8();
        raw = nullptr; item->get_Value(&raw); QNetworkCookie cookie(name, take(raw).toUtf8());
        raw = nullptr; item->get_Path(&raw); cookie.setPath(take(raw));
        cookie.setDomain(domain);
        BOOL secure = FALSE, http_only = FALSE, session = TRUE;
        item->get_IsSecure(&secure); item->get_IsHttpOnly(&http_only); item->get_IsSession(&session);
        cookie.setSecure(secure); cookie.setHttpOnly(http_only);
        if (!session) {
            double expires = 0; item->get_Expires(&expires);
            cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(expires), Qt::UTC));
        }
        result.append(cookie);
    }
    return result;
}
}

struct WeiboWebView::State {
    struct Popup {
        QPointer<QDialog> dialog;
        QPointer<QWidget> host;
        ComPtr<ICoreWebView2Controller> controller;
        ComPtr<ICoreWebView2> web;
    };
    struct Request {
        ComPtr<ICoreWebView2NewWindowRequestedEventArgs> args;
        ComPtr<ICoreWebView2Deferral> deferral;
        bool completed = false;
        void complete() { if (!completed) { completed = true; args->put_Handled(TRUE); deferral->Complete(); } }
        ~Request() { complete(); }
    };
    QPointer<WeiboWebView> owner;
    HWND host = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    ComPtr<ICoreWebView2CookieManager> cookies;
    std::vector<std::shared_ptr<Popup>> popups;
    bool com = false, guest = true, has_content = false, collecting = false, suppress_content = false;
    bool checking_document = false;
    bool restoring_for_login = false;
    QUrl previous_readable_url;
    quint64 navigation = 0, load_generation = 0;
    QString agent;
};

WeiboWebView::WeiboWebView(QQuickItem* parent) : QQuickItem(parent) {
    setActiveFocusOnTab(true);
    connect(this, &QQuickItem::windowChanged, this, &WeiboWebView::attach_window);
    connect(this, &QQuickItem::visibleChanged, this, &WeiboWebView::update_geometry);
    auto* timer = new QTimer(this);
    timer->setInterval(3000);
    connect(timer, &QTimer::timeout, this, &WeiboWebView::collect_cookies);
    timer->start();
}
WeiboWebView::~WeiboWebView() {
    if (attached_window_) attached_window_->removeEventFilter(this);
    shutdown();
}
void WeiboWebView::set_site(const QString& site) {
    if ((site != QStringLiteral("weibo") && site != QStringLiteral("xiaohongshu")) || site_ == site) return;
    site_ = site;
    current_url_.clear();
    emit siteChanged();
    emit currentUrlChanged();
    schedule_open();
}
QObject* WeiboWebView::session() const { return session_; }
void WeiboWebView::set_session(QObject* object) {
    const auto session = qobject_cast<WeiboSession*>(object);
    if (session_ == session) return;
    if (session_) disconnect(session_, nullptr, this, nullptr);
    session_ = session;
    if (session_) connect(session_, &WeiboSession::changed, this, &WeiboWebView::schedule_open);
    emit sessionChanged();
    schedule_open();
}
void WeiboWebView::set_url(const QUrl& url) {
    if (url_ == url) return;
    url_ = url;
    current_url_ = QUrl{};
    emit urlChanged(); emit currentUrlChanged();
    schedule_open();
}
void WeiboWebView::set_zoom_factor(qreal factor) {
    factor = std::clamp(factor, 0.3, 2.0);
    if (qFuzzyCompare(zoom_, factor)) return;
    zoom_ = factor;
    if (state_ && state_->controller) state_->controller->put_ZoomFactor(zoom_);
    emit zoomFactorChanged();
}
void WeiboWebView::set_dark_theme(bool value) {
    if (dark_ == value) return;
    dark_ = value; apply_appearance(); emit appearanceChanged();
}
void WeiboWebView::set_hide_site_back(bool value) {
    if (hide_back_ == value) return;
    hide_back_ = value; apply_appearance(); emit appearanceChanged();
}
void WeiboWebView::set_background_color(const QColor& color) {
    if (!color.isValid() || background_ == color) return;
    background_ = color; apply_appearance(); emit appearanceChanged();
}
void WeiboWebView::set_login_only(bool value) {
    if (login_only_ == value) return;
    login_only_ = value; schedule_open(); emit loginOnlyChanged();
}

QString WeiboWebView::appearance_script() const {
    if (site_ == QStringLiteral("xiaohongshu")) return QStringLiteral("void 0;");
    const auto config = QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("dark"), dark_}, {QStringLiteral("hideBack"), hide_back_},
        {QStringLiteral("loginOnly"), login_only_},
        {QStringLiteral("bg"), background_.name()},
        {QStringLiteral("surface"), dark_ ? QStringLiteral("#141414") : QStringLiteral("#f9fbfc")},
        {QStringLiteral("raised"), dark_ ? QStringLiteral("#222222") : QStringLiteral("#eff3f6")},
        {QStringLiteral("fg"), dark_ ? QStringLiteral("#ededed") : QStringLiteral("#213340")},
        {QStringLiteral("secondary"), dark_ ? QStringLiteral("#bdbdbd") : QStringLiteral("#617480")},
        {QStringLiteral("border"), dark_ ? QStringLiteral("#373737") : QStringLiteral("#d8e2e8")},
        {QStringLiteral("link"), dark_ ? QStringLiteral("#91bddd") : QStringLiteral("#397fbd")}
    }).toJson(QJsonDocument::Compact));
    return QStringLiteral(R"JS((()=>{
      if(!/(^|\.)weibo\.(cn|com)$/.test(location.hostname) && !/(^|\.)sina\.com\.cn$/.test(location.hostname)) return true;
      window.__newsxAppearance=%1;
      if(!window.__newsxApplyAppearance){
        const normal=s=>(s||'').replace(/[\s‹〈←<]/g,'').trim();
        const isStatus=()=>location.hostname==='m.weibo.cn' && /^\/(detail|status)\/[^/]+/.test(location.pathname);
        window.__newsxApplyAppearance=()=>{
          if(!document.documentElement) return;
          const c=window.__newsxAppearance;
          const loginPage=c.loginOnly && ['passport.weibo.com','passport.weibo.cn','login.sina.com.cn'].includes(location.hostname);
          // The sign-in site's dark variants depend on the .dark class.
          if(loginPage) document.documentElement.classList.toggle('dark',c.dark);
          let css=loginPage ? `
            :root{color-scheme:${c.dark?'dark':'light'};}
            html.dark,html.dark :is(body,#app,.bg-card,.bg-white){
              background-color:${c.bg}!important;color:${c.fg};}
            html.dark :is(.bg-cardin,.bg-white95){background-color:${c.raised}!important;}
            html.dark :is(.text-main,.text-darkGray,ul a.border-brand,#dropdownDefaultButton){
              color:${c.fg}!important;}
            html.dark :is(.text-mainb,.text-sub,ul a.inline-block:not(.border-brand)){
              color:${c.secondary}!important;}
            html.dark .text-disabled{color:#929292!important;}
            html.dark .text-alink{color:${c.link}!important;}
            html.dark .text-red{color:#ffadac!important;}
            html.dark :is(.border-line,.border-lineb,.border-input){border-color:${c.border}!important;}
            html.dark form input.border-input{
              background-color:${c.raised}!important;color:${c.fg}!important;
              border:1px solid ${c.border};border-radius:8px;}
            html.dark form input.border-input::placeholder{color:${c.secondary}!important;}
            html.dark form input.border-input:not([class~="pl-20"]){padding-inline-start:12px;}
            html.dark #dropdownDefaultButton{margin-inline-start:12px;}
            html.dark form .w-25.justify-end{padding-inline-end:12px;}
            html.dark :is(a,button,input):focus-visible{outline:2px solid ${c.link}!important;outline-offset:2px;}
            html.dark form input.border-input:focus-visible{outline-offset:-2px;}
            html.dark button.bg-brand{
              background-color:#ffb46a!important;color:#2b1a0c!important;font-weight:600;}
            html.dark button.bg-brand:is(:hover,:active){background-color:#ffc591!important;}
            html.dark .border-brand{border-color:#ffb46a!important;}
            html.dark .text-brand{color:#ffb46a!important;}
            html.dark input[type=checkbox]{accent-color:#ffb46a;}
            html.dark img[class~="h-4.5"]{
              background-color:#eeeeee;box-sizing:content-box;border-radius:6px;padding:4px 7px;}
            html.dark .w-45.h-45.p-5:has(>img){background-color:white;border-radius:8px;}
          ` : `:root{color-scheme:${c.dark?'dark':'light'}!important;
              --m-card-background:${c.surface};--m-card-background-alpha:transparent;
              --m-content-text:${c.fg};--m-subtitle-text:${c.secondary};
              --m-attachment-text:${c.secondary};--m-link-text:${c.link};--m-border-color:${c.border};}
            html,body,#app,.m-container,.m-page-group{background-color:${c.bg}!important;color:${c.fg}!important;}
            .m-panel,.card,.card-wrap,.lite-page-wrap,.lite-page-editor{
              background-color:${c.surface}!important;color:${c.fg}!important;border-color:${c.border}!important;}
            .card11,.m-top-nav,.m-nav-main,.m-nav-sub,.m-tab-bar,.m-bottom-bar{
              background-color:${c.raised}!important;background-image:none!important;
              color:${c.fg}!important;border-color:${c.border}!important;}
            .card-main,.card-list,.m-card-list,.m-item-box,.m-text-box,.m-box,.card-title,.m-toolbar,.m-tab{
              background-color:transparent!important;border-color:${c.border}!important;}
            .weibo-text,.weibo-text p,.m-text-box,.card-title,.card h1,.card h2,.card h3,.card h4,
            .card h5,.card .m-text-cut,.card-title a,.card-title span,.m-text-box>a,.m-text-box>span{
              color:${c.fg}!important;}
            .weibo-text a,.weibo-text a span,.card h1 a,.card h2 a,.card h3 a{color:${c.link}!important;}
            .time,.m-text-box h4,.m-text-box .time,.m-text-box h4 a,.m-text-box h4 span,
            .m-toolbar,.m-toolbar a,.m-toolbar span,.m-toolbar .m-font,.m-tab a,.m-nav-sub a,
            .m-tab .m-text-cut,.m-bottom-bar a,.m-bottom-bar span{color:${c.secondary}!important;}
            .m-tab .m-cur,.m-tab .m-cur a,.m-nav-sub .m-cur{color:${c.link}!important;}
            .m-nav-sub:before,.m-nav-sub:after,.m-nav-sub .m-nav-left,.m-nav-sub .m-nav-right{
              background-image:none!important;background-color:${c.raised}!important;}
            .card:before,.card:after,.m-panel:before,.m-panel:after,.m-toolbar:before,.m-toolbar:after,
            .m-toolbar li,.m-toolbar li:before,.m-toolbar li:after{
              border-color:${c.border}!important;}
            html,body,#app,.m-container,.m-page-group,[data-newsx-page-scroll]{
              background-image:none!important;box-shadow:none!important;}
            *{scrollbar-width:auto!important;scrollbar-color:auto!important;}
            ::-webkit-scrollbar{width:5px;height:5px;}
            ::-webkit-scrollbar-track,::-webkit-scrollbar-corner{background:transparent;}
            ::-webkit-scrollbar-thumb{background:${c.border};border-radius:5px;}
            ::-webkit-scrollbar-thumb:hover{background:${c.secondary};}
            ::-webkit-scrollbar-button{display:none;width:0;height:0;}
            input,textarea,select{color:${c.fg}!important;background-color:${c.surface}!important;}
            [data-newsx-topic-summary],[data-newsx-topic-summary] :is(h1,h2,h3,h4,h5,p,span,a,div,strong){
              color:${c.fg}!important;}
            [data-newsx-topic-summary] [data-newsx-topic-meta],
            [data-newsx-topic-summary] [data-newsx-topic-meta] *{color:${c.secondary}!important;}
            [data-newsx-topic-tabs],[data-newsx-topic-tabs] :is(a,span,div,li,button){
              color:${c.secondary}!important;background-color:transparent!important;background-image:none!important;
              border-color:${c.border}!important;box-shadow:none!important;}
            [data-newsx-topic-tabs]{background-color:${c.raised}!important;}
            [data-newsx-topic-tabs] :is(.m-cur,.cur,.active,.current,.selected,[aria-selected="true"]),
            [data-newsx-topic-tabs] :is(.m-cur,.cur,.active,.current,.selected,[aria-selected="true"]) *,
            [data-newsx-topic-tabs] [data-newsx-tab-label="综合"]{color:${c.fg}!important;}
            [data-newsx-topic-tabs] *:before,[data-newsx-topic-tabs] *:after{background-image:none!important;}
            [data-newsx-tabs-fade]:before,[data-newsx-tabs-fade]:after{
              background:transparent!important;box-shadow:none!important;}
            .m-ctrl-box,.m-ctrl-box *{color:${c.secondary}!important;border-color:${c.border}!important;}
            .m-ctrl-box,.m-ctrl-box:before,.m-ctrl-box:after,.m-ctrl-box *:before,.m-ctrl-box *:after,
            .m-toolbar *,.m-toolbar *:before,.m-toolbar *:after{
              border-color:${c.border}!important;box-shadow:none!important;}
            .line-top:before,.line-bottom:after,.line-left:before,.line-right:after{
              border-color:${c.border}!important;background-color:${c.border}!important;}
            hr{border-color:${c.border}!important;background-color:${c.border}!important;}
          `;
          if(c.hideBack) css+=`
            html[data-newsx-status] :is(.lite-topbar,.lite-page-tab,.lite-botbar,.lite-page-editor){
              background-color:${c.raised}!important;color:${c.fg}!important;
              border-color:${c.border}!important;border-image:none!important;box-shadow:none!important;}
            html[data-newsx-status] .lite-page-top :is(.nav-main,.nav-main h3,.m-font){
              color:${c.fg}!important;}
            html[data-newsx-status] .lite-page-top .nav-main{background:transparent!important;}
            html[data-newsx-status] .lite-page-tab{color:${c.secondary}!important;box-shadow:0 1px 0 ${c.border}!important;}
            html[data-newsx-status] .lite-page-tab :is(.tab-item,a){color:inherit!important;}
            html[data-newsx-status] .lite-page-tab :is(.cur,.active,[aria-selected="true"]){color:${c.fg}!important;}
            html[data-newsx-status] .lite-page-tab .cur em{border-color:${c.link}!important;}
            html[data-newsx-status] :is(.lite-page-list,.lite-msg,.lite-page-comment){
              background-color:${c.surface}!important;color:${c.fg}!important;
              border-color:${c.border}!important;border-image:none!important;}
            html[data-newsx-status] .lite-page-list .cmt-sub-txt{
              background-color:${c.raised}!important;color:${c.fg}!important;}
            html[data-newsx-status] .lite-page-list .cmt-sub-txt *{color:${c.fg}!important;}
            html[data-newsx-status] :is(.lite-bot,.lite-bot .lite-iconf,.lite-time,.lite-page-editor .lite-iconf,
                .lite-page-editor .focus,.lite-botbar .m-diy-btn,.lite-botbar .lite-iconf){color:${c.secondary}!important;}
            html[data-newsx-status] :is(.lite-bot-link,.lite-page-list .m-text-box a,
                .lite-page-list .m-text-box a span,.lite-page-list .cmt-sub-txt .total,.lite-page-list .cmt-sub-txt a,
                .lite-page-list .cmt-sub-txt a *,.lite-page-list .cmt-sub-txt .total *){color:${c.link}!important;}
            html[data-newsx-status] .lite-page-wrap .ad-wrap,
            .mv-toast:has(.m-loading){display:none!important;}
            [data-newsx-back-hidden],.m-top-bar,div:empty:has(+ .m-top-bar){display:none!important;}
            html,body{overflow-x:clip!important;}
            body{padding-top:0!important;}
            /* PageTabs and module-page-fragment toggle .fixed and a sibling
               placeholder on scroll. Keep only their outer nav in normal flow,
               hide that placeholder in both states, and retain the inner flex
               positioning used by the official horizontal tab strip. */
            .m-top-nav,.m-top-nav.fixed,.module-page-fragment,.module-page-fragment.fixed{
              position:relative!important;top:auto!important;left:auto!important;right:auto!important;
              bottom:auto!important;transform:none!important;}
            .m-top-nav{margin-bottom:0!important;}
            .m-top-nav-wrapper,.m-top-nav-wrapper.hide{display:none!important;height:0!important;}
            .m-top-nav .nav-main{position:relative!important;}
            .m-top-nav .nav-main:before,.m-top-nav .nav-plus:before,
            .lite-topbar.main-top .nav-main .inner-box:before,
            .lite-topbar.main-top .nav-main .inner-box:after{display:none!important;content:none!important;}
            .m-top-nav .scroll-box{scrollbar-width:none!important;}
            .m-top-nav .scroll-box::-webkit-scrollbar{display:none!important;width:0!important;height:0!important;}
            .m-top-nav,.m-ctrl-box{border-image:none!important;}
          `;
          let style=document.getElementById('newsx-appearance');
          if(!style){style=document.createElement('style');style.id='newsx-appearance';(document.head||document.documentElement).appendChild(style);}
          if(style.textContent!==css) style.textContent=css;
          const status=c.hideBack && isStatus();
          document.documentElement.toggleAttribute('data-newsx-status',status);
          if(c.hideBack && !status && document.body){
            // Identify the official summary and tabs by their own text rather
            // than relying on version-specific card classes. Only add styling
            // hooks; all content and interactions stay owned by the website.
            const labels=['综合','实时','图片','视频','关注','热门','话题','超话'];
            const texts=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
            let leaf, count=0;
            while((leaf=texts.nextNode()) && ++count<3000){
              if(!leaf.parentElement || /^(SCRIPT|STYLE)$/.test(leaf.parentElement.tagName)) continue;
              const value=normal(leaf.nodeValue);
              if(value.includes('阅读')){
                let summary=leaf.parentElement;
                for(let depth=0;depth<6 && summary && summary!==document.body;depth++,summary=summary.parentElement){
                  const text=normal(summary.textContent);
                  if(text.length>500) break;
                  if(text.includes('阅读') && text.includes('讨论') && text.includes('#')){
                    summary.setAttribute('data-newsx-topic-summary','');
                    summary.querySelectorAll('span,p,div,a').forEach(meta=>{
                      const text=normal(meta.textContent);
                      if(text.length<160 && !text.includes('#') && /阅读|讨论|主持人|发布/.test(text))
                        meta.setAttribute('data-newsx-topic-meta','');
                    });
                    break;
                  }
                }
              }
              if(value==='综合'){
                let tabs=leaf.parentElement;
                leaf.parentElement.setAttribute('data-newsx-tab-label','综合');
                for(let depth=0;depth<6 && tabs && tabs!==document.body;depth++,tabs=tabs.parentElement){
                  const text=normal(tabs.textContent);
                  if(text.length>100) break;
                  if(labels.filter(label=>text.includes(label)).length>=4){
                    tabs.setAttribute('data-newsx-topic-tabs','');
                    break;
                  }
                }
              }
            }
          }
          if(c.hideBack){
            // The official TopBar component renders a 2.75rem spacer directly
            // before .m-top-bar. Hide that sibling, not arbitrary page padding.
            document.querySelectorAll('.m-top-bar').forEach(bar=>{
              const spacer=bar.previousElementSibling;
              if(spacer && spacer.tagName==='DIV' && spacer.children.length===0
                  && !normal(spacer.textContent) && /^([\d.]+)(rem|px)$/.test(spacer.style.height))
                spacer.setAttribute('data-newsx-back-hidden','');
            });
          }
        };
        let queued=false;
        const schedule=()=>{
          if(queued)return;
          queued=true;
          queueMicrotask(()=>{queued=false;window.__newsxApplyAppearance();});
        };
        new MutationObserver(schedule).observe(document,{childList:true,characterData:true,subtree:true});
        document.addEventListener('DOMContentLoaded',()=>window.__newsxApplyAppearance(),{once:true});
      }
      window.__newsxApplyAppearance();
    })())JS").arg(config);
}

void WeiboWebView::apply_appearance() {
    if (!state_ || !state_->web) return;
    const auto script = appearance_script();
    ComPtr<ICoreWebView2_13> web13;
    if (SUCCEEDED(state_->web.As(&web13))) {
        ComPtr<ICoreWebView2Profile> profile;
        if (SUCCEEDED(web13->get_Profile(&profile)) && profile)
            profile->put_PreferredColorScheme(dark_ ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
    }
    const auto decorate = [&](ICoreWebView2Controller* controller, ICoreWebView2* web) {
        ComPtr<ICoreWebView2Controller2> controller2;
        if (controller && SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
            COREWEBVIEW2_COLOR color{255, static_cast<BYTE>(background_.red()), static_cast<BYTE>(background_.green()), static_cast<BYTE>(background_.blue())};
            controller2->put_DefaultBackgroundColor(color);
        }
        if (web) web->ExecuteScript(reinterpret_cast<LPCWSTR>(script.utf16()), nullptr);
    };
    decorate(state_->controller.Get(), state_->web.Get());
    for (const auto& popup : state_->popups) decorate(popup->controller.Get(), popup->web.Get());
}
void WeiboWebView::componentComplete() {
    QQuickItem::componentComplete();
    attach_window();
    schedule_open();
}
void WeiboWebView::attach_window() {
    if (attached_window_ == window()) { update_geometry(); return; }
    if (attached_window_) { attached_window_->removeEventFilter(this); disconnect(attached_window_, nullptr, this, nullptr); }
    shutdown();
    attached_window_ = window();
    if (!attached_window_) return;
    attached_window_->installEventFilter(this);
    connect(attached_window_, &QWindow::visibleChanged, this, &WeiboWebView::update_geometry);
    connect(attached_window_, &QWindow::widthChanged, this, &WeiboWebView::update_geometry);
    connect(attached_window_, &QWindow::heightChanged, this, &WeiboWebView::update_geometry);
    connect(attached_window_, &QWindow::xChanged, this, &WeiboWebView::update_geometry);
    connect(attached_window_, &QWindow::yChanged, this, &WeiboWebView::update_geometry);
    connect(attached_window_, &QWindow::screenChanged, this, &WeiboWebView::update_geometry);
    schedule_open();
}
void WeiboWebView::schedule_open() {
    if (rebuild_pending_) return;
    if (state_) state_->owner.clear(); // Ignore callbacks from the previous URL/profile.
    rebuild_pending_ = true;
    QTimer::singleShot(0, this, [this] { rebuild_pending_ = false; open(); });
}
void WeiboWebView::shutdown() {
    if (!state_) return;
    auto old = std::move(state_);
    old->owner.clear();
    if (old->guest && old->cookies) old->cookies->DeleteAllCookies();
    for (const auto& popup : old->popups) {
        if (popup->controller) popup->controller->Close();
        popup->web.Reset(); popup->controller.Reset();
        if (popup->dialog) { popup->dialog->close(); popup->dialog->deleteLater(); }
    }
    if (old->controller) old->controller->Close();
    old->cookies.Reset(); old->web.Reset(); old->controller.Reset(); old->environment.Reset();
    if (old->host && IsWindow(old->host)) DestroyWindow(old->host);
    if (old->com) CoUninitialize();
}
void WeiboWebView::set_loading(bool value) {
    if (loading_ != value) { loading_ = value; emit loadingChanged(); }
}
void WeiboWebView::set_content_ready(bool value) {
    if (content_ready_ != value) { content_ready_ = value; emit contentReadyChanged(); }
}
void WeiboWebView::set_login_required(bool value) {
    if (login_required_ != value) { login_required_ = value; emit loginRequiredChanged(); }
}
void WeiboWebView::show_login_required() {
    if (!state_->has_content && !state_->previous_readable_url.isEmpty() && !state_->restoring_for_login) {
        LPWSTR raw = nullptr;
        state_->web->get_Source(&raw);
        const QUrl source(take(raw));
        if (source == state_->previous_readable_url) {
            state_->restoring_for_login = true;
            finish_navigation();
            return;
        }
        const auto previous = state_->previous_readable_url.toString();
        state_->restoring_for_login = true;
        if (SUCCEEDED(state_->web->Navigate(reinterpret_cast<LPCWSTR>(previous.utf16())))) return;
    }
    state_->restoring_for_login = false;
    set_loading(false);
    set_login_required(true);
}
void WeiboWebView::finish_navigation() {
    const bool restored = state_->restoring_for_login;
    state_->restoring_for_login = false;
    state_->has_content = !state_->suppress_content;
    set_content_ready(state_->has_content);
    set_loading(false);
    update_geometry();
    collect_cookies();
    if (restored) set_login_required(true);
}
void WeiboWebView::reveal_document(quint64 generation, int attempts) {
    if (!state_ || !state_->web || state_->load_generation != generation || !error_.isEmpty()
        || login_required_ || state_->suppress_content || state_->has_content) return;
    if (attempts == readiness_retries) apply_appearance();
    const bool opening_as_guest = state_->guest && state_->previous_readable_url.isEmpty() && !state_->restoring_for_login;
    // A visitor handshake can have its own DOM before the requested page arrives.
    if (opening_as_guest && current_url_.host() != url_.host()) return;
    const std::weak_ptr<State> weak = state_;
    const auto hr = state_->web->ExecuteScript(document_ready_script,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [weak, generation, attempts, opening_as_guest](HRESULT result, LPCWSTR value) -> HRESULT {
                const auto s = weak.lock();
                if (!s || !s->owner) return S_OK;
                const bool waiting = SUCCEEDED(result) && value && QString::fromWCharArray(value) == QStringLiteral("false");
                const bool retry = waiting && (attempts > 0 || opening_as_guest);
                const int delay = !retry ? 0 : attempts > 0 ? readiness_interval_ms : 200;
                QTimer::singleShot(delay, s->owner, [weak, generation, attempts, retry] {
                    const auto s = weak.lock();
                    if (!s || !s->owner || s->load_generation != generation || s->suppress_content
                        || s->owner->login_required_ || !s->owner->error_.isEmpty()) return;
                    if (retry) s->owner->reveal_document(generation, std::max(0, attempts - 1));
                    else s->owner->finish_navigation();
                });
                return S_OK;
            }).Get());
    if (FAILED(hr) && !login_required_) finish_navigation();
}
void WeiboWebView::fail(const QString& message) {
    error_ = message;
    if (site_ == QStringLiteral("xiaohongshu")) {
        error_.replace(QStringLiteral("或使用备用阅读"), QStringLiteral("或在浏览器打开"));
        error_.replace(QStringLiteral("可使用备用阅读"), QStringLiteral("请重试或在浏览器打开"));
        error_.replace(QStringLiteral("、切换入口或使用备用阅读"), QStringLiteral("或在浏览器打开"));
    }
    set_loading(false);
    update_geometry();
    emit errorStringChanged();
}
void WeiboWebView::open() {
    if (!isComponentComplete() || !window() || !window()->isVisible() || width() <= 0 || height() <= 0 || url_.isEmpty()) return;
    shutdown();
    set_login_required(false);
    error_.clear(); emit errorStringChanged();
    can_go_back_ = false; emit canGoBackChanged();
    set_content_ready(false);
    set_loading(true);
    if (!web_url(url_)) { fail(QStringLiteral("此链接无法在内嵌网页中打开。")); return; }
    state_ = std::make_shared<State>();
    auto s = state_;
    s->owner = this;
    s->guest = site_ == QStringLiteral("weibo") && !login_only_ && (!session_ || !session_->logged_in());
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    s->com = SUCCEEDED(com);
    if (!s->com) { fail(QStringLiteral("网页浏览器初始化失败，可重试或使用备用阅读。")); return; }
    s->host = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 1, 1, reinterpret_cast<HWND>(window()->winId()), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!s->host) { fail(QStringLiteral("无法创建网页显示区域，可使用备用阅读。")); return; }
    const auto folder = QDir(app_data_dir()).filePath(site_ == QStringLiteral("xiaohongshu")
        ? QStringLiteral("xiaohongshu-webview")
        : s->guest ? QStringLiteral("weibo-guest-webview") : QStringLiteral("weibo-webview"));
    QDir().mkpath(folder);
    const std::weak_ptr<State> weak = s;
    const auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, reinterpret_cast<LPCWSTR>(folder.utf16()), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
                const auto s = weak.lock();
                if (!s || !s->owner) return S_OK;
                if (FAILED(error) || !environment) {
                    QTimer::singleShot(0, s->owner, [weak] { const auto s = weak.lock(); if (s && s->owner) s->owner->fail(QStringLiteral("无法启动 WebView2，请重试或使用备用阅读。")); });
                    return S_OK;
                }
                s->environment = environment;
                const auto hr = environment->CreateCoreWebView2Controller(s->host,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [weak](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                            const auto s = weak.lock();
                            if (!s || !s->owner) return S_OK;
                            if (FAILED(error) || !controller) {
                                QTimer::singleShot(0, s->owner, [weak] { const auto s = weak.lock(); if (s && s->owner) s->owner->fail(QStringLiteral("网页显示区域初始化失败，可重试或使用备用阅读。")); });
                                return S_OK;
                            }
                            s->controller = controller;
                            controller->get_CoreWebView2(&s->web);
                            ComPtr<ICoreWebView2_2> web2;
                            if (s->web && SUCCEEDED(s->web.As(&web2))) web2->get_CookieManager(&s->cookies);
                            if (!s->web || !s->cookies) {
                                QTimer::singleShot(0, s->owner, [weak] { const auto s = weak.lock(); if (s && s->owner) s->owner->fail(QStringLiteral("官网浏览器会话初始化失败，可重试或使用备用阅读。")); });
                                return S_OK;
                            }
                            if (s->guest || (s->owner->login_only_ && s->owner->session_ && !s->owner->session_->logged_in())) s->cookies->DeleteAllCookies();
                            else if (s->owner->site_ == QStringLiteral("weibo") && s->owner->session_) {
                                for (const auto& cookie : s->owner->session_->browser_cookies()) {
                                    if (!cookie.isSessionCookie() && cookie.expirationDate() <= QDateTime::currentDateTimeUtc()) continue;
                                    const auto name = QString::fromUtf8(cookie.name());
                                    const auto value = QString::fromUtf8(cookie.value());
                                    const auto domain = cookie.domain();
                                    const auto path = cookie.path().isEmpty() ? QStringLiteral("/") : cookie.path();
                                    ComPtr<ICoreWebView2Cookie> item;
                                    if (SUCCEEDED(s->cookies->CreateCookie(reinterpret_cast<LPCWSTR>(name.utf16()),
                                        reinterpret_cast<LPCWSTR>(value.utf16()), reinterpret_cast<LPCWSTR>(domain.utf16()),
                                        reinterpret_cast<LPCWSTR>(path.utf16()), &item))) {
                                        item->put_IsHttpOnly(cookie.isHttpOnly()); item->put_IsSecure(cookie.isSecure());
                                        if (!cookie.isSessionCookie()) item->put_Expires(cookie.expirationDate().toSecsSinceEpoch());
                                        s->cookies->AddOrUpdateCookie(item.Get());
                                    }
                                }
                            }
                            ComPtr<ICoreWebView2Settings> settings;
                            s->web->get_Settings(&settings);
                            ComPtr<ICoreWebView2Settings2> settings2;
                            if (settings && SUCCEEDED(settings.As(&settings2))) {
                                LPWSTR agent = nullptr; settings2->get_UserAgent(&agent); s->agent = take(agent);
                            }
                            controller->put_ZoomFactor(s->owner->zoom_);
                            EventRegistrationToken token;
                            if (web2) web2->add_DOMContentLoaded(Callback<ICoreWebView2DOMContentLoadedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2DOMContentLoadedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock(); if (!s || !s->owner) return S_OK;
                                    UINT64 id = 0; args->get_NavigationId(&id);
                                    if (id != s->navigation || s->owner->login_only_) return S_OK;
                                    const auto generation = s->load_generation;
                                    QTimer::singleShot(0, s->owner, [weak, generation] {
                                        const auto s = weak.lock();
                                        if (!s || !s->owner || s->load_generation != generation || s->checking_document) return;
                                        s->checking_document = true;
                                        s->owner->reveal_document(generation, readiness_retries);
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            controller->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                [weak](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock();
                                    UINT key = 0; args->get_VirtualKey(&key);
                                    COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
                                    if (s && s->owner && (s->owner->login_only_ || s->owner->site_ == QStringLiteral("xiaohongshu")) && key == VK_ESCAPE
                                        && kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) {
                                        args->put_Handled(TRUE);
                                        QTimer::singleShot(0, s->owner, [weak] {
                                            const auto s = weak.lock(); if (s && s->owner) emit s->owner->closeRequested();
                                        });
                                    }
                                    return S_OK;
                                }).Get(), &token);
                            controller->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
                                [weak](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
                                    COREWEBVIEW2_MOVE_FOCUS_REASON reason; args->get_Reason(&reason);
                                    if (reason == COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC) return S_OK;
                                    args->put_Handled(TRUE);
                                    const auto s = weak.lock();
                                    if (s && s->owner) QTimer::singleShot(0, s->owner, [weak, reason] {
                                        const auto s = weak.lock(); if (!s || !s->owner || !s->owner->window()) return;
                                        SetFocus(reinterpret_cast<HWND>(s->owner->window()->winId()));
                                        const bool forward = reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT;
                                        auto* next = s->owner->nextItemInFocusChain(forward);
                                        if (next && next != s->owner) next->forceActiveFocus(forward ? Qt::TabFocusReason : Qt::BacktabFocusReason);
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr; args->get_Uri(&uri); const QUrl target(take(uri));
                                    if (!web_url(target)) { args->put_Cancel(TRUE); return S_OK; }
                                    const auto s = weak.lock();
                                    if (!s || !s->owner) return S_OK;
                                    if (s->guest && weibo_sign_in(target)) {
                                        args->put_Cancel(TRUE);
                                        const auto generation = s->load_generation;
                                        QTimer::singleShot(0, s->owner, [weak, generation] {
                                            const auto s = weak.lock();
                                            if (s && s->owner && s->load_generation == generation) s->owner->show_login_required();
                                        });
                                        return S_OK;
                                    }
                                    if (s->has_content && !s->owner->login_only_) {
                                        LPWSTR source = nullptr;
                                        s->web->get_Source(&source);
                                        s->previous_readable_url = QUrl(take(source));
                                    }
                                    s->suppress_content = s->owner->login_only_ && !auth_page(target);
                                    args->get_NavigationId(&s->navigation);
                                    const auto generation = ++s->load_generation;
                                    s->has_content = false;
                                    s->checking_document = false;
                                    QTimer::singleShot(0, s->owner, [weak, target, generation] {
                                        const auto s = weak.lock();
                                        if (!s || !s->owner || s->load_generation != generation) return;
                                        s->owner->current_url_ = target; emit s->owner->currentUrlChanged();
                                        s->owner->set_login_required(false);
                                        s->owner->error_.clear(); emit s->owner->errorStringChanged();
                                        // Keep the current reader visible during redirects and
                                        // while WebView2 is preparing the next document.
                                        if (s->owner->login_only_) s->owner->set_content_ready(false);
                                        s->owner->set_loading(true); s->owner->update_geometry();
                                    });
                                    QTimer::singleShot(30000, s->owner, [weak, generation] {
                                        const auto s = weak.lock();
                                        if (s && s->owner && s->load_generation == generation && s->owner->loading_)
                                            s->owner->fail(QStringLiteral("官网页面加载超时，可重试、切换入口或使用备用阅读。"));
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock(); if (!s || !s->owner) return S_OK;
                                    UINT64 id = 0; args->get_NavigationId(&id); if (id != s->navigation) return S_OK;
                                    BOOL success = FALSE; args->get_IsSuccess(&success);
                                    COREWEBVIEW2_WEB_ERROR_STATUS status; args->get_WebErrorStatus(&status);
                                    if (!success && status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) return S_OK;
                                    const auto generation = s->load_generation;
                                    QTimer::singleShot(0, s->owner, [weak, success, generation] {
                                        const auto s = weak.lock(); if (!s || !s->owner || s->load_generation != generation) return;
                                        if (s->owner->login_required_) return;
                                        if (!success) {
                                            s->owner->fail(QStringLiteral("官网页面暂时无法打开，可重试、切换入口或使用备用阅读。"));
                                        } else if (s->owner->login_only_) {
                                            s->owner->apply_appearance();
                                            s->owner->finish_navigation();
                                        } else if (!s->checking_document) {
                                            s->checking_document = true;
                                            s->owner->reveal_document(generation, readiness_retries);
                                        }
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            // SPA navigation updates the URL without loading another document.
                            if (s->owner->site_ == QStringLiteral("xiaohongshu"))
                                s->web->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
                                    [weak](ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                                        const auto s = weak.lock(); if (!s || !s->owner) return S_OK;
                                        QTimer::singleShot(0, s->owner, [weak] {
                                            const auto s = weak.lock(); if (!s || !s->owner || !s->web) return;
                                            LPWSTR raw = nullptr; s->web->get_Source(&raw);
                                            s->owner->current_url_ = QUrl(take(raw)); emit s->owner->currentUrlChanged();
                                        });
                                        return S_OK;
                                    }).Get(), &token);
                            s->web->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
                                [weak](ICoreWebView2*, IUnknown*) -> HRESULT {
                                    const auto s = weak.lock(); if (!s || !s->owner) return S_OK;
                                    QTimer::singleShot(0, s->owner, [weak] {
                                        const auto s = weak.lock(); if (!s || !s->owner || !s->web) return;
                                        BOOL back = FALSE; s->web->get_CanGoBack(&back);
                                        s->owner->can_go_back_ = back; emit s->owner->canGoBackChanged();
                                    }); return S_OK;
                                }).Get(), &token);
                            s->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock(); if (s && s->owner) s->owner->open_popup(args); else args->put_Handled(TRUE);
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                                [weak](ICoreWebView2*, IUnknown*) -> HRESULT {
                                    const auto s = weak.lock(); if (s && s->owner) QTimer::singleShot(0, s->owner, [weak] {
                                        const auto s = weak.lock(); if (s && s->owner) emit s->owner->closeRequested();
                                    }); return S_OK;
                                }).Get(), &token);
                            s->web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                                    const auto s = weak.lock(); if (s && s->owner) QTimer::singleShot(0, s->owner, [weak] {
                                        const auto s = weak.lock(); if (s && s->owner) s->owner->fail(QStringLiteral("官网浏览器进程已退出，请重试或使用备用阅读。"));
                                    }); return S_OK;
                                }).Get(), &token);
                            QTimer::singleShot(0, s->owner, [weak] {
                                const auto s = weak.lock(); if (!s || !s->owner) return;
                                s->owner->apply_appearance();
                                const auto script = s->owner->appearance_script();
                                const auto hr = s->web->AddScriptToExecuteOnDocumentCreated(reinterpret_cast<LPCWSTR>(script.utf16()),
                                    Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                                        [weak](HRESULT, LPCWSTR) -> HRESULT {
                                            const auto s = weak.lock(); if (s && s->owner) QTimer::singleShot(0, s->owner, [weak] {
                                                const auto s = weak.lock(); if (s && s->owner) { s->owner->update_geometry(); s->owner->navigate(); }
                                            }); return S_OK;
                                        }).Get());
                                if (FAILED(hr)) s->owner->navigate();
                            });
                            return S_OK;
                        }).Get());
                if (FAILED(hr)) QTimer::singleShot(0, s->owner, [weak] { const auto s = weak.lock(); if (s && s->owner) s->owner->fail(QStringLiteral("无法创建官网浏览器，可重试或使用备用阅读。")); });
                return S_OK;
            }).Get());
    if (FAILED(hr)) fail(QStringLiteral("无法启动官网浏览器，可重试或使用备用阅读。"));
    QTimer::singleShot(35000, this, [weak] {
        const auto s = weak.lock(); if (s && s->owner && !s->has_content && s->owner->loading_)
            s->owner->fail(QStringLiteral("官网页面加载超时，可重试或使用备用阅读。"));
    });
}

void WeiboWebView::navigate() {
    if (!state_ || !state_->web) return;
    const auto target = current_url_.isEmpty() ? url_ : current_url_;
    const auto text = target.toString();
    if (FAILED(state_->web->Navigate(reinterpret_cast<LPCWSTR>(text.utf16()))))
        fail(QStringLiteral("官网浏览器不可用，请重试或使用备用阅读。"));
}
void WeiboWebView::navigateTo(const QUrl& url) {
    if (!web_url(url)) return;
    current_url_ = url;
    emit currentUrlChanged();
    error_.clear(); emit errorStringChanged();
    if (!state_ || !state_->web) { schedule_open(); return; }
    set_loading(true);
    navigate();
}
void WeiboWebView::reload() {
    error_.clear(); emit errorStringChanged();
    if (!state_ || !state_->web || !state_->has_content) { schedule_open(); return; }
    set_loading(true);
    if (FAILED(state_->web->Reload())) schedule_open();
}
void WeiboWebView::goBack() {
    if (state_ && state_->web && can_go_back_) state_->web->GoBack();
}
bool WeiboWebView::isPointerOverWindow() const {
    auto* host_window = window();
    if (!host_window || !host_window->isVisible() || !isVisible()) return false;
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    const auto target = WindowFromPoint(cursor);
    return target && GetAncestor(target, GA_ROOT) == reinterpret_cast<HWND>(host_window->winId());
}
void WeiboWebView::geometryChange(const QRectF& value, const QRectF& old) {
    QQuickItem::geometryChange(value, old); update_geometry();
}
void WeiboWebView::update_geometry() {
    if (!state_) {
        if (isVisible() && window() && window()->isVisible() && isComponentComplete() && !url_.isEmpty()) schedule_open();
        return;
    }
    if (!state_->owner) {
        if (isVisible() && window() && window()->isVisible()) schedule_open();
        return;
    }
    if (!state_->host || !IsWindow(state_->host)) return;
    const auto origin = mapToScene(QPointF(0, 0));
    const auto scale = window() ? window()->devicePixelRatio() : 1;
    const auto w = qMax(1, qRound(width() * scale)), h = qMax(1, qRound(height() * scale));
    SetWindowPos(state_->host, nullptr, qRound(origin.x() * scale), qRound(origin.y() * scale), w, h,
        SWP_NOZORDER | SWP_NOACTIVATE);
    RECT bounds{0, 0, w, h};
    const bool visible = isVisible() && window() && window()->isVisible() && width() > 0 && height() > 0
        && error_.isEmpty() && content_ready_ && !state_->suppress_content;
    ShowWindow(state_->host, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (state_->controller) {
        state_->controller->put_Bounds(bounds);
        state_->controller->put_IsVisible(visible);
        state_->controller->NotifyParentWindowPositionChanged();
    }
}
bool WeiboWebView::eventFilter(QObject* object, QEvent* event) {
    if (object == attached_window_) {
        if (event->type() == QEvent::PlatformSurface) {
            const auto surface = static_cast<QPlatformSurfaceEvent*>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) shutdown();
            else schedule_open();
        } else if (event->type() == QEvent::DevicePixelRatioChange) update_geometry();
    }
    if (state_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        for (const auto& popup : state_->popups) if (object == popup->host) QTimer::singleShot(0, this, [popup] {
            if (!popup->host || !popup->controller) return;
            RECT bounds; GetClientRect(reinterpret_cast<HWND>(popup->host->winId()), &bounds);
            popup->controller->put_Bounds(bounds); popup->controller->put_IsVisible(popup->host->isVisible());
        });
    }
    return QQuickItem::eventFilter(object, event);
}
void WeiboWebView::focusInEvent(QFocusEvent* event) {
    QQuickItem::focusInEvent(event);
    if (event->reason() != Qt::TabFocusReason && event->reason() != Qt::BacktabFocusReason) return;
    QTimer::singleShot(0, this, [this] { if (state_ && state_->controller) state_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); });
}
void WeiboWebView::open_popup(ICoreWebView2NewWindowRequestedEventArgs* args) {
    LPWSTR raw = nullptr; args->get_Uri(&raw); const QUrl target(take(raw));
    if (!state_ || !state_->environment || !web_url(target)) { args->put_Handled(TRUE); return; }
    if (state_->guest && weibo_sign_in(target)) {
        args->put_Handled(TRUE);
        const std::weak_ptr<State> weak = state_;
        QTimer::singleShot(0, this, [weak] {
            const auto s = weak.lock();
            if (s && s->owner) s->owner->show_login_required();
        });
        return;
    }
    auto pending = std::make_shared<State::Request>();
    pending->args = args;
    if (FAILED(args->GetDeferral(&pending->deferral)) || !pending->deferral) {
        pending->completed = true; args->put_Handled(TRUE); return;
    }
    const std::weak_ptr<State> weak = state_;
    QTimer::singleShot(0, this, [weak, pending] {
        const auto s = weak.lock();
        if (!s || !s->owner || !s->environment) { pending->complete(); return; }
        auto popup = std::make_shared<State::Popup>();
        popup->dialog = new QDialog;
        popup->dialog->setWindowTitle(s->owner->site_ == QStringLiteral("xiaohongshu")
            ? QStringLiteral("小红书网页") : QStringLiteral("微博网页"));
        popup->dialog->setWindowModality(Qt::NonModal);
        auto* window = s->owner->window();
        if (window) popup->dialog->setWindowFlag(Qt::WindowStaysOnTopHint, window->flags().testFlag(Qt::WindowStaysOnTopHint));
        popup->dialog->resize(700, 680);
        popup->dialog->winId();
        if (window) {
            popup->dialog->windowHandle()->setTransientParent(window);
            const auto available = window->screen()->availableGeometry();
            popup->dialog->resize(qMin(700, available.width()), qMin(680, available.height()));
            popup->dialog->move(available.center() - QPoint(popup->dialog->width() / 2, popup->dialog->height() / 2));
        }
        auto* layout = new QVBoxLayout(popup->dialog);
        layout->setContentsMargins(0, 0, 0, 0);
        popup->host = new QWidget(popup->dialog);
        popup->host->setAttribute(Qt::WA_NativeWindow);
        popup->host->installEventFilter(s->owner);
        layout->addWidget(popup->host);
        s->popups.push_back(popup);
        QObject::connect(popup->dialog, &QDialog::finished, s->owner, [weak, popup] {
            const auto s = weak.lock(); if (!s || !s->owner) return;
            QTimer::singleShot(0, s->owner, [weak, popup] {
                const auto s = weak.lock(); if (!s || !s->owner) return;
                if (popup->controller) popup->controller->Close();
                popup->web.Reset(); popup->controller.Reset();
                if (popup->dialog) popup->dialog->deleteLater();
                s->popups.erase(std::remove(s->popups.begin(), s->popups.end(), popup), s->popups.end());
                s->owner->collect_cookies();
            });
        });
        popup->dialog->show(); popup->dialog->raise(); popup->dialog->activateWindow();
        const auto hr = s->environment->CreateCoreWebView2Controller(reinterpret_cast<HWND>(popup->host->winId()),
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [weak, popup, pending](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                    const auto s = weak.lock();
                    if (s && s->owner && popup->dialog && popup->dialog->isVisible() && SUCCEEDED(error) && controller) {
                        popup->controller = controller;
                        controller->get_CoreWebView2(&popup->web);
                        if (popup->web) {
                            RECT bounds; GetClientRect(reinterpret_cast<HWND>(popup->host->winId()), &bounds);
                            controller->put_Bounds(bounds); controller->put_IsVisible(TRUE);
                            controller->put_ZoomFactor(s->owner->zoom_);
                            EventRegistrationToken token;
                            popup->web->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                                [weak, popup](ICoreWebView2*, IUnknown*) -> HRESULT {
                                    const auto s = weak.lock(); if (s && s->owner) QTimer::singleShot(0, s->owner, [popup] { if (popup->dialog) popup->dialog->close(); });
                                    return S_OK;
                                }).Get(), &token);
                            popup->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [weak, popup](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (!s || !s->owner) return S_OK;
                                    LPWSTR raw = nullptr; args->get_Uri(&raw);
                                    const QUrl target(take(raw));
                                    if (s->guest && weibo_sign_in(target)) {
                                        args->put_Cancel(TRUE);
                                        QTimer::singleShot(0, s->owner, [weak, popup] {
                                            const auto s = weak.lock();
                                            if (s && s->owner) s->owner->show_login_required();
                                            if (popup->dialog) popup->dialog->close();
                                        });
                                    } else if (!web_url(target)) {
                                        args->put_Cancel(TRUE);
                                    } else if (s->owner->login_only_ && !auth_page(target)) {
                                        popup->controller->put_IsVisible(FALSE);
                                        QTimer::singleShot(0, s->owner, [popup] { if (popup->dialog) popup->dialog->hide(); });
                                    }
                                    return S_OK;
                                }).Get(), &token);
                            popup->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock(); if (s && s->owner) s->owner->open_popup(args); else args->put_Handled(TRUE);
                                    return S_OK;
                                }).Get(), &token);
                            popup->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                    const auto s = weak.lock(); if (s && s->owner) QTimer::singleShot(0, s->owner, [weak] {
                                        const auto s = weak.lock(); if (s && s->owner) s->owner->collect_cookies();
                                    }); return S_OK;
                                }).Get(), &token);
                            s->owner->apply_appearance();
                            pending->args->put_NewWindow(popup->web.Get());
                        }
                    }
                    pending->complete();
                    return S_OK;
                }).Get());
        if (FAILED(hr)) { pending->complete(); popup->dialog->close(); }
    });
}

void WeiboWebView::collect_cookies() {
    if (site_ != QStringLiteral("weibo") || !isVisible() || !state_ || !state_->cookies || state_->collecting || !session_) return;
    state_->collecting = true;
    const std::weak_ptr<State> weak = state_;
    const auto hr = state_->cookies->GetCookies(nullptr, Callback<ICoreWebView2GetCookiesCompletedHandler>(
        [weak](HRESULT error, ICoreWebView2CookieList* list) -> HRESULT {
            const auto s = weak.lock(); if (!s || !s->owner) return S_OK;
            s->collecting = false;
            if (FAILED(error) || !list) return S_OK;
            const auto cookies = cookie_list(list);
            QTimer::singleShot(0, s->owner, [weak, cookies] {
                const auto s = weak.lock(); if (!s || !s->owner || !s->owner->session_) return;
                bool has_mobile_sub = false;
                for (const auto& cookie : cookies)
                    has_mobile_sub |= cookie.name() == "SUB" && cookie.domain().endsWith(QStringLiteral("weibo.cn"));
                if (!s->guest && s->owner->session_->logged_in() && s->has_content && !s->owner->loading_ && !has_mobile_sub) {
                    s->owner->session_->logout(QStringLiteral("微博已退出登录 · 已切换匿名会话"));
                    return;
                }
                s->owner->session_->submit(cookies, s->agent);
            }); return S_OK;
        }).Get());
    if (FAILED(hr)) state_->collecting = false;
}
}
