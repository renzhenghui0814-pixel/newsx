#include "weibo_session.h"
#include "settings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QVariant>
#include <QUrlQuery>
#include <windows.h>
#include <dpapi.h>

namespace newsx {
namespace {
QString path() { return QDir(app_data_dir()).filePath(QStringLiteral("weibo-session.dat")); }
bool weibo_host(QString host) {
    host = host.toLower();
    return host == QStringLiteral("weibo.com") || host.endsWith(QStringLiteral(".weibo.com"))
        || host == QStringLiteral("weibo.cn") || host.endsWith(QStringLiteral(".weibo.cn"));
}
void verification_stage(int http, int error, bool login) {
    QFile file(QDir(app_data_dir()).filePath(QStringLiteral("weibo-login.jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    if (file.size() > 500'000) { file.resize(0); file.seek(0); }
    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
        {QStringLiteral("stage"), QStringLiteral("session_verification")},
        {QStringLiteral("http"), http}, {QStringLiteral("network_error"), error},
        {QStringLiteral("login"), login}
    }).toJson(QJsonDocument::Compact));
    file.write("\n");
}
QByteArray header(const QList<QNetworkCookie>& cookies, const QUrl& url) {
    if (url.scheme() != QStringLiteral("https") || !weibo_host(url.host())) return {};
    QByteArray result;
    for (const auto& cookie : cookies) {
        auto domain = cookie.domain().toLower();
        if (domain.startsWith('.')) domain.remove(0, 1);
        if (domain.isEmpty() || (url.host() != domain && !url.host().endsWith('.' + domain))) continue;
        if (!cookie.isSessionCookie() && cookie.expirationDate() <= QDateTime::currentDateTimeUtc()) continue;
        const auto cookie_path = cookie.path().isEmpty() ? QStringLiteral("/") : cookie.path();
        const auto request_path = url.path().isEmpty() ? QStringLiteral("/") : url.path();
        if (!request_path.startsWith(cookie_path)
            || (request_path.size() > cookie_path.size() && !cookie_path.endsWith('/')
                && request_path[cookie_path.size()] != '/')) continue;
        if (!result.isEmpty()) result += "; ";
        result += cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
    }
    return result;
}
QByteArray protect(const QByteArray& bytes, bool encrypt) {
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
    DATA_BLOB output{};
    const auto ok = encrypt
        ? CryptProtectData(&input, L"NewsX Weibo session", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
}

WeiboSession::WeiboSession(QObject* parent, bool restore) : QObject(parent) {
    if (!restore) return;
    QTimer::singleShot(0, this, [this] {
        QFile file(path());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1'000'000) return;
        const auto plain = protect(file.readAll(), false);
        QList<QNetworkCookie> restored;
        const auto document = QJsonDocument::fromJson(plain);
        const auto rows = document.isArray() ? document.array()
            : document.object().value(QStringLiteral("cookies")).toArray();
        const auto agent = document.object().value(QStringLiteral("user_agent")).toString();
        user_name_ = document.object().value(QStringLiteral("user_name")).toString();
        user_id_ = document.object().value(QStringLiteral("user_id")).toString();
        emit userNameChanged();
        for (const auto& item : rows)
            restored.append(QNetworkCookie::parseCookies(QByteArray::fromBase64(item.toString().toLatin1())));
        if (!restored.isEmpty()) submit(restored, agent);
    });
}

QByteArray WeiboSession::cookie_header(const QUrl& url) const {
    return logged_in_ ? header(cookies_, url) : QByteArray{};
}

void WeiboSession::set_status(const QString& value) {
    if (status_ == value) return;
    status_ = value;
    emit statusChanged();
}

bool WeiboSession::save() const {
    QJsonArray rows;
    for (const auto& cookie : cookies_)
        rows.append(QString::fromLatin1(cookie.toRawForm(QNetworkCookie::Full).toBase64()));
    const auto encrypted = protect(QJsonDocument(QJsonObject{
        {QStringLiteral("cookies"), rows}, {QStringLiteral("user_agent"), user_agent_},
        {QStringLiteral("user_name"), user_name_}, {QStringLiteral("user_id"), user_id_}
    }).toJson(QJsonDocument::Compact), true);
    if (encrypted.isEmpty()) return false;
    QDir().mkpath(app_data_dir());
    QSaveFile file(path());
    return file.open(QIODevice::WriteOnly) && file.write(encrypted) == encrypted.size() && file.commit();
}

void WeiboSession::submit(const QList<QNetworkCookie>& candidates, const QString& user_agent) {
    if (verifying_) return;
    const QUrl url(QStringLiteral("https://m.weibo.cn/api/config"));
    const auto cookie = header(candidates, url);
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (cookie.isEmpty() || (cookie == last_attempt_ && now - last_attempt_ms_ < (logged_in_ ? 30000 : 5000))) return;
    last_attempt_ = cookie;
    last_attempt_ms_ = now;
    verifying_ = true;
    const int generation = generation_;
    if (!logged_in_) set_status(QStringLiteral("正在确认微博登录状态…"));
    QNetworkRequest request(url);
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Cookie", cookie);
    const auto agent = user_agent.isEmpty() ? QStringLiteral("Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36") : user_agent;
    request.setRawHeader("User-Agent", agent.toUtf8());
    request.setRawHeader("Referer", "https://m.weibo.cn/");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("X-Requested-With", "XMLHttpRequest");
    request.setRawHeader("MWeibo-Pwa", "1");
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, candidates, generation, agent] {
        const auto data = QJsonDocument::fromJson(reply->read(200'000)).object().value(QStringLiteral("data")).toObject();
        const bool network_ok = reply->error() == QNetworkReply::NoError;
        // The explicit login flag is authoritative; uid is not required by all
        // versions of this config response. Never treat visitor SUB as login.
        const auto login = data.value(QStringLiteral("login"));
        const bool valid = network_ok && (login.toBool() || login.toInt() == 1);
        verification_stage(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->error(), valid);
        reply->deleteLater();
        if (generation != generation_) return;
        verifying_ = false;
        if (!valid) {
            if (!network_ok) last_attempt_.clear();
            if (logged_in_ && network_ok && (login.isBool() || login.isDouble())) {
                logout(QStringLiteral("微博登录已失效 · 已切换匿名会话"));
                return;
            }
            if (!logged_in_) set_status(QStringLiteral("请在下方微博官方页面完成登录"));
            return;
        }
        const bool was_logged_in = logged_in_;
        const bool cookies_changed = cookies_ != candidates || user_agent_ != agent;
        cookies_ = candidates;
        user_agent_ = agent;
        logged_in_ = true;
        const auto uid = data.value(QStringLiteral("uid")).toVariant().toString();
        if (!uid.isEmpty() && uid != user_id_) {
            user_id_ = uid;
            user_name_.clear();
            emit userNameChanged();
        }
        const auto direct_name = data.value(QStringLiteral("user")).toObject().value(QStringLiteral("screen_name")).toString();
        if (!direct_name.isEmpty() && direct_name != user_name_) {
            user_name_ = direct_name;
            emit userNameChanged();
        }
        const bool saved = (!cookies_changed && was_logged_in) || save();
        set_status(saved ? QStringLiteral("已登录微博") : QStringLiteral("已登录微博 · 本次会话有效，保存失败"));
        if (!was_logged_in) emit changed();
        if (user_name_.isEmpty() && !uid.isEmpty()) fetch_user_name(uid);
    });
}

void WeiboSession::fetch_user_name(const QString& uid, int attempt) {
    const int generation = generation_;
    QUrl url(attempt == 0 ? QStringLiteral("https://m.weibo.cn/api/container/getIndex")
                         : QStringLiteral("https://weibo.com/ajax/profile/info"));
    QUrlQuery query;
    if (attempt == 0) { query.addQueryItem(QStringLiteral("type"), QStringLiteral("uid")); query.addQueryItem(QStringLiteral("value"), uid); }
    else query.addQueryItem(QStringLiteral("uid"), uid);
    url.setQuery(query);
    const auto cookie = cookie_header(url);
    if (cookie.isEmpty()) { if (attempt == 0) fetch_user_name(uid, 1); return; }
    QNetworkRequest request(url);
    request.setTransferTimeout(12000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Cookie", cookie);
    request.setRawHeader("User-Agent", user_agent_.toUtf8());
    request.setRawHeader("Referer", attempt == 0 ? "https://m.weibo.cn/" : "https://weibo.com/");
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, uid, attempt, generation] {
        const auto data = QJsonDocument::fromJson(reply->read(500'000)).object().value(QStringLiteral("data")).toObject();
        auto user = data.value(QStringLiteral("userInfo")).toObject();
        if (user.isEmpty()) user = data.value(QStringLiteral("user")).toObject();
        const auto name = user.value(QStringLiteral("screen_name")).toString();
        const auto id = user.value(QStringLiteral("id")).toVariant().toString();
        const bool valid = reply->error() == QNetworkReply::NoError && !name.isEmpty() && id == uid;
        reply->deleteLater();
        if (generation != generation_ || !logged_in_ || user_id_ != uid) return;
        if (valid) {
            user_name_ = name;
            save();
            emit userNameChanged();
        } else if (attempt == 0) fetch_user_name(uid, 1);
    });
}

void WeiboSession::logout(const QString& message) {
    ++generation_;
    verifying_ = false;
    cookies_.clear();
    last_attempt_.clear();
    last_attempt_ms_ = 0;
    user_agent_.clear();
    user_name_.clear(); user_id_.clear();
    emit userNameChanged();
    QFile::remove(path());
    const bool was_logged_in = logged_in_;
    logged_in_ = false;
    set_status(message.isEmpty() ? QStringLiteral("未登录 · 使用匿名会话") : message);
    if (was_logged_in) emit changed();
}
}
