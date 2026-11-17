#include "news_service.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <QUrlQuery>
#include <QVector>
#include <algorithm>
#include <memory>

namespace newsx {
namespace {

constexpr auto kDesktopAgent = "Mozilla/5.0";
constexpr auto kMobileAgent = "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 "
                              "(KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36";

QString text(const QJsonValue& value) {
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble(), 'f', 0);
    return {};
}

QString plain_html(const QString& source) {
    QTextDocument document;
    document.setHtml(source);
    return document.toPlainText().trimmed();
}

QString safe_url(const QString& source) {
    const QUrl url(source);
    return (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))
        ? url.toString() : QString{};
}

QJsonArray post_links(const QString& html) {
    const QRegularExpression anchors(QStringLiteral(R"(<a\b[^>]*href=["']([^"']+)["'][^>]*>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    QJsonArray links;
    auto matches = anchors.globalMatch(html);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto url = safe_url(plain_html(match.captured(1)));
        const auto label = plain_html(match.captured(2));
        if (!url.isEmpty() && !label.isEmpty())
            links.append(QJsonObject{{QStringLiteral("label"), label}, {QStringLiteral("url"), url}});
    }
    return links;
}

QString escape_html(const QString& source) {
    QString out;
    out.reserve(source.size());
    for (const auto ch : source) {
        switch (ch.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        default: out += ch;
        }
    }
    return out;
}

