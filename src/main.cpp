#include "core/IconImageProvider.h"
#include "core/WheelController.h"
#include "link/WheelLink.h"
#ifdef Q_OS_WIN
#include "platform/windows/WindowsBackend.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacOSBackend.h"
#else
#include "platform/linux/LinuxBackend.h"
#endif

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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
#include <QTemporaryDir>
#include <memory>

int main(int argc, char **argv)
{
    QQuickStyle::setStyle(QStringLiteral("Material"));
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arcade Wheel"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("arcadewheel.org"));
    QCoreApplication::setApplicationName(QStringLiteral("Arcade Wheel"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARCADE_VERSION));
    app.setQuitOnLastWindowClosed(false);
    app.setDesktopFileName(QStringLiteral("com.arcadewheel.ArcadeWheel"));

    const QString command = app.arguments().size() > 1 ? app.arguments().at(1) : QStringLiteral("--settings");
    const bool smokeTest = command == QStringLiteral("--smoke-test");
    std::unique_ptr<QTemporaryDir> smokeDirectory;
    if (smokeTest) {
        smokeDirectory = std::make_unique<QTemporaryDir>();
        if (!smokeDirectory->isValid()) return 1;
        qputenv("ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT", "1");
        qputenv("ARCADE_WHEEL_INSTANCE", smokeDirectory->path().toUtf8());
    }
    if (command == QStringLiteral("--help") || command == QStringLiteral("-h")) {
        QTextStream(stdout) << "Arcade Wheel\n\n"
            "  arcade-wheel               Open Settings; keep the launcher running\n"
            "  arcade-wheel --settings    Same as above\n"
            "  arcade-wheel --background  Start the resident launcher without a window\n"
            "  arcade-wheel --show        Open the wheel; click an action or right-click to cancel\n"
            "  arcade-wheel --preview     Show a safe visual preview\n"
            "  arcade-wheel --cancel      Dismiss the wheel\n"
            "  arcade-wheel --press       Simulate pressing the trigger\n"
            "  arcade-wheel --release     Simulate releasing the trigger (runs the selection)\n"
            "  arcade-wheel --status      Show resident status as JSON\n"
            "  arcade-wheel --restart     Restart the resident launcher\n"
            "  arcade-wheel --quit        Quit the resident launcher\n"
            "  arcade-wheel --version     Print the version\n"
            "  arcade-wheel --arcade-manifest\n"
            "                             Print the Arcade Link manifest (no side effects)\n\n"
            "Once running, hold the configured shortcut (F8 by default), move toward an action, then release.\n";
        return 0;
    }
    if (command == QStringLiteral("--version") || command == QStringLiteral("-V")) {
        QTextStream(stdout) << "Arcade Wheel " << ARCADE_VERSION << '\n';
        return 0;
    }
    if (command == QStringLiteral("--arcade-manifest")) {
        // Read the configuration without the store, which would write defaults.
        QJsonObject config;
        QFile file(ConfigStore().path());
        if (file.open(QIODevice::ReadOnly)) config = ConfigStore::normalize(QJsonDocument::fromJson(file.readAll()).object());
        if (config.isEmpty()) config = ConfigStore::defaults();
        QTextStream(stdout) << QJsonDocument(WheelLink::manifest(config, QStringLiteral(ARCADE_VERSION))).toJson(QJsonDocument::Indented);
        return 0;
    }
    QByteArray identity = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toUtf8()
        + qgetenv("ARCADE_WHEEL_INSTANCE") + qgetenv("WAYLAND_DISPLAY");
    // Test/offscreen instances must never intercept commands for the desktop.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen") ||
        QGuiApplication::platformName() == QStringLiteral("minimal")) identity += "-headless";
    const auto hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(12);
    const QString instanceKey = QStringLiteral("arcade-wheel-%1").arg(QString::fromLatin1(hash));
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#ifdef Q_OS_WIN
    const QString serverName = instanceKey;
#else
    // A bare name would live in the shared /tmp, where another local user
    // could pre-create it. The runtime directory is private to this user.
    const QString serverName = QDir(runtime).filePath(instanceKey + QStringLiteral(".sock"));
#endif
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

    QLockFile instanceLock(QDir(runtime).filePath(instanceKey + QStringLiteral(".lock")));
    if (!instanceLock.tryLock(500)) {
        QTextStream(stderr) << "Arcade Wheel is already starting. Try again in a moment.\n";
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(serverName); // a dead process may leave its socket behind
    if (!server.listen(serverName)) {
        QTextStream(stderr) << "Arcade Wheel could not open its command socket: " << server.errorString() << '\n';
        return 1;
    }

#ifdef Q_OS_WIN
    WindowsBackend backend;
#elif defined(Q_OS_MACOS)
    MacOSBackend backend;
#else
    LinuxBackend backend;
#endif
    WheelController controller(&backend, nullptr,
        smokeTest ? smokeDirectory->filePath(QStringLiteral("config.json")) : QString());
    controller.initialize();
    // Arcade Link, set up once the event loop runs. Test instances (the smoke
    // test, offscreen platforms) stay out of the real registry unless they
    // were given their own ARCADE_HOME.
    const bool headless = QGuiApplication::platformName() == QStringLiteral("offscreen")
        || QGuiApplication::platformName() == QStringLiteral("minimal");
    std::unique_ptr<WheelLink> link;
    if (!smokeTest && (!headless || !qEnvironmentVariableIsEmpty("ARCADE_HOME"))) {
        link = std::make_unique<WheelLink>(QStringLiteral(ARCADE_VERSION));
        link->setController(&controller);
        QTimer::singleShot(0, link.get(), [&] { link->apply(QJsonObject::fromVariantMap(controller.config())); });
        QObject::connect(&controller, &WheelController::configChanged, link.get(),
                         [&] { link->apply(QJsonObject::fromVariantMap(controller.config())); });
    }
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
    if (overlay.status() == QQuickView::Error) return 1;
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
            if (settings->status() == QQuickView::Error) {
                app.exit(1);
                return;
            }
            // Closing Settings gives up an action another app asked to add.
            QObject::connect(settings.get(), &QWindow::visibleChanged, &controller, [&](bool visible) {
                if (!visible) controller.finishLinkAction(false);
            });
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
        // Quitting without a successor would silently remove the launcher.
        QString executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_LINUX
        if (!qEnvironmentVariableIsEmpty("APPIMAGE")) executable = qEnvironmentVariable("APPIMAGE");
#endif
        if (!QProcess::startDetached(executable, {QStringLiteral("--restarting")})) {
            const QString message = QStringLiteral("Arcade Wheel could not start a new instance, so it kept running.");
            if (tray.isVisible()) tray.showMessage(QStringLiteral("Arcade Wheel"), message, QSystemTrayIcon::Warning, 3000);
            QTextStream(stderr) << message << '\n';
            return;
        }
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
    if (smokeTest) {
        QTimer::singleShot(0, &app, showSettings);
        QTimer::singleShot(1500, &app, [&] {
            app.exit(settings && settings->status() == QQuickView::Ready
                     && overlay.status() == QQuickView::Ready ? 0 : 1);
        });
    } else if (command != QStringLiteral("--background"))
        QTimer::singleShot(0, &app, [&] { handleCommand(command); });
    return app.exec();
}
