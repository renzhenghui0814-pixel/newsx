#include "cache_store.h"
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace newsx {
namespace {

QString path() { return QDir(app_data_dir()).filePath(QStringLiteral("cache.json")); }
QString stamp() { return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HH:mm:ss")); }
int priority(const QString& platform) {
    if (platform == QStringLiteral("stocks")) return 0;
    if (platform == QStringLiteral("weibo")) return 1;
    return 2;
}

} // namespace

QJsonArray CacheStore::read_all() const {
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}

QJsonObject CacheStore::load(const QString& platform) const {
    for (const auto& value : read_all()) {
        const auto row = value.toObject();
        if (row.value(QStringLiteral("platform")).toString() == platform &&
            row.value(QStringLiteral("items")).isArray()) return row;
    }
    return {};
}

bool CacheStore::save(const QString& platform, const QJsonArray& items,
                      const QString& trading_day, const QString& source_time,
                      const QString& origin, const QString& saved_at) const {
    if (platform != QStringLiteral("stocks") && platform != QStringLiteral("weibo") &&
        platform != QStringLiteral("cls")) return false;
    QJsonArray result;
    const auto existing = read_all();
    QJsonObject row;
    row.insert(QStringLiteral("platform"), platform);
    row.insert(QStringLiteral("time"), saved_at.isEmpty() ? stamp() : saved_at);
    row.insert(QStringLiteral("items"), items);
    if (platform == QStringLiteral("stocks")) row.insert(QStringLiteral("trading_day"), trading_day);
    if (platform == QStringLiteral("weibo")) {
        if (!source_time.isEmpty()) row.insert(QStringLiteral("source_time"), source_time);
        if (!origin.isEmpty()) row.insert(QStringLiteral("origin"), origin);
    }
    for (int order = 0; order < 3; ++order) {
        if (priority(platform) == order) result.append(row);
        for (const auto& value : existing) {
            const auto old = value.toObject();
            const auto old_platform = old.value(QStringLiteral("platform")).toString();
            if (old_platform != platform && priority(old_platform) == order &&
                (old_platform == QStringLiteral("stocks") || old_platform == QStringLiteral("weibo") ||
                 old_platform == QStringLiteral("cls"))) result.append(old);
        }
    }
    QDir().mkpath(app_data_dir());
    QSaveFile file(path());
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
    return file.commit();
}

} // namespace newsx
