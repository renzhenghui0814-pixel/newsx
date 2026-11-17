#include "settings.h"

#include "market/symbol.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

namespace newsx {

QString app_data_dir() {
#ifdef Q_OS_WIN
    // The Python app stores both config.json and cache.json under %APPDATA%.
    // Qt's GenericConfigLocation resolves to LocalAppData on Windows.
    const auto appdata = qEnvironmentVariable("APPDATA");
    if (!appdata.isEmpty()) return QDir(appdata).filePath(QStringLiteral("NewsXWidget"));
#endif
    const auto roaming = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(roaming).filePath(QStringLiteral("NewsXWidget"));
}

Settings load_settings() {
    Settings settings;
    QFile file(QDir(app_data_dir()).filePath(QStringLiteral("config.json")));
    if (!file.open(QIODevice::ReadOnly)) return settings;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const auto theme = object.value(QStringLiteral("theme")).toString();
    if (theme == QStringLiteral("dark") || theme == QStringLiteral("light")) settings.theme = theme;
    settings.opacity = std::clamp(object.value(QStringLiteral("opacity")).toDouble(0.86), 0.45, 1.0);
    settings.always_on_top = object.value(QStringLiteral("always_on_top")).toBool(true);
    settings.minimal_mode = object.value(QStringLiteral("minimal_mode")).toBool(false);
    const auto minimal_card = object.value(QStringLiteral("minimal_card")).toString();
    if (minimal_card == QStringLiteral("stocks") || minimal_card == QStringLiteral("weibo") ||
        minimal_card == QStringLiteral("cls")) settings.minimal_card = minimal_card;
    settings.width = std::clamp(object.value(QStringLiteral("width")).toInt(332), 332, 900);
    settings.height = std::clamp(object.value(QStringLiteral("height")).toInt(390), 390, 1040);
    settings.x = object.value(QStringLiteral("x")).toInt(-1);
    settings.y = object.value(QStringLiteral("y")).toInt(-1);
    settings.hot_refresh_minutes = std::clamp(object.value(QStringLiteral("hot_refresh_minutes")).toInt(10), 1, 1440);
    settings.headline_refresh_minutes = std::clamp(object.value(QStringLiteral("headline_refresh_minutes")).toInt(10), 1, 1440);
    settings.tora_front = object.value(QStringLiteral("tora_front")).toString();
    const auto hot_url = object.value(QStringLiteral("hot_url")).toString();
    if (!hot_url.isEmpty() && !hot_url.contains(QStringLiteral("solarhell.github.io")) &&
        !hot_url.contains(QStringLiteral("s.weibo.com/top/summary"))) settings.hot_url = hot_url;

    const auto rows = object.value(QStringLiteral("symbols")).toArray();
    if (object.contains(QStringLiteral("symbols"))) {
        settings.symbols.clear();
        QSet<QString> seen;
        QSet<QString> old_followed;
        for (const auto& item : object.value(QStringLiteral("followed_symbols")).toArray())
            old_followed.insert(item.toString());
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const auto raw = value.isString() ? value.toString() : row.value(QStringLiteral("symbol")).toString();
            const auto parsed = market::parse_symbol(raw.toStdString());
            if (!parsed) continue;
            const auto symbol = QString::fromStdString(parsed->full());
            if (seen.contains(symbol)) continue;
            seen.insert(symbol);
            const bool followed = value.isString()
                ? (!object.contains(QStringLiteral("followed_symbols")) || old_followed.contains(raw))
                : row.value(QStringLiteral("followed")).toBool(true);
            settings.symbols.push_back({symbol, followed});
        }
    }
    return settings;
}

bool save_settings(const Settings& settings) {
    QDir().mkpath(app_data_dir());
    QSaveFile file(QDir(app_data_dir()).filePath(QStringLiteral("config.json")));
    if (!file.open(QIODevice::WriteOnly)) return false;
    QJsonObject object;
    object.insert(QStringLiteral("theme"), settings.theme);
    object.insert(QStringLiteral("opacity"), settings.opacity);
    object.insert(QStringLiteral("always_on_top"), settings.always_on_top);
    object.insert(QStringLiteral("minimal_mode"), settings.minimal_mode);
    object.insert(QStringLiteral("minimal_card"), settings.minimal_card);
    object.insert(QStringLiteral("x"), settings.x);
    object.insert(QStringLiteral("y"), settings.y);
    object.insert(QStringLiteral("width"), settings.width);
    object.insert(QStringLiteral("height"), settings.height);
    object.insert(QStringLiteral("hot_refresh_minutes"), settings.hot_refresh_minutes);
    object.insert(QStringLiteral("headline_refresh_minutes"), settings.headline_refresh_minutes);
    object.insert(QStringLiteral("tora_front"), settings.tora_front);
    object.insert(QStringLiteral("hot_url"), settings.hot_url);
    QJsonArray rows;
    for (const auto& item : settings.symbols) {
        QJsonObject row;
        row.insert(QStringLiteral("symbol"), item.symbol);
        row.insert(QStringLiteral("followed"), item.followed);
        rows.append(row);
    }
    object.insert(QStringLiteral("symbols"), rows);
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace newsx
