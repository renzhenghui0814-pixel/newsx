// Live, anonymous service probe; no account cookies and no application window.
#include "news_service.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    newsx::NewsService service(nullptr, false);
    const QString keyword = app.arguments().value(1, QStringLiteral("深圳"));
    QSet<QString> seen;
    int requests = 0;
    QObject::connect(&service, &newsx::NewsService::posts_failed, &app,
        [&](int, int page, const QString& message) {
            const auto out = QJsonDocument(QJsonObject{{"page", page}, {"error", message}}).toJson(QJsonDocument::Compact);
            std::fprintf(stderr, "%s\n", out.constData());
            app.exit(2);
        });
    QObject::connect(&service, &newsx::NewsService::posts_ready, &app,
        [&](int, int page, const QJsonArray& rows, bool final, bool has_more, const QString& status) {
            if (!final) return;
            int supplements = 0;
            for (const auto& value : rows) {
                const auto row = value.toObject();
                seen.insert(row.value("id").toString());
                if (row.contains("search_source")) ++supplements;
            }
            const auto out = QJsonDocument(QJsonObject{{"keyword", keyword}, {"page", page},
                {"received", rows.size()}, {"unique_total", seen.size()}, {"has_more", has_more},
                {"realtime_rows", supplements}, {"status", status}}).toJson(QJsonDocument::Compact);
            std::printf("%s\n", out.constData());
            std::fflush(stdout);
            if (!has_more) { app.exit(seen.size() > 15 ? 0 : 3); return; }
            if (++requests >= 4) { app.exit(4); return; }
            QTimer::singleShot(1000, &service, [&, page]() {
                service.load_more_weibo_posts(keyword, {}, 1, page + 1);
            });
        });
    QTimer::singleShot(90000, &app, [&]() { app.exit(5); });
    service.load_weibo_posts(keyword, {}, 1);
    return app.exec();
}
