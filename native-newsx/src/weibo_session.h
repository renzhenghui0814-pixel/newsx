#pragma once
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QObject>

namespace newsx {
class WeiboSession final : public QObject {
    Q_OBJECT
public:
    explicit WeiboSession(QObject* parent = nullptr, bool restore = true);
    bool logged_in() const { return logged_in_; }
    QString status() const { return status_; }
    QByteArray cookie_header(const QUrl& url) const;
    void submit(const QList<QNetworkCookie>& cookies, const QString& user_agent = {});
    QString user_agent() const { return user_agent_; }
    QString user_name() const { return user_name_; }
    QList<QNetworkCookie> browser_cookies() const { return logged_in_ ? cookies_ : QList<QNetworkCookie>{}; }
    void logout(const QString& message = {});
signals:
    void changed();
    void statusChanged();
    void userNameChanged();
private:
    void set_status(const QString& status);
    bool save() const;
    void fetch_user_name(const QString& uid, int attempt = 0);
    QList<QNetworkCookie> cookies_;
    QNetworkAccessManager network_;
    bool logged_in_ = false;
    bool verifying_ = false;
    QByteArray last_attempt_;
    qint64 last_attempt_ms_ = 0;
    QString user_agent_;
    QString user_name_;
    QString user_id_;
    int generation_ = 0;
    QString status_ = QStringLiteral("未登录 · 使用匿名会话");
};
}
