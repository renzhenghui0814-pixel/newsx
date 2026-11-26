#include "desktop_weibo.h"
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <QUrl>
#include <QVector>

namespace newsx {
namespace {
QString plain(const QString& html) {
    QTextDocument document;
    document.setHtml(html);
    auto result = document.toPlainText();
    result.remove(QChar(0x200b));
    return result.trimmed();
}
QString decoded(QString value) {
    value.replace('<', QStringLiteral("&lt;"));
    value.replace('>', QStringLiteral("&gt;"));
    return plain(value);
}
QString url(QString raw) {
    if (raw.trimmed().isEmpty()) return {};
    raw = decoded(raw).replace(QStringLiteral("\\/"), QStringLiteral("/"));
    const auto resolved = QUrl(QStringLiteral("https://s.weibo.com/")).resolved(QUrl(raw));
    return resolved.scheme() == QStringLiteral("https") || resolved.scheme() == QStringLiteral("http")
        ? resolved.toString() : QString{};
}
struct Node {
    QString tag;
    QMap<QString, QString> attrs;
    int parent = -1;
    qsizetype start = 0, content = 0, close = 0;
};
struct Html {
    QString source;
    QVector<Node> nodes;
    explicit Html(const QByteArray& bytes) : source(QString::fromUtf8(bytes)) {
        nodes.append(Node{QStringLiteral("root"), {}, -1, 0, 0, source.size()});
        QVector<int> stack{0};
        const QRegularExpression tags(QStringLiteral(R"(<!--[\s\S]*?-->|<![^>]*>|<\s*(/?)\s*([A-Za-z][\w:-]*)((?:"[^"]*"|'[^']*'|[^'">])*)>)"));
        const QRegularExpression attrs(QStringLiteral(R"rx(([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))rx"));
        const QSet<QString> voids{QStringLiteral("img"), QStringLiteral("br"), QStringLiteral("hr"),
            QStringLiteral("input"), QStringLiteral("meta"), QStringLiteral("link"), QStringLiteral("source"),
            QStringLiteral("area"), QStringLiteral("base"), QStringLiteral("col"), QStringLiteral("embed"),
            QStringLiteral("param"), QStringLiteral("track"), QStringLiteral("wbr")};
        auto matches = tags.globalMatch(source);
        qsizetype skip = 0;
        while (matches.hasNext() && nodes.size() < 200'000) {
            const auto match = matches.next();
            if (match.capturedStart() < skip || match.captured(2).isEmpty()) continue;
            const auto tag = match.captured(2).toLower();
            if (!match.captured(1).isEmpty()) {
                for (qsizetype i = stack.size() - 1; i > 0; --i) {
                    if (nodes[stack[i]].tag != tag) continue;
                    while (stack.size() > i) nodes[stack.takeLast()].close = match.capturedStart();
                    break;
                }
                continue;
            }
            if (tag == QStringLiteral("script") || tag == QStringLiteral("style")) {
                const auto end = source.indexOf(QStringLiteral("</") + tag, match.capturedEnd(), Qt::CaseInsensitive);
                skip = end < 0 ? source.size() : source.indexOf('>', end) + 1;
                continue;
            }
            Node node{tag, {}, stack.last(), match.capturedStart(), match.capturedEnd(), source.size()};
            auto values = attrs.globalMatch(match.captured(3));
            while (values.hasNext()) {
                const auto value = values.next();
                node.attrs.insert(value.captured(1).toLower(), value.captured(2).isNull()
                    ? (value.captured(3).isNull() ? value.captured(4) : value.captured(3)) : value.captured(2));
            }
            nodes.append(node);
            if (!voids.contains(tag) && !match.captured(3).trimmed().endsWith('/')) stack.append(nodes.size() - 1);
        }
    }
    bool cls(int i, const QString& name) const {
        return nodes[i].attrs.value(QStringLiteral("class")).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(name);
    }
    int find(int root, const QString& key, const QString& value, bool last = false) const {
        if (root < 0) return -1;
        int result = -1;
        for (int i = root + 1; i < nodes.size() && nodes[i].start < nodes[root].close; ++i) {
            if ((key == QStringLiteral("class") ? cls(i, value) : nodes[i].attrs.value(key) == value)) {
                result = i;
                if (!last) break;
            }
        }
        return result;
    }
    int tag(int root, const QString& name) const {
        if (root < 0) return -1;
        for (int i = root + 1; i < nodes.size() && nodes[i].start < nodes[root].close; ++i)
            if (nodes[i].tag == name) return i;
        return -1;
    }
    QString inner(int i) const {
        return i < 0 ? QString{} : source.mid(nodes[i].content, nodes[i].close - nodes[i].content);
    }
    QString attr(int i, const QString& key) const {
        return i < 0 ? QString{} : decoded(nodes[i].attrs.value(key));
    }
    int body(int root) const {
        const auto short_text = find(root, QStringLiteral("node-type"), QStringLiteral("feed_list_content"));
        if (short_text < 0) return -1;
        for (int i = short_text + 1; i < nodes.size() && nodes[i].start < nodes[root].close; ++i)
            if (nodes[i].parent == nodes[short_text].parent
                && nodes[i].attrs.value(QStringLiteral("node-type")) == QStringLiteral("feed_list_content_full")) return i;
        return short_text;
    }
};
QString body_text(QString html) {
    html.remove(QRegularExpression(QStringLiteral(R"(<a\b[^>]*action-type=["']fl_(?:fold|unfold)["'][^>]*>[\s\S]*?</a>)")));
    html.remove(QRegularExpression(QStringLiteral(R"(<i\b[^>]*class=["'][^"']*wbicon[^"']*["'][^>]*>[\s\S]*?</i>)")));
    // Emoji are meaningful text; retain their alt label instead of a Qt image placeholder.
    const QRegularExpression images(QStringLiteral(R"(<img\b[^>]*\balt=["']([^"']*)["'][^>]*>)"));
    html.replace(images, QStringLiteral("\\1"));
    return plain(html);
}
void links(const Html& html, int body, QJsonArray& result) {
    if (body < 0) return;
    for (int i = body + 1; i < html.nodes.size() && html.nodes[i].start < html.nodes[body].close; ++i) {
        const auto& node = html.nodes[i];
        if (node.tag != QStringLiteral("a") || node.attrs.value(QStringLiteral("action-type")).startsWith(QStringLiteral("fl_"))) continue;
        const auto href = url(node.attrs.value(QStringLiteral("href")));
        const auto label = body_text(html.inner(i));
        if (!href.isEmpty() && !label.isEmpty())
            result.append(QJsonObject{{QStringLiteral("label"), label}, {QStringLiteral("url"), href}});
    }
}
QJsonValue count(const Html& html, int card, const QString& action) {
    const auto node = html.find(card, QStringLiteral("action-type"), action, true);
    const auto match = QRegularExpression(QStringLiteral(R"((\d+(?:\.\d+)?)\s*(万|亿)?)")).match(plain(html.inner(node)));
    if (!match.hasMatch()) return 0;
    const auto scale = match.captured(2) == QStringLiteral("亿") ? 100'000'000
        : match.captured(2) == QStringLiteral("万") ? 10'000 : 1;
    return match.captured(1).toDouble() * scale;
}
}

DesktopWeiboPage parse_desktop_weibo(const QByteArray& bytes) {
    const Html html(bytes);
    DesktopWeiboPage page;
    page.login_required = html.source.contains(QStringLiteral("<title>登录"))
        || html.source.contains(QStringLiteral("<title>Sina Visitor System"));
    QSet<QString> seen;
    for (int card = 1; card < html.nodes.size(); ++card) {
        const auto& node = html.nodes[card];
        const auto id = html.attr(card, QStringLiteral("mid"));
        if (id.isEmpty() || !html.cls(card, QStringLiteral("card-wrap"))) continue;
        page.recognized = true;
        if (seen.contains(id)) continue;
        const auto body = html.body(card);
        if (body < 0) continue;
        seen.insert(id);
        auto text = body_text(html.inner(body));
        auto author = html.attr(body, QStringLiteral("nick-name"));
        if (author.isEmpty()) author = plain(html.inner(html.find(card, QStringLiteral("class"), QStringLiteral("name"))));
        QJsonArray post_links;
        links(html, body, post_links);
        const auto quoted = html.find(card, QStringLiteral("class"), QStringLiteral("card-comment"));
        const auto quote_body = html.body(quoted);
        if (quote_body >= 0 && quote_body != body) {
            text += QStringLiteral("\n\n//@") + html.attr(quote_body, QStringLiteral("nick-name"))
                + QStringLiteral("：") + body_text(html.inner(quote_body));
            links(html, quote_body, post_links);
        }
        const auto avatar = html.tag(html.find(card, QStringLiteral("class"), QStringLiteral("avator")), QStringLiteral("img"));
        const auto from = html.find(card, QStringLiteral("class"), QStringLiteral("from"));
        const auto time_link = html.tag(from, QStringLiteral("a"));
        auto permalink = url(html.attr(time_link, QStringLiteral("href")));
        if (permalink.isEmpty()) permalink = QStringLiteral("https://weibo.com/detail/") + id;
        QUrl clean(permalink);
        clean.setQuery(QString{});
        QJsonArray pictures;
        QSet<QString> picture_urls;
        QString video, poster;
        for (int i = card + 1; i < html.nodes.size() && html.nodes[i].start < node.close; ++i) {
            const auto& media = html.nodes[i];
            if (media.attrs.value(QStringLiteral("action-type")) == QStringLiteral("fl_pics")) {
                const auto image = html.tag(i, QStringLiteral("img"));
                const auto src = html.attr(image, QStringLiteral("src"));
                const auto image_url = src.isEmpty() ? url(html.attr(image, QStringLiteral("data-src"))) : url(src);
                if (!image_url.isEmpty() && !picture_urls.contains(image_url)) {
                    picture_urls.insert(image_url);
                    pictures.append(image_url);
                }
            }
            if (video.isEmpty() && html.cls(i, QStringLiteral("WB_video_h5"))) {
                const auto data = html.attr(i, QStringLiteral("data-str"));
                const auto source = QRegularExpression(QStringLiteral(R"(\bsrc\s*:\s*['"]([^'"]+)['"])")).match(data);
                const auto cover = QRegularExpression(QStringLiteral(R"(\bposter\s*:\s*['"]([^'"]+)['"])")).match(data);
                video = source.hasMatch() ? url(source.captured(1)) : QString{};
                poster = cover.hasMatch() ? url(cover.captured(1)) : QString{};
            }
        }
        page.posts.append(QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("title"), author},
            {QStringLiteral("text"), text}, {QStringLiteral("url"), clean.toString()},
            {QStringLiteral("avatar"), url(html.attr(avatar, QStringLiteral("src")))},
            {QStringLiteral("published_at"), plain(html.inner(time_link))},
            {QStringLiteral("images"), pictures}, {QStringLiteral("links"), post_links},
            {QStringLiteral("video_url"), video}, {QStringLiteral("video_poster"), poster},
            {QStringLiteral("reposts_count"), count(html, card, QStringLiteral("feed_list_forward"))},
            {QStringLiteral("comments_count"), count(html, card, QStringLiteral("feed_list_comment"))},
            {QStringLiteral("attitudes_count"), count(html, card, QStringLiteral("feed_list_like"))},
            {QStringLiteral("complete"), html.attr(body, QStringLiteral("node-type")) == QStringLiteral("feed_list_content_full")
                || !html.inner(body).contains(QStringLiteral("fl_unfold"))},
            {QStringLiteral("source"), QStringLiteral("电脑端官网")}
        });
    }
    for (int i = 1; i < html.nodes.size(); ++i) {
        if (html.nodes[i].tag == QStringLiteral("a")
            && (html.cls(i, QStringLiteral("next")) || plain(html.inner(i)) == QStringLiteral("下一页"))
            && !html.attr(i, QStringLiteral("href")).isEmpty()
            && !html.cls(i, QStringLiteral("disabled"))) page.has_more = true;
    }
    if (html.source.contains(QStringLiteral("card-no-result")) || html.source.contains(QStringLiteral("抱歉，未找到"))) page.recognized = true;
    return page;
}
}