// Turns one paragraph of the article HTML into rich text for QtQuick.Text: <b> runs
// are kept, <br> survives, everything else is dropped.
QString rich_paragraph(const QString& html) {
    static const QRegularExpression emphasis_open(QStringLiteral(R"(<\s*(strong|b)\b[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression emphasis_close(QStringLiteral(R"(<\s*/\s*(strong|b)\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression line_break(QStringLiteral(R"(<\s*br\s*/?\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    QString marked = html;
    marked.replace(emphasis_open, QStringLiteral("\x01"));
    marked.replace(emphasis_close, QStringLiteral("\x02"));
    marked.replace(line_break, QStringLiteral("\x03"));
    QString out;
    bool bold = false;
    for (const auto ch : plain_html(marked)) {
        if (ch.unicode() == 1) {
            if (!bold) { out += QStringLiteral("<b>"); bold = true; }
        } else if (ch.unicode() == 2) {
            if (bold) { out += QStringLiteral("</b>"); bold = false; }
        } else if (ch.unicode() == 3) {
            out += QStringLiteral("<br/>");
        } else {
            out += escape_html(QString(ch));
        }
    }
    if (bold) out += QStringLiteral("</b>");
    return out.trimmed();
}

QJsonObject build_article(const QString& html, const QJsonObject& detail) {
    QJsonArray blocks;
    QJsonArray lead_images;
    QJsonArray paragraphs;
    QStringList images;
    QSet<QString> seen_images;
    const auto add_image = [&blocks, &lead_images, &images, &seen_images](const QString& raw, bool from_content) {
        const auto url = safe_url(raw.trimmed());
        if (url.isEmpty()) return;
        if (from_content)
            blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                      {QStringLiteral("url"), url}});
        else if (!seen_images.contains(url))
            lead_images.append(url);
        if (!seen_images.contains(url)) {
            seen_images.insert(url);
            images.append(url);
        }
    };

    // Walk the source in document order. Images may sit inside a paragraph or
    // between paragraphs, so collecting them separately loses their position.
    const QRegularExpression tag(
        QStringLiteral(R"(<\s*/?\s*(?:p|div|h[1-6]|li)\b[^>]*>|<img\b[^>]*>)"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression image_tag(QStringLiteral(R"(<img\b[^>]*src=["']([^"']+)["'][^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    int cursor = 0;
    QString segment;
    const auto flush_text = [&]() {
        const auto rich = rich_paragraph(segment);
        segment.clear();
        if (rich.isEmpty() || paragraphs.size() >= 300) return;
        paragraphs.append(rich);
        blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                  {QStringLiteral("text"), rich}});
    };
    auto iterator = tag.globalMatch(html);
    while (iterator.hasNext() && paragraphs.size() < 300) {
        const auto match = iterator.next();
        segment += html.mid(cursor, match.capturedStart() - cursor);
        flush_text();
        const auto markup = match.captured();
        if (markup.startsWith(QStringLiteral("<img"), Qt::CaseInsensitive)) {
            const auto image = image_tag.match(markup);
            if (image.hasMatch()) add_image(image.captured(1), true);
        }
        cursor = match.capturedEnd();
    }
    if (paragraphs.size() < 300) {
        segment += html.mid(cursor);
        flush_text();
    }
    for (const auto& value : detail.value(QStringLiteral("images")).toArray())
        add_image(value.toString(), false);

    // 电报类文章的 brief 就是正文本身，重复展示没有意义
    auto brief = detail.value(QStringLiteral("brief")).toString().trimmed();
    if (!brief.isEmpty()) {
        const auto plain_brief = plain_html(brief);
        const auto plain_body = plain_html(html);
        if (plain_brief.isEmpty() || plain_body.startsWith(plain_brief)) brief.clear();
    }

    auto title = detail.value(QStringLiteral("title")).toString().trimmed();
    return QJsonObject{{QStringLiteral("title"), title},
        {QStringLiteral("brief"), brief},
        {QStringLiteral("source"), detail.value(QStringLiteral("source"))},
        {QStringLiteral("published_at"), detail.value(QStringLiteral("ctime"))},
        {QStringLiteral("lead_images"), lead_images},
        {QStringLiteral("blocks"), blocks},
        {QStringLiteral("paragraphs"), paragraphs},
        {QStringLiteral("images"), QJsonArray::fromStringList(images)}};
}

QJsonArray parse_hot_band(const QByteArray& bytes) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    const auto list = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("band_list")).toArray();
    QJsonArray result;
    QSet<QString> seen;
    for (const auto& value : list) {
        const auto row = value.toObject();
        bool rank_ok = false;
        const int rank = text(row.value(QStringLiteral("realpos"))).toInt(&rank_ok);
        if (!rank_ok || rank <= 0) continue;
        QString title = row.value(QStringLiteral("word")).toString().trimmed();
        if (title.startsWith('#') && title.endsWith('#')) title = title.mid(1, title.size() - 2);
        if (title.isEmpty() || seen.contains(title)) continue;
        seen.insert(title);
        QUrl url(QStringLiteral("https://s.weibo.com/weibo"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), text(row.value(QStringLiteral("word_scheme"))).isEmpty()
            ? row.value(QStringLiteral("word")).toString() : text(row.value(QStringLiteral("word_scheme"))));
        url.setQuery(query);
        QJsonObject item{{QStringLiteral("rank"), rank}, {QStringLiteral("title"), title},
                         {QStringLiteral("url"), url.toString()},
                         {QStringLiteral("hot"), text(row.value(QStringLiteral("raw_hot"))).isEmpty()
                            ? text(row.value(QStringLiteral("num"))) : text(row.value(QStringLiteral("raw_hot")))},
                         {QStringLiteral("tag"), text(row.value(QStringLiteral("label_name"))).isEmpty()
                            ? text(row.value(QStringLiteral("icon_desc"))) : text(row.value(QStringLiteral("label_name")))}};
        result.append(item);
        if (result.size() >= 60) break;
    }
    return result;
}

QJsonArray parse_snapshot(const QByteArray& bytes) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    for (const auto& value : root.value(QStringLiteral("boards")).toArray()) {
        const auto board = value.toObject();
        if (board.value(QStringLiteral("platform")).toString() != QStringLiteral("weibo")) continue;
        QJsonArray result;
        for (const auto& entry : board.value(QStringLiteral("items")).toArray()) {
            const auto row = entry.toObject();
            const auto title = row.value(QStringLiteral("title")).toString().trimmed();
            if (title.isEmpty()) continue;
            const int rank = row.value(QStringLiteral("rank")).toInt(result.size() + 1);
            if (rank <= 0) continue;
            result.append(QJsonObject{{QStringLiteral("rank"), rank},
                {QStringLiteral("title"), title}, {QStringLiteral("url"), safe_url(row.value(QStringLiteral("url")).toString())},
                {QStringLiteral("hot"), text(row.value(QStringLiteral("hot_text"))).isEmpty()
                    ? text(row.value(QStringLiteral("hot"))) : text(row.value(QStringLiteral("hot_text")))},
                {QStringLiteral("tag"), row.value(QStringLiteral("tag")).toString()}});
            if (result.size() >= 30) break;
        }
        return result;
    }
    return {};
}

QJsonArray parse_hot_page(const QByteArray& bytes) {
    const auto html = QString::fromUtf8(bytes);
    if (!html.contains(QStringLiteral("pl_top_realtimehot"))) return {};
    const QRegularExpression rows(QStringLiteral(R"(<tr\b[^>]*>(.*?)</tr>)"),
                                  QRegularExpression::DotMatchesEverythingOption |
                                  QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression rank_cell(QStringLiteral(R"(<td\b[^>]*class=["'][^"']*td-01[^"']*["'][^>]*>(.*?)</td>)"),
                                       QRegularExpression::DotMatchesEverythingOption |
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression title_cell(QStringLiteral(R"(<td\b[^>]*class=["'][^"']*td-02[^"']*["'][^>]*>(.*?)</td>)"),
                                        QRegularExpression::DotMatchesEverythingOption |
                                        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression link(QStringLiteral(R"(<a\b[^>]*href=["']([^"']+)["'][^>]*>(.*?)</a>)"),
                                  QRegularExpression::DotMatchesEverythingOption |
                                  QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression heat(QStringLiteral(R"(<span\b[^>]*>(.*?)</span>)"),
                                  QRegularExpression::DotMatchesEverythingOption |
                                  QRegularExpression::CaseInsensitiveOption);
    QJsonArray result;
    QSet<QString> seen;
    auto iterator = rows.globalMatch(html);
    while (iterator.hasNext()) {
        const auto row = iterator.next().captured(1);
        bool valid = false;
        const int rank = plain_html(rank_cell.match(row).captured(1)).toInt(&valid);
        if (!valid || rank <= 0) continue;
        const auto middle = title_cell.match(row).captured(1);
        const auto found_link = link.match(middle);
        if (!found_link.hasMatch()) continue;
        const auto title = plain_html(found_link.captured(2));
        const auto href = found_link.captured(1);
        if (title.isEmpty() || seen.contains(title) || href.startsWith(QStringLiteral("javascript:"))) continue;
        const auto url = QUrl(QStringLiteral("https://s.weibo.com/top/summary?cate=realtimehot"))
                             .resolved(QUrl(href)).toString();
        if (safe_url(url).isEmpty()) continue;
        seen.insert(title);
        result.append(QJsonObject{{QStringLiteral("rank"), rank}, {QStringLiteral("title"), title},
            {QStringLiteral("url"), url}, {QStringLiteral("hot"), plain_html(heat.match(middle).captured(1))},
            {QStringLiteral("tag"), QString{}}});
        if (result.size() >= 60) break;
    }
    return result;
}

QJsonArray parse_headlines(const QByteArray& bytes) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (text(root.value(QStringLiteral("errno"))) != QStringLiteral("0")) return {};
    const auto data = root.value(QStringLiteral("data")).toObject();
    QJsonArray rows = data.value(QStringLiteral("top_article")).toArray();
    for (const auto& item : data.value(QStringLiteral("depth_list")).toArray()) rows.append(item);
    QVector<QJsonObject> articles;
    QSet<qint64> seen;
    for (const auto& value : rows) {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("is_ad")).toBool()) continue;
        const auto id = text(row.value(QStringLiteral("id"))).toLongLong();
        const auto title = text(row.value(QStringLiteral("title"))).trimmed();
        if (id <= 0 || title.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        articles.append(QJsonObject{{QStringLiteral("id"), static_cast<double>(id)},
            {QStringLiteral("title"), title}, {QStringLiteral("brief"), row.value(QStringLiteral("brief"))},
            {QStringLiteral("source"), row.value(QStringLiteral("source")).toString(QStringLiteral("财联社"))},
            {QStringLiteral("published_at"), row.value(QStringLiteral("ctime"))},
            {QStringLiteral("url"), QStringLiteral("https://www.cls.cn/detail/%1").arg(id)}});
    }
    std::stable_sort(articles.begin(), articles.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return text(left.value(QStringLiteral("published_at"))).toLongLong()
            > text(right.value(QStringLiteral("published_at"))).toLongLong();
    });
    QJsonArray result;
    for (const auto& article : articles) {
        result.append(article);
        if (result.size() >= 30) break;
    }
    return result;
}

QJsonArray parse_posts(const QByteArray& bytes) {
    const auto cards = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("data"))
                           .toObject().value(QStringLiteral("cards")).toArray();
    QJsonArray result;
    QSet<QString> seen;
    for (const auto& value : cards) {
        const auto card = value.toObject();
        QJsonArray candidates{card};
        for (const auto& group : card.value(QStringLiteral("card_group")).toArray()) candidates.append(group);
        for (const auto& candidate : candidates) {
            const auto blog = candidate.toObject().value(QStringLiteral("mblog")).toObject();
            if (blog.value(QStringLiteral("id")).isUndefined()) continue;
            const auto bid = text(blog.value(QStringLiteral("bid"))).isEmpty()
                ? text(blog.value(QStringLiteral("id"))) : text(blog.value(QStringLiteral("bid")));
            if (bid.isEmpty() || seen.contains(bid)) continue;
            seen.insert(bid);
            QJsonArray images;
            for (const auto& picture : blog.value(QStringLiteral("pics")).toArray()) {
                const auto pic = picture.toObject();
                QString image = pic.value(QStringLiteral("large")).toObject().value(QStringLiteral("url")).toString();
                if (image.isEmpty()) image = pic.value(QStringLiteral("original")).toObject().value(QStringLiteral("url")).toString();
                if (image.isEmpty()) image = pic.value(QStringLiteral("url")).toString();
                image = safe_url(image);
                if (!image.isEmpty() && !images.contains(image)) images.append(image);
            }
            const auto page = blog.value(QStringLiteral("page_info")).toObject();
            const auto user = blog.value(QStringLiteral("user")).toObject();
            const auto media = page.value(QStringLiteral("media_info")).toObject();
            QString video;
            QString poster;
            if (page.value(QStringLiteral("type")).toString() == QStringLiteral("video")) {
                video = safe_url(media.value(QStringLiteral("stream_url_hd")).toString());
                if (video.isEmpty()) video = safe_url(media.value(QStringLiteral("stream_url")).toString());
                poster = safe_url(page.value(QStringLiteral("page_pic")).toObject().value(QStringLiteral("url")).toString());
            }
            result.append(QJsonObject{{QStringLiteral("id"), text(blog.value(QStringLiteral("id")))},
                {QStringLiteral("title"), user.value(QStringLiteral("screen_name")).toString(QStringLiteral("微博用户"))},
                {QStringLiteral("avatar"), safe_url(user.value(QStringLiteral("profile_image_url")).toString())},
                {QStringLiteral("text"), plain_html(blog.value(QStringLiteral("text")).toString())},
                {QStringLiteral("links"), post_links(blog.value(QStringLiteral("text")).toString())},
                {QStringLiteral("url"), QStringLiteral("https://m.weibo.cn/status/") + bid},
                {QStringLiteral("images"), images}, {QStringLiteral("video_url"), video},
                {QStringLiteral("video_poster"), poster},
                {QStringLiteral("published_at"), blog.value(QStringLiteral("created_at")).toString()},
                {QStringLiteral("reposts_count"), blog.value(QStringLiteral("reposts_count"))},
                {QStringLiteral("comments_count"), blog.value(QStringLiteral("comments_count"))},
                {QStringLiteral("attitudes_count"), blog.value(QStringLiteral("attitudes_count"))},
                {QStringLiteral("complete"), !blog.value(QStringLiteral("isLongText")).toBool()}});
        }
    }
    return result;
}

QJsonArray parse_comments(const QByteArray& bytes) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (root.value(QStringLiteral("ok")).toInt() != 1) return {};
    const auto entries = root.value(QStringLiteral("data")).toObject()
                         .value(QStringLiteral("data")).toArray();
    QJsonArray result;
    for (const auto& value : entries) {
        const auto comment = value.toObject();
        const auto user = comment.value(QStringLiteral("user")).toObject();
        const auto body = plain_html(comment.value(QStringLiteral("text")).toString());
        if (body.isEmpty()) continue;
        result.append(QJsonObject{
            {QStringLiteral("id"), text(comment.value(QStringLiteral("id")))},
            {QStringLiteral("author"), user.value(QStringLiteral("screen_name")).toString(QStringLiteral("微博用户"))},
            {QStringLiteral("avatar"), safe_url(user.value(QStringLiteral("profile_image_url")).toString())},
            {QStringLiteral("text"), body},
            {QStringLiteral("published_at"), comment.value(QStringLiteral("created_at")).toString()}});
        if (result.size() >= 10) break;
    }
    return result;
}

} // namespace

