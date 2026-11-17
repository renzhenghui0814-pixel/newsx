#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace newsx {

class CacheStore {
public:
    [[nodiscard]] QJsonObject load(const QString& platform) const;
    bool save(const QString& platform, const QJsonArray& items,
              const QString& trading_day = {}, const QString& source_time = {},
              const QString& origin = {}, const QString& saved_at = {}) const;

private:
    [[nodiscard]] QJsonArray read_all() const;
};

} // namespace newsx
