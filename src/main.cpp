#include "core/IconImageProvider.h"
#include "core/WheelController.h"
#ifdef Q_OS_WIN
#include "platform/windows/WindowsBackend.h"
#else
#include "platform/linux/LinuxBackend.h"
#endif

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QLockFile>
#include <QTextStream>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <memory>

int main(int argc, char **argv)
{
    QQuickStyle::setStyle(QStringLiteral("Material"));
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arcade Wheel"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("arcadewheel.org"));
    QCoreApplication::setApplicationName(QStringLiteral("Arcade Wheel"));
    app.setQuitOnLastWindowClosed(false);
    app.setDesktopFileName(QStringLiteral("com.arcadewheel.ArcadeWheel"));

    const QString command = app.arguments().size() > 1 ? app.arguments().at(1) : QStringLiteral("--settings");
    if (command == QStringLiteral("--help") || command == QStringLiteral("-h")) {
        QTextStream(stdout) << "Arcade Wheel\n\n"
            "  arcade-wheel             Open Settings; keep the launcher running\n"
            "  arcade-wheel --show      Open the wheel; click an action or right-click to cancel\n"
            "  arcade-wheel --background Start the resident launcher\n"
            "  arcade-wheel --preview   Show a safe visual preview\n"
            "  arcade-wheel --cancel    Dismiss the wheel\n"
            "  arcade-wheel --status    Show resident status\n"
            "  arcade-wheel --quit      Quit the resident launcher\n\n"
            "Once running, hold F8, move toward an action, then release.\n";
        return 0;
    }
    QByteArray identity = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toUtf8()
        + qgetenv("ARCADE_WHEEL_INSTANCE") + qgetenv("WAYLAND_DISPLAY");
    // Test/offscreen instances must never intercept commands for the desktop.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen") ||
        QGuiApplication::platformName() == QStringLiteral("minimal")) identity += "-headless";
    const auto hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(12);
    const QString serverName = QStringLiteral("arcade-wheel-%1").arg(QString::fromLatin1(hash));
    if (command == QStringLiteral("--restarting")) {
        for (int attempt = 0; attempt < 60; ++attempt) {
            QLocalSocket probe;
            probe.connectToServer(serverName);
            if (!probe.waitForConnected(30)) break;
            probe.disconnectFromServer();
            QThread::msleep(50);
        }
    } else {
        QLocalSocket existing;
        existing.connectToServer(serverName);
        if (existing.waitForConnected(150)) {
            existing.write(command.toUtf8() + '\n');
            existing.waitForBytesWritten(300);
            if (existing.waitForReadyRead(1000)) {
                const QByteArray reply = existing.readAll();
                if (command == QStringLiteral("--status")) QTextStream(stdout) << reply;
            }
            return 0;
        }
        if (command == QStringLiteral("--release") || command == QStringLiteral("--cancel")
            || command == QStringLiteral("--quit")) return 0;
        if (command == QStringLiteral("--status")) {
            QTextStream(stdout) << "Arcade Wheel is not running\n";
            return 1;
        }
    }

    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QLockFile instanceLock(QDir(runtime).filePath(serverName + QStringLiteral(".lock")));
    if (!instanceLock.tryLock(500)) {
        QTextStream(stderr) << "Arcade Wheel is already starting. Try again in a moment.\n";
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(serverName); // a dead process may leave its socket behind
    if (!server.listen(serverName)) return 1;

#ifdef Q_OS_WIN
    WindowsBackend backend;
#else
    LinuxBackend backend;
#endif
    WheelController controller(&backend);
    controller.initialize();
    QIcon appIcon(QStringLiteral(":/assets/arcade-wheel.png"));
    if (appIcon.isNull()) appIcon = QIcon::fromTheme(QStringLiteral("applications-system"));
    app.setWindowIcon(appIcon);

    QQuickView overlay;
    overlay.setColor(Qt::transparent);
    overlay.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                     Qt::WindowDoesNotAcceptFocus);
    overlay.setResizeMode(QQuickView::SizeRootObjectToView);
    overlay.engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
    overlay.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    overlay.setSource(QUrl(QStringLiteral("qrc:/qml/wheel/Overlay.qml")));
    if (overlay.rootObject())
        overlay.rootObject()->setProperty("controller", QVariant::fromValue(static_cast<QObject *>(&controller)));
    controller.setOverlayView(&overlay);

    std::unique_ptr<QQuickView> settings;
    const auto showSettings = [&] {
        if (!settings) {
            settings = std::make_unique<QQuickView>();
            settings->setTitle(QStringLiteral("Arcade Wheel · Settings"));
            settings->setColor(QColor(QStringLiteral("#0d1118")));
            settings->setMinimumSize(QSize(960, 640));
            settings->resize(1240, 820);
            settings->setResizeMode(QQuickView::SizeRootObjectToView);
            settings->engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
            settings->rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
            settings->setSource(QUrl(QStringLiteral("qrc:/qml/settings/Settings.qml")));
        }
        settings->show();
        settings->raise();
        settings->requestActivate();
    };

    QSystemTrayIcon tray;
    tray.setToolTip(QStringLiteral("Arcade Wheel"));
    tray.setIcon(appIcon);
    QMenu menu;
    auto *wheelAction = menu.addAction(QStringLiteral("Open Wheel"));
    QObject::connect(wheelAction, &QAction::triggered, &controller, &WheelController::showWheel);
    auto *openAction = menu.addAction(QStringLiteral("Open Settings"));
    auto *restartAction = menu.addAction(QStringLiteral("Restart Arcade Wheel"));
    menu.addSeparator();
    auto *quitAction = menu.addAction(QStringLiteral("Quit Arcade Wheel"));
    QObject::connect(openAction, &QAction::triggered, &app, showSettings);
    QObject::connect(quitAction, &QAction::triggered, &app, &QCoreApplication::quit);
    QObject::connect(&controller, &WheelController::settingsRequested, &app, showSettings);
    QObject::connect(&controller, &WheelController::quitRequested, &app, &QCoreApplication::quit);
    const auto restart = [&] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--restarting")});
        app.quit();
    };
    QObject::connect(restartAction, &QAction::triggered, &app, restart);
    QObject::connect(&controller, &WheelController::restartRequested, &app, restart);
    QObject::connect(&controller, &WheelController::actionFailed, &tray, [&](const QString &message) {
        if (tray.isVisible() && controller.config().value(QStringLiteral("general")).toMap()
                                   .value(QStringLiteral("showNotifications"), true).toBool())
            tray.showMessage(QStringLiteral("Arcade Wheel"), message, QSystemTrayIcon::Warning, 3000);
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray.setContextMenu(&menu);
        tray.show();
        QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showSettings();
        });
    }

    const auto handleCommand = [&](const QString &raw) {
        const QString cmd = raw.trimmed();
        if (cmd == QStringLiteral("--show")) controller.showWheel();
        else if (cmd == QStringLiteral("--press")) controller.pressTrigger();
        else if (cmd == QStringLiteral("--release")) controller.releaseTrigger();
        else if (cmd == QStringLiteral("--preview")) controller.previewOverlay();
        else if (cmd == QStringLiteral("--cancel")) controller.cancelTrigger();
        else if (cmd == QStringLiteral("--quit")) app.quit();
        else if (cmd == QStringLiteral("--restart")) restart();
        else if (cmd == QStringLiteral("--settings") || cmd.isEmpty()) showSettings();
    };
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            const auto receive = [&, socket] {
                if (!socket->canReadLine()) return;
                const QString cmd = QString::fromUtf8(socket->readLine(256)).trimmed();
                if (cmd == QStringLiteral("--status")) {
                    const QJsonObject status{{QStringLiteral("running"), true},
                        {QStringLiteral("platform"), QGuiApplication::platformName()},
                        {QStringLiteral("trigger"), controller.triggerStatus()},
                        {QStringLiteral("overlayVisible"), controller.overlayVisible()},
                        {QStringLiteral("overlayRevealed"), controller.overlayRevealed()},
                        {QStringLiteral("surfaceVisible"), overlay.isVisible()},
                        {QStringLiteral("selectedIndex"), controller.selectedIndex()},
                        {QStringLiteral("deckIndex"), controller.currentDeckIndex()},
                        {QStringLiteral("error"), controller.lastError()}};
                    socket->write(QJsonDocument(status).toJson(QJsonDocument::Compact) + '\n');
                } else {
                    handleCommand(cmd);
                    socket->write("OK\n");
                }
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &app, receive);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            receive(); // Data may have arrived before readyRead was connected.
        }
    });
    if (command != QStringLiteral("--background"))
        QTimer::singleShot(0, &app, [&] { handleCommand(command); });
    return app.exec();
}