NewsService::NewsService(QObject* parent) : QObject(parent) {}
NewsService::~NewsService() {
    for (auto* reply : network_.findChildren<QNetworkReply*>()) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
    }
}

void NewsService::get(const QUrl& url, const QString& referer, const QString& agent,
                      qint64 max_bytes, ReplyHandler handler, const QByteArray& cookie) {
    QNetworkRequest request(url);
    request.setTransferTimeout(12000);
    request.setRawHeader("User-Agent", agent.toUtf8());
    if (!referer.isEmpty()) request.setRawHeader("Referer", referer.toUtf8());
    if (!cookie.isEmpty()) {
        request.setRawHeader("Cookie", cookie);
        request.setRawHeader("MWeibo-Pwa", "1");
        request.setRawHeader("X-Requested-With", "XMLHttpRequest");
    }
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, max_bytes, handler = std::move(handler)]() mutable {
        const auto data = reply->read(max_bytes + 1);
        const auto error = reply->error() == QNetworkReply::NoError
            ? (data.size() > max_bytes ? QStringLiteral("响应过大") : QString{}) : reply->errorString();
        reply->deleteLater();
        handler(data, error);
    });
}

void NewsService::refresh_hot(const QString& custom_url) {
    if (!custom_url.isEmpty() && !custom_url.contains(QStringLiteral("weibo.com"))) {
        hot_snapshot(custom_url);
        return;
    }
    get(QUrl(QStringLiteral("https://weibo.com/ajax/statuses/hot_band")),
        QStringLiteral("https://weibo.com/"), QString::fromLatin1(kDesktopAgent), 2'000'000,
        [this](const QByteArray& bytes, const QString& error) {
            const auto rows = error.isEmpty() ? parse_hot_band(bytes) : QJsonArray{};
            if (!rows.isEmpty()) emit hot_ready(rows, QStringLiteral("官网接口"));
            else hot_page(0);
        });
}

