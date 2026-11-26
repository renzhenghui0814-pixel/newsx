#include "weibo_login_dialog.h"
#include "weibo_session.h"
#include "settings.h"
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFocusEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScopedValueRollback>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QShowEvent>
#include <QHideEvent>
#include <algorithm>
#include <vector>
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace newsx {
namespace {
const wchar_t* login_url = L"https://passport.weibo.cn/signin/login?entry=mweibo&r=https%3A%2F%2Fm.weibo.cn%2F";
const wchar_t* desktop_login_url = L"https://passport.weibo.com/sso/signin?entry=miniblog&source=miniblog&url=https%3A%2F%2Fm.weibo.cn%2F";
bool allowed(const QString& raw) {
    const QUrl url(raw);
    const auto host = url.host().toLower();
    // Official legacy cross-domain SSO may redirect to HTTP. Preserve the
    // browser's URL/POST and normal secure-cookie rules instead of rewriting it.
    return (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")) &&
        (host == QStringLiteral("weibo.com") || host.endsWith(QStringLiteral(".weibo.com"))
         || host == QStringLiteral("weibo.cn") || host.endsWith(QStringLiteral(".weibo.cn"))
         || host == QStringLiteral("sina.com.cn") || host.endsWith(QStringLiteral(".sina.com.cn")));
}
bool allowed_navigation(const QString& raw) {
    return raw == QStringLiteral("about:blank") || allowed(raw);
}
QString take_string(LPWSTR value) {
    const auto result = QString::fromWCharArray(value ? value : L"");
    CoTaskMemFree(value);
    return result;
}
void login_stage(const QString& stage, HRESULT result = S_OK) {
    QDir().mkpath(app_data_dir());
    QFile file(QDir(app_data_dir()).filePath(QStringLiteral("weibo-login.jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    if (file.size() > 500'000) { file.resize(0); file.seek(0); }
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("result"), QString::number(static_cast<quint32>(result), 16)}
    }).toJson(QJsonDocument::Compact));
    file.write("\n");
}
}

struct WeiboLoginDialog::State {
    struct PopupRequest {
        ComPtr<ICoreWebView2NewWindowRequestedEventArgs> request;
        ComPtr<ICoreWebView2Deferral> deferral;
        bool completed = false;
        void complete() {
            if (completed) return;
            completed = true;
            request->put_Handled(TRUE);
            deferral->Complete();
        }
        ~PopupRequest() { complete(); }
    };
    struct Popup {
        QPointer<QDialog> dialog;
        QPointer<QWidget> host;
        ComPtr<ICoreWebView2Controller> controller;
        ComPtr<ICoreWebView2> web;
    };
    QPointer<WeiboLoginDialog> dialog;
    QPointer<WeiboSession> session;
    QLabel* status = nullptr;
    QWidget* host = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    ComPtr<ICoreWebView2CookieManager> cookies;
    ComPtr<ICoreWebView2Environment> environment;
    std::vector<std::shared_ptr<Popup>> popups;
    bool collecting = false;
    bool com_initialized = false;
    bool clear_cookies = false;
    int cookie_generation = 0;
    bool focus_pending = false;
    bool moving_focus = false;
    bool initialization_finished = false;
    bool initialization_started = false;
    bool renderer_failed = false;
    bool use_desktop_login = true;
    bool tried_alternate = false;
    bool loading = false;
    bool waiting_for_login_page = false;
    quint64 load_generation = 0;
    UINT64 navigation_id = 0;
    int automatic_rebuilds = 0;
    QString user_agent;
    QString last_cookie_summary;
};

WeiboLoginDialog::WeiboLoginDialog(WeiboSession& session, QWindow* owner)
    : state_(std::make_shared<State>()) {
    state_->dialog = this;
    state_->session = &session;
    login_stage(QStringLiteral("window_creating"));
    setWindowTitle(QStringLiteral("新浪微博登录"));
    // Keep the main window usable even if the browser fails to initialize.
    setWindowModality(Qt::NonModal);
    if (owner) setWindowFlag(Qt::WindowStaysOnTopHint, owner->flags().testFlag(Qt::WindowStaysOnTopHint));
    setMinimumSize(360, 440);
    resize(460, 650);
    winId();
    if (owner) {
        windowHandle()->setTransientParent(owner);
        const auto available = (owner->screen() ? owner->screen() : screen())->availableGeometry();
        resize(qMin(width(), available.width()), qMin(height(), available.height()));
        const auto center = owner->geometry().center();
        move(qBound(available.left(), center.x() - width() / 2, available.right() - width() + 1),
             qBound(available.top(), center.y() - height() / 2, available.bottom() - height() + 1));
    }
    auto* layout = new QVBoxLayout(this);
    auto* actions = new QHBoxLayout;
    state_->status = new QLabel(session.status(), this);
    state_->status->setWordWrap(true);
    actions->addWidget(state_->status, 1);
    auto* logout = new QPushButton(QStringLiteral("退出登录"), this);
    logout->setEnabled(session.logged_in());
    actions->addWidget(logout);
    auto* reload = new QPushButton(QStringLiteral("重新加载"), this);
    actions->addWidget(reload);
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    actions->addWidget(close);
    layout->addLayout(actions);
    auto* hint = new QLabel(QStringLiteral("请在微博官方页面登录，完成后自动返回。"), this);
    layout->addWidget(hint);
    state_->host = new QWidget(this);
    state_->host->setAttribute(Qt::WA_NativeWindow);
    state_->host->setFocusPolicy(Qt::StrongFocus);
    state_->host->installEventFilter(this);
    layout->addWidget(state_->host, 1);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(reload, &QPushButton::clicked, this, &WeiboLoginDialog::reload_page);
    connect(logout, &QPushButton::clicked, this, [this] {
        ++state_->cookie_generation;
        state_->clear_cookies = true;
        if (state_->cookies) state_->cookies->DeleteAllCookies();
        state_->session->logout();
        navigate_login();
    });
    connect(&session, &WeiboSession::statusChanged, this, [this] {
        state_->status->setText(state_->session->status());
    });
    connect(&session, &WeiboSession::changed, this, [this, logout] {
        logout->setEnabled(state_->session->logged_in());
        if (state_->session->logged_in()) accept();
    });
    auto* timer = new QTimer(this);
    timer->setInterval(1500);
    connect(timer, &QTimer::timeout, this, &WeiboLoginDialog::collect_cookies);
    timer->start();
    // Initialize only after showEvent, when Qt has created the final native host.
    login_stage(QStringLiteral("window_created"));
}

WeiboLoginDialog::~WeiboLoginDialog() {
    shutdown_browser();
}

void WeiboLoginDialog::shutdown_browser() {
    state_->dialog.clear();
    for (const auto& popup : state_->popups) {
        if (popup->controller) popup->controller->Close();
        popup->web.Reset();
        popup->controller.Reset();
        if (popup->dialog) { popup->dialog->close(); popup->dialog->deleteLater(); }
    }
    state_->popups.clear();
    if (state_->controller) state_->controller->Close();
    state_->cookies.Reset();
    state_->web.Reset();
    state_->controller.Reset();
    state_->environment.Reset();
    if (state_->com_initialized) { CoUninitialize(); state_->com_initialized = false; }
}

void WeiboLoginDialog::restart_browser(bool manual) {
    if (!manual && state_->automatic_rebuilds >= 1) {
        state_->renderer_failed = true;
        state_->status->setText(QStringLiteral("登录浏览器未能恢复，请关闭后重新打开登录窗口。"));
        login_stage(QStringLiteral("browser_rebuild_limit"));
        return;
    }
    auto fresh = std::make_shared<State>();
    fresh->dialog = this;
    fresh->session = state_->session;
    fresh->status = state_->status;
    fresh->host = state_->host;
    fresh->use_desktop_login = state_->use_desktop_login;
    fresh->automatic_rebuilds = manual ? 0 : state_->automatic_rebuilds + 1;
    shutdown_browser();
    state_ = std::move(fresh);
    login_stage(QStringLiteral("browser_rebuilding"));
    state_->status->setText(QStringLiteral("正在重新创建微博登录页面…"));
    QTimer::singleShot(0, this, &WeiboLoginDialog::initialize);
}

void WeiboLoginDialog::navigate_login() {
    if (!state_->web || !state_->controller) return;
    BOOL visible = FALSE;
    const auto health = state_->controller->get_IsVisible(&visible);
    if (FAILED(health)) {
        login_stage(QStringLiteral("controller_invalid"), health);
        restart_browser();
        return;
    }
    state_->loading = true;
    state_->waiting_for_login_page = !state_->session->logged_in();
    state_->navigation_id = 0;
    const auto generation = ++state_->load_generation;
    const std::weak_ptr<State> weak = state_;
    const auto target = state_->session->logged_in() ? L"https://m.weibo.cn/"
        : state_->use_desktop_login ? desktop_login_url : login_url;
    login_stage(state_->use_desktop_login ? QStringLiteral("loading_desktop_entry") : QStringLiteral("loading_mobile_entry"));
    state_->status->setText(QStringLiteral("正在加载微博官方登录页…"));
    const auto hr = state_->web->Navigate(target);
    if (FAILED(hr)) {
        state_->loading = false;
        state_->renderer_failed = true;
        login_stage(QStringLiteral("navigate_call_failed"), hr);
        state_->status->setText(QStringLiteral("登录浏览器已失效，请点击重新加载重建。"));
        return;
    }
    // Includes the case where Navigate succeeds but no navigation event arrives.
    QTimer::singleShot(25000, this, [weak, generation] {
        const auto s = weak.lock();
        if (!s || !s->dialog || s->load_generation != generation || !s->loading) return;
        s->loading = false;
        login_stage(QStringLiteral("page_load_timeout"));
        s->dialog->alternate_login();
    });
}

void WeiboLoginDialog::alternate_login() {
    if (!state_->tried_alternate && state_->waiting_for_login_page && state_->popups.empty()
        && !state_->session->logged_in() && isVisible()) {
        state_->tried_alternate = true;
        state_->use_desktop_login = !state_->use_desktop_login;
        const std::weak_ptr<State> weak = state_;
        QTimer::singleShot(0, this, [weak] {
            const auto s = weak.lock();
            if (s && s->dialog) s->dialog->navigate_login();
        });
    } else {
        state_->status->setText(QStringLiteral("微博登录页连接失败或超时，请点击重新加载后重试。"));
    }
}

bool WeiboLoginDialog::initialization_failed() const {
    return state_->renderer_failed || (state_->initialization_finished && !state_->web);
}

void WeiboLoginDialog::reload_page() {
    if (!state_->web || state_->renderer_failed) {
        if (state_->initialization_started && !state_->initialization_finished) {
            state_->status->setText(QStringLiteral("登录浏览器仍在初始化，请稍候或关闭后重试。"));
            return;
        }
        restart_browser(true);
        return;
    }
    for (const auto& popup : state_->popups)
        if (popup->dialog) popup->dialog->close();
    state_->tried_alternate = false;
    navigate_login();
}

void WeiboLoginDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        if (!isVisible()) return;
        if (!state_->initialization_started) initialize();
    });
}

