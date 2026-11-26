#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace newsx {

struct WatchItem {
    QString symbol;
    bool followed = true;
};

struct Settings {
    QString theme = QStringLiteral("dark");
    double opacity = 0.86;
    bool always_on_top = true;
    bool minimal_mode = false;
    QString window_mode = QStringLiteral("normal");
    QString minimal_card = QStringLiteral("stocks");
    int x = -1;
    int y = -1;
    int width = 332;
    int height = 390;
    QVector<WatchItem> symbols{{QStringLiteral("SH600519"), true},
                               {QStringLiteral("SZ000001"), true}};
    QString tora_front;
    QString hot_url = QStringLiteral("https://weibo.com/ajax/statuses/hot_band");
    int hot_refresh_minutes = 10;
    int headline_refresh_minutes = 10;
};

[[nodiscard]] QString app_data_dir();
[[nodiscard]] Settings load_settings();
bool save_settings(const Settings& settings);

} // namespace newsx