void NewsService::hot_page(int attempt) {
    const auto url = attempt == 0
        ? QStringLiteral("https://s.weibo.com/top/summary?cate=realtimehot")
        : QStringLiteral("https://s.weibo.com/top/summary/summary?cate=realtimehot");
    get(QUrl(url), QStringLiteral("https://s.weibo.com/"), QString::fromLatin1(kDesktopAgent),
        3'000'000, [this, attempt](const QByteArray& bytes, const QString& error) {
            const auto rows = error.isEmpty() ? parse_hot_page(bytes) : QJsonArray{};
            if (!rows.isEmpty()) emit hot_ready(rows, QStringLiteral("官网"));
            else if (attempt == 0) hot_page(1);
            else hot_snapshot();
        });
}

void NewsService::hot_snapshot(const QString& custom_url) {
    const auto url = custom_url.isEmpty()
        ? QStringLiteral("https://solarhell.github.io/hot-search-crawler/latest.json") : custom_url;
    get(QUrl(url), {}, QStringLiteral("NewsXWidget/0.1"), 3'000'000,
        [this](const QByteArray& bytes, const QString& error) {
            const auto rows = error.isEmpty() ? parse_snapshot(bytes) : QJsonArray{};
            if (!rows.isEmpty()) emit hot_ready(rows, QStringLiteral("备用源"));
            else emit hot_failed(error.isEmpty() ? QStringLiteral("微博热榜不可用") : error);
        });
}