void WeiboLoginDialog::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    // Do not tear down the browser or change native windows in a COM callback.
    QTimer::singleShot(0, this, [this] {
        if (isVisible()) return;
        if (state_->controller) state_->controller->put_IsVisible(FALSE);
        for (const auto& popup : state_->popups)
            if (popup->dialog) popup->dialog->close();
    });
}

void WeiboLoginDialog::open_popup(ICoreWebView2NewWindowRequestedEventArgs* args) {
    LPWSTR uri = nullptr;
    args->get_Uri(&uri);
    const auto url = take_string(uri);
    if (!allowed_navigation(url) || !state_->environment) {
        args->put_Handled(TRUE);
        login_stage(QStringLiteral("popup_blocked_host:") + QUrl(url).host());
        state_->status->setText(QStringLiteral("微博验证窗口未能打开，请重新加载后重试。"));
        return;
    }
    ComPtr<ICoreWebView2Deferral> deferral;
    if (FAILED(args->GetDeferral(&deferral)) || !deferral) {
        args->put_Handled(TRUE);
        return;
    }
    auto pending = std::make_shared<State::PopupRequest>();
    pending->request = args;
    pending->deferral = deferral;
    const std::weak_ptr<State> weak = state_;
    // A real WebView in the same environment preserves window.opener, POST
    // bodies, scripts and SSO callbacks. Navigate(uri) in the original view does not.
    QTimer::singleShot(0, this, [weak, pending] {
        const auto s = weak.lock();
        if (!s || !s->dialog || !s->environment || !s->dialog->isVisible()) {
            pending->complete();
            return;
        }
        auto popup = std::make_shared<State::Popup>();
        popup->dialog = new QDialog(s->dialog);
        popup->dialog->setWindowTitle(QStringLiteral("微博登录验证"));
        popup->dialog->setWindowModality(Qt::NonModal);
        popup->dialog->setWindowFlag(Qt::WindowStaysOnTopHint,
            s->dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        popup->dialog->resize(s->dialog->size());
        popup->dialog->move(s->dialog->pos() + QPoint(16, 16));
        auto* layout = new QVBoxLayout(popup->dialog);
        auto* hint = new QLabel(QStringLiteral("请完成微博官方验证，完成后自动同步登录状态。"), popup->dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        popup->host = new QWidget(popup->dialog);
        popup->host->setAttribute(Qt::WA_NativeWindow);
        popup->host->setFocusPolicy(Qt::StrongFocus);
        popup->host->installEventFilter(s->dialog);
        layout->addWidget(popup->host, 1);
        s->popups.push_back(popup);
        QObject::connect(popup->dialog, &QDialog::finished, s->dialog, [weak, popup] {
            const auto s = weak.lock();
            if (!s || !s->dialog) return;
            QTimer::singleShot(0, s->dialog, [weak, popup] {
                const auto s = weak.lock();
                if (!s || !s->dialog) return;
                if (popup->controller) popup->controller->Close();
                popup->web.Reset();
                popup->controller.Reset();
                if (popup->dialog) popup->dialog->deleteLater();
                s->popups.erase(std::remove(s->popups.begin(), s->popups.end(), popup), s->popups.end());
                login_stage(QStringLiteral("popup_closed"));
                s->dialog->collect_cookies();
            });
        });
        popup->dialog->show();
        popup->dialog->raise();
        popup->dialog->activateWindow();
        login_stage(QStringLiteral("popup_creating"));
        const auto hr = s->environment->CreateCoreWebView2Controller(
            reinterpret_cast<HWND>(popup->host->winId()),
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [weak, popup, pending](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                    const auto s = weak.lock();
                    if (s && s->dialog && popup->dialog && popup->dialog->isVisible()
                        && SUCCEEDED(error) && controller) {
                        popup->controller = controller;
                        controller->get_CoreWebView2(&popup->web);
                        if (popup->web) {
                            RECT bounds;
                            GetClientRect(reinterpret_cast<HWND>(popup->host->winId()), &bounds);
                            controller->put_Bounds(bounds);
                            controller->put_IsVisible(TRUE);
                            EventRegistrationToken token;
                            popup->web->add_WindowCloseRequested(Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                                [weak, popup](ICoreWebView2*, IUnknown*) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (s && s->dialog) QTimer::singleShot(0, s->dialog, [popup] {
                                        if (popup->dialog) popup->dialog->close();
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            popup->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    args->get_Uri(&uri);
                                    const auto url = take_string(uri);
                                    if (!allowed_navigation(url)) {
                                        args->put_Cancel(TRUE);
                                        login_stage(QStringLiteral("popup_navigation_blocked_host:") + QUrl(url).host());
                                    }
                                    return S_OK;
                                }).Get(), &token);
                            popup->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (s && s->dialog) s->dialog->open_popup(args);
                                    else args->put_Handled(TRUE);
                                    return S_OK;
                                }).Get(), &token);
                            popup->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (s && s->dialog) QTimer::singleShot(0, s->dialog, [weak] {
                                        const auto s = weak.lock();
                                        if (s && s->dialog) s->dialog->collect_cookies();
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            const auto attached = pending->request->put_NewWindow(popup->web.Get());
                            login_stage(QStringLiteral("popup_ready"), attached);
                            if (FAILED(attached)) {
                                s->status->setText(QStringLiteral("微博验证窗口连接失败，请重新加载后重试。"));
                            } else {
                                s->status->setText(QStringLiteral("请在微博验证窗口完成登录。"));
                            }
                        }
                    } else if (s && s->dialog) {
                        login_stage(QStringLiteral("popup_failed"), error);
                        s->status->setText(QStringLiteral("微博验证窗口创建失败，请重新加载后重试。"));
                    }
                    pending->complete();
                    return S_OK;
                }).Get());
        if (FAILED(hr)) {
            login_stage(QStringLiteral("popup_start_failed"), hr);
            pending->complete();
            popup->dialog->close();
        }
    });
}

