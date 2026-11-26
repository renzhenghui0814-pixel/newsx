// Explicit live probe requested by the user. No UI, screenshots or credential output.
#include "desktop_weibo.h"
#include "news_service.h"
#include "weibo_session.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <cstdio>
#include <functional>

void output(const QJsonObject& value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    std::printf("%s\n", bytes.constData());
    std::fflush(stdout);
}
bool fixture() {
    const auto result = newsx::parse_desktop_weibo(R"HTML(
<script>var fake='<div class="card-wrap" mid="999"><p>wrong</p></div>';</script>
<div class="card-wrap" mid="101"><div class="avator"><img src="//wx1.sinaimg.cn/avatar.jpg"></div>
<div class="content"><a class="name" nick-name="A">A</a>
<div class="from"><a href="//weibo.com/u/bid?tracking=1">today</a></div>
<p node-type="feed_list_content" nick-name="A">main<img alt="[smile]" src="//face.example/x.png"></p>
<div class="card-comment"><p node-type="feed_list_content" nick-name="B">quote short</p>
<p node-type="feed_list_content_full" nick-name="B">quote full</p></div>
<li action-type="fl_pics"><img src="//wx1.sinaimg.cn/photo.jpg"></li>
<a class="WB_video_h5" data-str="sources:[{src:'//video.example/a.mp4'}],poster:'https://image.example/p.jpg'"></a>
</div><a action-type="feed_list_like">12</a></div>
<div class="card-wrap" mid="101"><p node-type="feed_list_content">duplicate</p></div>
<div class="card-wrap" mid="102"><p node-type="feed_list_content" nick-name="C">short</p>
<p node-type="feed_list_content_full" nick-name="C">full content</p></div>
<a class="next" href="?page=2">next</a>
)HTML");
    if (result.posts.size() != 2 || !result.has_more) return false;
    const auto row = result.posts[0].toObject();
    return row.value("id") == "101" && row.value("title") == "A"
        && row.value("text").toString().contains("main[smile]")
        && row.value("text").toString().contains("quote full")
        && row.value("images").toArray().size() == 1
        && row.value("video_url") == "https://video.example/a.mp4"
        && row.value("url") == "https://weibo.com/u/bid"
        && result.posts[1].toObject().value("text") == "full content";
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (!fixture()) { output({{"fixture_passed", false}}); return 10; }
    output({{"fixture_passed", true}});
    const auto keyword = app.arguments().value(1, QStringLiteral("南方豆腐掉地上能弹起来"));
    newsx::NewsService service;
    QNetworkAccessManager network;
    QUrl base(QStringLiteral("https://s.weibo.com/weibo"));
    QUrlQuery query;
    query.addQueryItem("q", "#" + keyword + "#");
    query.addQueryItem("t", "31"); query.addQueryItem("band_rank", "1"); query.addQueryItem("Refer", "top");
    base.setQuery(query);
    bool started = false;
    QSet<QString> all_ids;
    std::function<void(int)> raw_page;
    raw_page = [&](int page) {
        auto url = base;
        QUrlQuery q(url);
        if (page > 1) q.addQueryItem("page", QString::number(page));
        url.setQuery(q);
        QNetworkRequest request(url);
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setRawHeader("User-Agent", service.session()->user_agent().toUtf8());
        request.setRawHeader("Cookie", service.session()->cookie_header(url));
        auto* reply = network.get(request);
        QObject::connect(reply, &QNetworkReply::finished, &app, [&, reply, page] {
            const auto bytes = reply->read(4'000'000);
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            const auto parsed = newsx::parse_desktop_weibo(bytes);
            QJsonArray expected, actual;
            QSet<QString> seen;
            const QRegularExpression cards(QStringLiteral(R"rx(<div\b[^>]*class=["'][^"']*card-wrap[^"']*["'][^>]*\bmid=["'](\d+)["'])rx"));
            auto matches = cards.globalMatch(QString::fromUtf8(bytes));
            while (matches.hasNext()) {
                const auto id = matches.next().captured(1);
                if (!seen.contains(id)) { expected.append(id); seen.insert(id); }
            }
            int bodies = 0, videos = 0, photos = 0;
            for (const auto& value : parsed.posts) {
                const auto row = value.toObject();
                actual.append(row.value("id"));
                bodies += !row.value("text").toString().isEmpty();
                videos += !row.value("video_url").toString().isEmpty();
                photos += !row.value("images").toArray().isEmpty();
            }
            const bool order_ok = !actual.isEmpty() && expected == actual;
            output({{"phase", "same_html_order"}, {"page", page}, {"network_ok", ok},
                {"order_matches_html", order_ok}, {"count", actual.size()}, {"text_count", bodies},
                {"video_count", videos}, {"photo_posts", photos}, {"has_more", parsed.has_more}, {"ids", actual}});
            if (!ok || !order_ok || bodies != actual.size()) { app.exit(11); return; }
            if (page == 1) raw_page(2);
            else service.load_weibo_posts(keyword, base.toString(), 1);
        });
    };
    QObject::connect(&service, &newsx::NewsService::posts_failed, &app, [&](int, int page, const QString&) {
        output({{"phase", "service_failed"}, {"page", page}}); app.exit(12);
    });
    QObject::connect(&service, &newsx::NewsService::posts_ready, &app,
        [&](int, int page, const QJsonArray& rows, bool final, bool more, const QString&) {
            if (!final) return;
            int pc_rows = 0;
            QJsonArray ids;
            for (const auto& value : rows) {
                const auto row = value.toObject();
                const auto id = row.value("id").toString();
                ids.append(id); all_ids.insert(id);
                pc_rows += row.value("source") == QStringLiteral("电脑端官网");
            }
            output({{"phase", "actual_service"}, {"page", page}, {"count", rows.size()},
                {"pc_rows", pc_rows}, {"unique_total", all_ids.size()}, {"has_more", more}, {"ids", ids}});
            if (rows.isEmpty() || pc_rows != rows.size()) { app.exit(13); return; }
            if (page == 1 && more) service.load_more_weibo_posts(keyword, base.toString(), 1, 2);
            else app.exit(all_ids.size() > 15 ? 0 : 14);
        });
    QObject::connect(service.session(), &newsx::WeiboSession::changed, &app, [&] {
        if (!started && service.session()->logged_in()) { started = true; raw_page(1); }
    });
    QTimer::singleShot(90000, &app, [&] { app.exit(15); });
    return app.exec();
}