void NewsService::refresh_headlines() {
    const QByteArray parameters("appName=CailianpressWeb&os=web&sv=8.7.9");
    const auto sha = QCryptographicHash::hash(parameters, QCryptographicHash::Sha1).toHex();
    const auto sign = QCryptographicHash::hash(sha, QCryptographicHash::Md5).toHex();
    const QUrl url(QStringLiteral("https://www.cls.cn/v3/depth/home/assembled/1000?")
                   + QString::fromLatin1(parameters) + QStringLiteral("&sign=") + QString::fromLatin1(sign));
    get(url, QStringLiteral("https://www.cls.cn/"), QString::fromLatin1(kDesktopAgent), 3'000'000,
        [this](const QByteArray& bytes, const QString& error) {
            const auto rows = error.isEmpty() ? parse_headlines(bytes) : QJsonArray{};
            if (!rows.isEmpty()) emit headlines_ready(rows);
            else emit headlines_failed(error.isEmpty() ? QStringLiteral("财联社头条不可用") : error);
        });
}

void NewsService::ensure_visitor(std::function<void(QByteArray)> ready) {
    if (!visitor_cookie_.isEmpty()) { ready(visitor_cookie_); return; }
    get(QUrl(QStringLiteral("https://visitor.passport.weibo.cn/visitor/genvisitor2?cb=visitor_callback&from=weibo.cn")),
        {}, QString::fromLatin1(kMobileAgent), 100'000,
        [this, ready = std::move(ready)](const QByteArray& bytes, const QString& error) {
            if (!error.isEmpty()) { ready({}); return; }
            const QRegularExpression expression(QStringLiteral(R"(\((\{.*\})\))"));
            const auto match = expression.match(QString::fromUtf8(bytes));
            const auto data = QJsonDocument::fromJson(match.captured(1).toUtf8())
                                  .object().value(QStringLiteral("data")).toObject();
            const auto sub = data.value(QStringLiteral("sub")).toString();
            const auto subp = data.value(QStringLiteral("subp")).toString();
            if (!sub.isEmpty() && !subp.isEmpty())
                visitor_cookie_ = QByteArray("SUB=") + sub.toUtf8() + "; SUBP=" + subp.toUtf8();
            ready(visitor_cookie_);
        });
}