void WeiboLoginDialog::initialize() {
    if (!isVisible()) return;
    if (state_->initialization_started) return;
    state_->initialization_started = true;
    login_stage(QStringLiteral("initializing"));
    const auto com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    state_->com_initialized = SUCCEEDED(com_result);
    if (FAILED(com_result)) {
        state_->initialization_finished = true;
        login_stage(QStringLiteral("com_failed"), com_result);
        state_->status->setText(QStringLiteral("登录窗口初始化失败，请关闭后重试。"));
        return;
    }
    state_->status->setText(QStringLiteral("正在加载微博官方登录页…"));
    const auto folder = QDir(app_data_dir()).filePath(QStringLiteral("weibo-webview"));
    QDir().mkpath(folder);
    const std::weak_ptr<State> weak = state_;
    auto fail = [this] {
        state_->initialization_finished = true;
        state_->status->setText(QStringLiteral("登录窗口加载失败，请安装或修复 Microsoft Edge WebView2 Runtime 后重试。"));
    };
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(nullptr,
        reinterpret_cast<LPCWSTR>(folder.utf16()), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
                const auto s = weak.lock();
                if (!s || !s->dialog) return S_OK;
                if (FAILED(error) || !environment) {
                    s->initialization_finished = true;
                    login_stage(QStringLiteral("environment_failed"), error);
                    s->status->setText(QStringLiteral("无法创建微博登录窗口，请安装 Microsoft Edge WebView2 Runtime。"));
                    return S_OK;
                }
                login_stage(QStringLiteral("environment_ready"));
                s->environment = environment;
                const auto hr = environment->CreateCoreWebView2Controller(
                    reinterpret_cast<HWND>(s->host->winId()),
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [weak](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                            const auto s = weak.lock();
                            if (!s || !s->dialog) return S_OK;
                            if (FAILED(error) || !controller) {
                                s->initialization_finished = true;
                                login_stage(QStringLiteral("controller_failed"), error);
                                s->status->setText(QStringLiteral("登录页面创建失败，请关闭后重试。"));
                                return S_OK;
                            }
                            s->controller = controller;
                            controller->get_CoreWebView2(&s->web);
                            if (!s->web) {
                                s->initialization_finished = true;
                                s->status->setText(QStringLiteral("登录页面初始化失败，请关闭后重试。"));
                                login_stage(QStringLiteral("webview_missing"));
                                return S_OK;
                            }
                            ComPtr<ICoreWebView2_2> web2;
                            if (SUCCEEDED(s->web.As(&web2))) web2->get_CookieManager(&s->cookies);
                            if (s->cookies && s->clear_cookies) s->cookies->DeleteAllCookies();
                            RECT bounds;
                            GetClientRect(reinterpret_cast<HWND>(s->host->winId()), &bounds);
                            controller->put_Bounds(bounds);
                            controller->put_IsVisible(s->dialog->isVisible());
                            ComPtr<ICoreWebView2Settings> settings;
                            s->web->get_Settings(&settings);
                            ComPtr<ICoreWebView2Settings2> settings2;
                            if (settings && SUCCEEDED(settings.As(&settings2))) {
                                LPWSTR agent = nullptr;
                                settings2->get_UserAgent(&agent);
                                s->user_agent = take_string(agent);
                            }
                            EventRegistrationToken token;
                            controller->add_MoveFocusRequested(Callback<ICoreWebView2MoveFocusRequestedEventHandler>(
                                [weak](ICoreWebView2Controller*, ICoreWebView2MoveFocusRequestedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (!s || !s->dialog) return S_OK;
                                    COREWEBVIEW2_MOVE_FOCUS_REASON reason;
                                    args->get_Reason(&reason);
                                    if (reason != COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT
                                        && reason != COREWEBVIEW2_MOVE_FOCUS_REASON_PREVIOUS) return S_OK;
                                    args->put_Handled(TRUE);
                                    // Leave the COM callback before entering Qt's focus machinery.
                                    QTimer::singleShot(0, s->dialog, [weak, reason] {
                                        const auto s = weak.lock();
                                        if (!s || !s->dialog || !s->dialog->isVisible()) return;
                                        s->dialog->focusNextPrevChild(reason == COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
                                    });
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    args->get_Uri(&uri);
                                    const auto url = take_string(uri);
                                    const auto s = weak.lock();
                                    if (s && s->dialog) {
                                        args->get_NavigationId(&s->navigation_id);
                                        login_stage(QStringLiteral("navigation_start_host:") + QUrl(url).host());
                                    }
                                    if (!allowed_navigation(url)) {
                                        args->put_Cancel(TRUE);
                                        login_stage(QStringLiteral("navigation_blocked_host:") + QUrl(url).host());
                                        if (s && s->dialog) s->status->setText(QStringLiteral("登录跳转未完成，请点击重新加载后重试。"));
                                    }
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (s && s->dialog) s->dialog->open_popup(args);
                                    else args->put_Handled(TRUE);
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (!s || !s->dialog) return S_OK;
                                    UINT64 navigation_id = 0;
                                    args->get_NavigationId(&navigation_id);
                                    if (s->navigation_id && navigation_id != s->navigation_id) return S_OK;
                                    BOOL success = FALSE;
                                    args->get_IsSuccess(&success);
                                    if (!success) {
                                        COREWEBVIEW2_WEB_ERROR_STATUS status;
                                        args->get_WebErrorStatus(&status);
                                        if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) return S_OK;
                                        s->loading = false;
                                        login_stage(QStringLiteral("navigation_failed:") + QString::number(status));
                                        QTimer::singleShot(0, s->dialog, [weak] {
                                            const auto s = weak.lock();
                                            if (s && s->dialog) s->dialog->alternate_login();
                                        });
                                    } else {
                                        s->loading = false;
                                        s->waiting_for_login_page = false;
                                        login_stage(QStringLiteral("navigation_completed"));
                                        if (!s->session->logged_in())
                                            s->status->setText(QStringLiteral("请在微博官方页面完成登录。"));
                                        QTimer::singleShot(0, s->dialog, [weak] {
                                            const auto s = weak.lock();
                                            if (s && s->dialog) s->dialog->collect_cookies();
                                        });
                                    }
                                    return S_OK;
                                }).Get(), &token);
                            s->web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                                [weak](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                                    const auto s = weak.lock();
                                    if (!s || !s->dialog) return S_OK;
                                    s->renderer_failed = true;
                                    login_stage(QStringLiteral("browser_process_failed"));
                                    s->status->setText(QStringLiteral("登录页面进程已退出，请关闭后重新打开登录窗口。"));
                                    return S_OK;
                                }).Get(), &token);
                            s->initialization_finished = true;
                            login_stage(QStringLiteral("controller_ready"));
                            s->status->setText(s->session->status());
                            QTimer::singleShot(0, s->dialog, [weak] {
                                const auto s = weak.lock();
                                if (!s || !s->dialog || !s->web || !s->session) return;
                                login_stage(QStringLiteral("navigating"));
                                s->dialog->navigate_login();
                            });
                            return S_OK;
                        }).Get());
                if (FAILED(hr)) {
                    s->initialization_finished = true;
                    login_stage(QStringLiteral("controller_start_failed"), hr);
                    s->status->setText(QStringLiteral("登录窗口初始化失败，请关闭后重试。"));
                }
                return S_OK;
            }).Get());
    if (FAILED(result)) { login_stage(QStringLiteral("environment_start_failed"), result); fail(); }
    QTimer::singleShot(30000, this, [weak] {
        const auto s = weak.lock();
        if (!s || !s->dialog || s->initialization_finished) return;
        s->initialization_finished = true;
        login_stage(QStringLiteral("initialization_timeout"));
        s->status->setText(QStringLiteral("登录页面加载超时，可关闭窗口后重试；主窗口仍可使用。"));
    });
}

void WeiboLoginDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    QTimer::singleShot(0, this, [this] {
        if (!state_->controller) return;
        RECT bounds;
        GetClientRect(reinterpret_cast<HWND>(state_->host->winId()), &bounds);
        state_->controller->put_Bounds(bounds);
    });
}

bool WeiboLoginDialog::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        for (const auto& popup : state_->popups) {
            if (object != popup->host) continue;
            QTimer::singleShot(0, this, [popup] {
                if (!popup->host || !popup->controller) return;
                RECT bounds;
                GetClientRect(reinterpret_cast<HWND>(popup->host->winId()), &bounds);
                popup->controller->put_Bounds(bounds);
                popup->controller->put_IsVisible(popup->host->isVisible());
            });
        }
    }
    if (object == state_->host && event->type() == QEvent::FocusIn && state_->controller
        && !state_->moving_focus && !state_->focus_pending) {
        const auto reason = static_cast<QFocusEvent*>(event)->reason();
        // Native focus notifications use OtherFocusReason. Feeding those back
        // into MoveFocus can recurse between Qt and WebView2 indefinitely.
        if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
            || reason == Qt::MouseFocusReason || reason == Qt::ShortcutFocusReason) {
            state_->focus_pending = true;
            QTimer::singleShot(0, this, [this] {
                state_->focus_pending = false;
                if (!isVisible() || !state_->host->hasFocus() || !state_->controller) return;
                QScopedValueRollback<bool> guard(state_->moving_focus, true);
                state_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            });
        }
    }
    return QDialog::eventFilter(object, event);
}

