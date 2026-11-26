#pragma once

#include "settings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace newsx {

// Only pagination metadata is recorded: never cookies or response bodies.
inline void log_weibo_page(const QString& event, QJsonObject fields) {
    QDir().mkpath(app_data_dir());
    QFile file(QDir(app_data_dir()).filePath(QStringLiteral("weibo-pagination.jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    if (file.size() > 2'000'000) { file.resize(0); file.seek(0); }
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    file.write(QJsonDocument(fields).toJson(QJsonDocument::Compact));
    file.write("\n");
}

} // namespace newsx