void NewsService::load_weibo_posts(const QString& keyword, const QString& search_url, int token) {
    ensure_visitor([this, keyword, search_url, token](const QByteArray& cookie) {
        if (cookie.isEmpty()) emit posts_failed(token, 1, QStringLiteral("微博访客会话不可用"));
        else search_weibo(keyword, search_url, token, 1, 0);
    });
}

void NewsService::load_more_weibo_posts(const QString& keyword, const QString& search_url,
                                        int token, int page) {
    search_weibo(keyword, search_url, token, page, 0);
}

void NewsService::search_weibo(const QString& keyword, const QString& search_url,
                               int token, int page, int attempt) {
    QUrl url(QStringLiteral("https://m.weibo.cn/api/container/getIndex"));
    QUrlQuery query;
    const QUrl source(search_url);
    const QUrlQuery source_query(source);
    const QString topic = source.host() == QStringLiteral("s.weibo.com")
        ? source_query.queryItemValue(QStringLiteral("q")) : QString{};
    const QString search_term = topic.isEmpty() ? QStringLiteral("#") + keyword + QStringLiteral("#") : topic;
    query.addQueryItem(QStringLiteral("containerid"), QStringLiteral("100103type=1&q=") + search_term);
    query.addQueryItem(QStringLiteral("page_type"), QStringLiteral("searchall"));
    query.addQueryItem(QStringLiteral("t"), QStringLiteral("31"));
    if (source_query.hasQueryItem(QStringLiteral("band_rank")))
        query.addQueryItem(QStringLiteral("band_rank"), source_query.queryItemValue(QStringLiteral("band_rank")));
    if (source_query.hasQueryItem(QStringLiteral("Refer")))
        query.addQueryItem(QStringLiteral("Refer"), source_query.queryItemValue(QStringLiteral("Refer")));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    url.setQuery(query);
    get(url, QStringLiteral("https://m.weibo.cn/"), QString::fromLatin1(kMobileAgent), 2'000'000,
        [this, keyword, search_url, token, page, attempt](const QByteArray& bytes, const QString& error) {
            const auto root = QJsonDocument::fromJson(bytes).object();
            if (!error.isEmpty() || root.value(QStringLiteral("ok")).toInt() != 1) {
                if (page > 1 && error.isEmpty() && root.value(QStringLiteral("ok")).toInt() == -100) {
                    emit posts_ready(token, page, {}, true);
                    return;
                }
                if (attempt == 0) {
                    visitor_cookie_.clear();
                    ensure_visitor([this, keyword, search_url, token, page](const QByteArray& cookie) {
                        if (!cookie.isEmpty()) search_weibo(keyword, search_url, token, page, 1);
                        else emit posts_failed(token, page, QStringLiteral("微博搜索不可用"));
                    });
                } else emit posts_failed(token, page, error.isEmpty() ? QStringLiteral("微博搜索不可用") : error);
                return;
            }
            auto posts = parse_posts(bytes);
            if (posts.isEmpty() && page == 1) {
                emit posts_failed(token, page, QStringLiteral("微博综合搜索没有返回微博"));
                return;
            }
            emit posts_ready(token, page, posts, false);
            auto pending = std::make_shared<int>(0);
            auto extended_posts = std::make_shared<QJsonArray>(posts);
            for (int i = 0; i < posts.size(); ++i) {
                const auto post = posts[i].toObject();
                if (post.value(QStringLiteral("complete")).toBool()) continue;
                ++*pending;
                QUrl url(QStringLiteral("https://m.weibo.cn/statuses/extend"));
                QUrlQuery query;
                query.addQueryItem(QStringLiteral("id"), post.value(QStringLiteral("url")).toString().section('/', -1));
                url.setQuery(query);
                get(url, QStringLiteral("https://m.weibo.cn/"), QString::fromLatin1(kMobileAgent),
                    2'000'000, [this, token, page, extended_posts, pending, i](const QByteArray& bytes, const QString& error) {
                        if (error.isEmpty()) {
                            const auto body = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("data"))
                                                  .toObject().value(QStringLiteral("longTextContent")).toString();
                            if (!body.isEmpty()) {
                                auto row = (*extended_posts)[i].toObject();
                                row.insert(QStringLiteral("text"), plain_html(body));
                                row.insert(QStringLiteral("links"), post_links(body));
                                row.insert(QStringLiteral("complete"), true);
                                (*extended_posts)[i] = row;
                            }
                        }
                        if (--*pending == 0) emit posts_ready(token, page, *extended_posts, true);
                    }, visitor_cookie_);
            }
            if (*pending == 0) emit posts_ready(token, page, *extended_posts, true);
        }, visitor_cookie_);
}