void WeiboLoginDialog::collect_cookies() {
    if (!isVisible() || !state_->cookies || state_->collecting || state_->session->logged_in()) return;
    state_->collecting = true;
    const auto generation = state_->cookie_generation;
    const std::weak_ptr<State> weak = state_;
    const auto hr = state_->cookies->GetCookies(nullptr,
        Callback<ICoreWebView2GetCookiesCompletedHandler>(
            [weak, generation](HRESULT error, ICoreWebView2CookieList* list) -> HRESULT {
                const auto s = weak.lock();
                if (!s || !s->dialog) return S_OK;
                s->collecting = false;
                if (generation != s->cookie_generation) return S_OK;
                if (FAILED(error) || !list) return S_OK;
                UINT count = 0;
                list->get_Count(&count);
                QList<QNetworkCookie> cookies;
                for (UINT i = 0; i < count; ++i) {
                    ComPtr<ICoreWebView2Cookie> item;
                    if (FAILED(list->GetValueAtIndex(i, &item)) || !item) continue;
                    LPWSTR value = nullptr;
                    item->get_Name(&value); const auto name = take_string(value).toUtf8();
                    value = nullptr; item->get_Value(&value); const auto content = take_string(value).toUtf8();
                    value = nullptr; item->get_Domain(&value); const auto domain = take_string(value);
                    if (!allowed(QStringLiteral("https://") + (domain.startsWith('.') ? domain.mid(1) : domain))) continue;
                    QNetworkCookie cookie(name, content);
                    cookie.setDomain(domain);
                    value = nullptr; item->get_Path(&value); cookie.setPath(take_string(value));
                    BOOL secure = FALSE, http_only = FALSE, session = TRUE;
                    item->get_IsSecure(&secure); item->get_IsHttpOnly(&http_only); item->get_IsSession(&session);
                    cookie.setSecure(secure); cookie.setHttpOnly(http_only);
                    if (!session) {
                        double expires = 0;
                        item->get_Expires(&expires);
                        cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(expires), Qt::UTC));
                    }
                    cookies.append(cookie);
                }
                int mobile = 0, desktop = 0;
                for (const auto& cookie : cookies) {
                    if (cookie.name() != "SUB") continue;
                    if (cookie.domain().endsWith(QStringLiteral("weibo.cn"))) ++mobile;
                    if (cookie.domain().endsWith(QStringLiteral("weibo.com"))) ++desktop;
                }
                const auto summary = QStringLiteral("cookie_domains_cn:%1_com:%2").arg(mobile).arg(desktop);
                if (summary != s->last_cookie_summary) {
                    login_stage(summary);
                    s->last_cookie_summary = summary;
                }
                if (s->session) s->session->submit(cookies, s->user_agent);
                return S_OK;
            }).Get());
    if (FAILED(hr)) state_->collecting = false;
}
}
