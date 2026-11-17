#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include <functional>

namespace newsx {

class NewsService final : public QObject {
    Q_OBJECT
public:
    explicit NewsService(QObject* parent = nullptr);
    ~NewsService() override;

    void refresh_hot(const QString& custom_url = {});
    void refresh_headlines();
    void load_weibo_posts(const QString& keyword, const QString& search_url, int token);
    void load_more_weibo_posts(const QString& keyword, const QString& search_url, int token, int page);
    void load_comments(const QString& post_id, int token, int page);
    void load_article(const QString& url, int token);

signals:
    void hot_ready(QJsonArray items, QString origin);
    void hot_failed(QString message);
    void headlines_ready(QJsonArray items);
    void headlines_failed(QString message);
    void posts_ready(int token, int page, QJsonArray items, bool final);
    void posts_failed(int token, int page, QString message);
    void comments_ready(int token, QString post_id, int page, QJsonArray items, bool has_more);
    void comments_failed(int token, QString post_id, int page, QString message);
    void article_ready(int token, QJsonObject article);
    void article_failed(int token, QString message);

private:
    using ReplyHandler = std::function<void(QByteArray, QString)>;
    void get(const QUrl& url, const QString& referer, const QString& agent,
             qint64 max_bytes, ReplyHandler handler, const QByteArray& cookie = {});
    void hot_snapshot(const QString& custom_url = {});
    void hot_page(int attempt);
    void ensure_visitor(std::function<void(QByteArray)> ready);
    void search_weibo(const QString& keyword, const QString& search_url,
                      int token, int page, int attempt);

    QNetworkAccessManager network_;
    QByteArray visitor_cookie_;
};

} // namespace newsx