void NewsService::load_comments(const QString& post_id, int token, int page) {
    if (post_id.isEmpty()) {
        emit comments_failed(token, post_id, page, QStringLiteral("微博编号无效"));
        return;
    }
    QUrl url(QStringLiteral("https://m.weibo.cn/api/comments/show"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), post_id);
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    url.setQuery(query);
    get(url, QStringLiteral("https://m.weibo.cn/"), QString::fromLatin1(kMobileAgent), 1'000'000,
        [this, token, post_id, page](const QByteArray& bytes, const QString& error) {
            if (!error.isEmpty()) { emit comments_failed(token, post_id, page, QStringLiteral("评论加载失败")); return; }
            const auto root = QJsonDocument::fromJson(bytes).object();
            if (root.value(QStringLiteral("ok")).toInt() != 1) {
                emit comments_failed(token, post_id, page,
                    page > 1 ? QStringLiteral("访客接口无法继续加载评论") : QStringLiteral("评论暂不可用"));
                return;
            }
            const auto data = root.value(QStringLiteral("data")).toObject();
            const auto rows = parse_comments(bytes);
            const int total = text(data.value(QStringLiteral("total_number"))).toInt();
            emit comments_ready(token, post_id, page, rows,
                !rows.isEmpty() && (total > 0 ? page * 10 < total : rows.size() == 10));
        }, visitor_cookie_);
}

void NewsService::load_article(const QString& url, int token) {
    if (!url.startsWith(QStringLiteral("https://www.cls.cn/detail/"))) {
        emit article_failed(token, QStringLiteral("文章链接无效"));
        return;
    }
    get(QUrl(url), QStringLiteral("https://www.cls.cn/"), QString::fromLatin1(kDesktopAgent), 5'000'000,
        [this, token](const QByteArray& bytes, const QString& error) {
            if (!error.isEmpty()) { emit article_failed(token, error); return; }
            const QRegularExpression expression(QStringLiteral(
                R"(<script id="__NEXT_DATA__" type="application/json">(.*?)</script>)"),
                QRegularExpression::DotMatchesEverythingOption);
            const auto match = expression.match(QString::fromUtf8(bytes));
            const auto root = QJsonDocument::fromJson(match.captured(1).toUtf8()).object();
            const auto detail = root.value(QStringLiteral("props")).toObject()
                                  .value(QStringLiteral("pageProps")).toObject()
                                  .value(QStringLiteral("articleDetail")).toObject();
            const auto content = detail.value(QStringLiteral("content")).toString();
            if (content.isEmpty()) { emit article_failed(token, QStringLiteral("正文不可用")); return; }
            emit article_ready(token, build_article(content, detail));
        });
}

} // namespace newsx
