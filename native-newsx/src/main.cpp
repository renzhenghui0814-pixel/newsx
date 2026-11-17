#include "app_controller.h"

#include <QApplication>
#include <QAction>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QSurfaceFormat>
#include <QTimer>
#include <QStringList>

#include <memory>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/newsx.png")));
    application.setApplicationName(QStringLiteral("NewsX"));
    application.setOrganizationName(QStringLiteral("NewsX"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    newsx::AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.loadFromModule(QStringLiteral("NewsX"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    QMenu tray_menu;
    auto* show_action = tray_menu.addAction(QStringLiteral("显示组件"));
    auto* hide_action = tray_menu.addAction(QStringLiteral("隐藏组件"));
    tray_menu.addSeparator();
    auto* quit_action = tray_menu.addAction(QStringLiteral("退出 NewsX"));
    QObject::connect(show_action, &QAction::triggered, window, [window] {
        window->show();
        window->raise();
        window->requestActivate();
    });
    QObject::connect(hide_action, &QAction::triggered, window, [window] { window->hide(); });
    QObject::connect(quit_action, &QAction::triggered, &application, &QCoreApplication::quit);
    QObject::connect(&tray_menu, &QMenu::aboutToShow, window, [window, show_action, hide_action] {
        show_action->setEnabled(!window->isVisible());
        hide_action->setEnabled(window->isVisible());
    });
    QSystemTrayIcon tray_icon(QIcon(QStringLiteral(":/resources/newsx.png")), &application);
    tray_icon.setToolTip(QStringLiteral("NewsX"));
    tray_icon.setContextMenu(&tray_menu);
    QObject::connect(&tray_icon, &QSystemTrayIcon::activated, window,
                     [window](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick) return;
        if (window->isVisible()) window->hide();
        else {
            window->show();
            window->raise();
            window->requestActivate();
        }
    });
    tray_icon.show();
    QObject::connect(&controller, &newsx::AppController::idleCleanupRequested, &application,
                     [&engine, window] {
        QTimer::singleShot(500, &engine, [&engine, window] {
            if (!window || window->property("page").toString() != QStringLiteral("home")) return;
            engine.collectGarbage();
            engine.trimComponentCache();
            window->releaseResources();
        });
    });
    int screenshot_delay = 1200;
    for (int index = 1; index + 1 < argc; ++index) {
        if (QByteArray(argv[index]) == "--page")
            engine.rootObjects().first()->setProperty("page", QString::fromLocal8Bit(argv[index + 1]));
        if (QByteArray(argv[index]) == "--symbol")
            engine.rootObjects().first()->setProperty("selectedSymbol", QString::fromLocal8Bit(argv[index + 1]));
        if (QByteArray(argv[index]) == "--hot") {
            const auto title = QString::fromLocal8Bit(argv[index + 1]);
            engine.rootObjects().first()->setProperty("selectedTitle", title);
            engine.rootObjects().first()->setProperty("page", QStringLiteral("weiboDetail"));
            controller.openWeibo(title, {});
            screenshot_delay = 7000;
        }
        if (QByteArray(argv[index]) == "--article") {
            const auto url = QString::fromLocal8Bit(argv[index + 1]);
            engine.rootObjects().first()->setProperty("selectedUrl", url);
            engine.rootObjects().first()->setProperty("page", QStringLiteral("article"));
            controller.openArticle(url);
            screenshot_delay = 7000;
        }
    }
    if (argc >= 3 && QByteArray(argv[1]) == "--screenshot") {
        const auto path = QString::fromLocal8Bit(argv[2]);
        QTimer::singleShot(screenshot_delay, &application, [window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()), path] {
            if (window) window->grabWindow().save(path);
            QCoreApplication::quit();
        });
    }
    if (QCoreApplication::arguments().contains(QStringLiteral("--cycle-pages"))) {
        auto* cycle = new QTimer(&application);
        const QStringList pages{QStringLiteral("stocks"), QStringLiteral("home"),
            QStringLiteral("weibo"), QStringLiteral("home"), QStringLiteral("cls"),
            QStringLiteral("home"), QStringLiteral("quote"), QStringLiteral("home"),
            QStringLiteral("weiboDetail"), QStringLiteral("home"),
            QStringLiteral("article"), QStringLiteral("home")};
        auto index = std::make_shared<int>(0);
        QObject::connect(cycle, &QTimer::timeout, &application,
                         [root = engine.rootObjects().first(), &controller, cycle, pages, index] {
            if (*index >= pages.size()) { cycle->stop(); return; }
            const auto& page = pages[*index];
            if (page == QStringLiteral("home")) controller.cancelMedia();
            root->setProperty("page", page);
            if (page == QStringLiteral("weiboDetail")) controller.openWeibo(QStringLiteral("深圳"), {});
            if (page == QStringLiteral("article"))
                controller.openArticle(QStringLiteral("https://www.cls.cn/detail/2194356"));
            cycle->setInterval(page == QStringLiteral("weiboDetail") ? 7000 : 700);
            ++*index;
        });
        cycle->start(700);
    }
    return application.exec();
}
