#include "core/IconImageProvider.h"
#include "core/IconTheme.h"
#include "core/CommandReply.h"
#include "core/WheelController.h"
#include "core/WheelPaths.h"
#include "link/WheelLink.h"
#include "link/qt/ArcadeLink.h"
#include "link/qt/ArcadeTrayHost.h"
#include "install/AppInstaller.h"
#ifdef Q_OS_WIN
#include "platform/windows/WindowsBackend.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacOSBackend.h"
#else
#include "platform/linux/LinuxBackend.h"
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#endif

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QLockFile>
#include <QTextStream>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>
#include <memory>

Q_IMPORT_QML_PLUGIN(ArcadePlugin)
Q_IMPORT_QML_PLUGIN(ArcadeWheelPlugin)
#ifdef __GLIBC__
#include <malloc.h>
#endif
static void releaseFreedHeap()
{
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

// The tray icon: Wheel's glyph, monochrome (STANDARD §8). White on the usual
// dark Linux and Windows panels; on macOS a black template image the menu bar
// tints for its appearance.
static QIcon trayIcon()
{
    QFile file(QStringLiteral(":/assets/arcade/arcade.wheel.svg"));
    if (!file.open(QIODevice::ReadOnly)) return {};
#ifdef Q_OS_MACOS
    const QByteArray ink = "#000000";
#else
    const QByteArray ink = "#ffffff";
#endif
    QSvgRenderer renderer(file.readAll().replace("currentColor", ink));
    QIcon icon;
    for (const int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        {
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
        }
        icon.addPixmap(pixmap);
    }
#ifdef Q_OS_MACOS
    icon.setIsMask(true);
#endif
    return icon;
}

static bool readSettingsForExport(QJsonObject *config, QString *error)
{
    QFile file(WheelPaths::readableSettingsFile());
    if (!file.exists()) {
        *config = ConfigStore::defaults();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid configuration JSON: %1").arg(parseError.errorString());
        return false;
    }
    *config = ConfigStore::normalize(document.object(), error);
    return !config->isEmpty();
}

int main(int argc, char **argv)
{
    ArcadeLink::onStartup();
    // The kit draws every control itself on Qt Quick Templates; Basic is the
    // lightest fallback for anything it does not cover.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Arcade Wheel"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("arcadewheel.org"));
    QCoreApplication::setApplicationName(QStringLiteral("Arcade Wheel"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARCADE_VERSION));
    app.setQuitOnLastWindowClosed(false);
    // The OS identifier (STANDARD §10): Wayland app_id, the desktop file and
    // the portal's app id. Before 0.5 it was com.arcadewheel.ArcadeWheel.
    app.setDesktopFileName(QStringLiteral("dev.arcade.Wheel"));

    const QStringList cliArguments = app.arguments().mid(1);
    const QString command = cliArguments.isEmpty() ? QStringLiteral("--settings") : cliArguments.first();
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
            "  arcade-wheel --hide        Dismiss the wheel (also --cancel)\n"
            "  arcade-wheel --toggle      Open the wheel, or dismiss it when open\n"
            "  arcade-wheel --press       Simulate pressing the trigger\n"
            "  arcade-wheel --release     Simulate releasing the trigger (runs the selection)\n"
            "  arcade-wheel --status      Show resident status as JSON\n"
            "  arcade-wheel --restart     Restart the resident launcher\n"
            "  arcade-wheel --quit        Quit the resident launcher\n"
            "  arcade-wheel --menu        Print app-specific tray items as JSON\n"
            "  arcade-wheel --settings-export FILE\n"
            "                             Export portable settings without credentials or device identity\n"
            "  arcade-wheel --settings-import FILE\n"
            "                             Import portable settings after saving a private backup\n"
            "  arcade-wheel --install [--silent] [--no-launch] [--channel C]\n"
            "  arcade-wheel --uninstall [--silent] [--remove-data]\n"
            "  arcade-wheel --repair\n"
            "  arcade-wheel --integration-status\n"
            "                             Show receipt, integration entries and PATH status\n"
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
        QFile file(WheelPaths::readableSettingsFile());
        if (file.open(QIODevice::ReadOnly)) config = ConfigStore::normalize(QJsonDocument::fromJson(file.readAll()).object());
        if (config.isEmpty()) config = ConfigStore::defaults();
        QTextStream(stdout) << QJsonDocument(WheelLink::manifest(config, QStringLiteral(ARCADE_VERSION))).toJson(QJsonDocument::Indented);
        return 0;
    }
    if (command == QStringLiteral("--menu")) {
        if (cliArguments.size() != 1) {
            QTextStream(stderr) << "Usage: arcade-wheel --menu\n";
            return 2;
        }
        QTextStream(stdout) << QJsonDocument(WheelLink::menuItems()).toJson(QJsonDocument::Indented);
        return 0;
    }
    if (command == QStringLiteral("--settings-export")) {
        if (cliArguments.size() != 2 || cliArguments.at(1).isEmpty()) {
            QTextStream(stderr) << "Usage: arcade-wheel --settings-export FILE\n";
            return 2;
        }
        QJsonObject config;
        QString error;
        if (!readSettingsForExport(&config, &error)) {
            QTextStream(stderr) << error << '\n';
            return 1;
        }
        const QString output = QFileInfo(cliArguments.at(1)).absoluteFilePath();
        if (!ConfigStore().exportTo(config, output, &error)) {
            QTextStream(stderr) << error << '\n';
            return 1;
        }
        QTextStream(stdout) << "Exported portable settings to " << output << '\n';
        return 0;
    }
    if (command == QStringLiteral("--settings-import")
        && (cliArguments.size() != 2 || cliArguments.at(1).isEmpty())) {
        QTextStream(stderr) << "Usage: arcade-wheel --settings-import FILE\n";
        return 2;
    }
    const int installCommand = AppInstaller::command(cliArguments, QStringLiteral(ARCADE_VERSION));
    if (installCommand >= 0) return installCommand;
    const bool headlessPlatform = QGuiApplication::platformName() == u"offscreen"
        || QGuiApplication::platformName() == u"minimal";
    // One resident per user and display: the command socket's name comes
    // from the settings folder, so isolated test profiles get their own.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const auto instanceKeyFor = [&](const QString &folder) {
        QByteArray identity = folder.toUtf8() + qgetenv("ARCADE_WHEEL_INSTANCE") + qgetenv("WAYLAND_DISPLAY");
        // Test/offscreen instances must never intercept commands for the desktop.
        if (headlessPlatform) identity += "-headless";
        const auto hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(12);
        return QStringLiteral("arcade-wheel-%1").arg(QString::fromLatin1(hash));
    };
    const auto serverNameFor = [&](const QString &key) {
#ifdef Q_OS_WIN
        return key;
#else
        // A bare name would live in the shared /tmp, where another local user
        // could pre-create it. The runtime directory is private to this user.
        return QDir(runtime).filePath(key + QStringLiteral(".sock"));
#endif
    };
    // A real start moves the folders from before 0.5 (STANDARD §10). An older
    // Wheel still running from them is asked to quit first, so nothing writes
    // there mid-copy. Commands that only ask or dismiss leave them alone.
    QString migrationNote;
    const bool query = QStringList{QStringLiteral("--status"), QStringLiteral("--quit"), QStringLiteral("--release"),
                                   QStringLiteral("--cancel"), QStringLiteral("--hide")}.contains(command);
    if (!smokeTest && !headlessPlatform && !query) {
        const auto legacy = ArcadeLink::legacyDirs(ArcadeLink::Ids::Wheel);
        if (std::any_of(legacy.begin(), legacy.end(), [](const auto &dir) { return QFileInfo::exists(dir.path); })) {
            // Before 0.5 the key came from Qt's "Arcade Wheel/Arcade Wheel" config folder.
            QLocalSocket older;
            older.connectToServer(serverNameFor(instanceKeyFor(
                QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))));
            if (older.waitForConnected(150)) {
                older.write("--quit\n");
                older.waitForBytesWritten(300);
                older.waitForDisconnected(5000);
            }
            migrationNote = WheelPaths::migrate();
        }
    }
    if (!AppInstaller::firstRun(cliArguments, QStringLiteral(ARCADE_VERSION), smokeTest || headlessPlatform)) return 0;
#ifdef Q_OS_LINUX
    // An installed copy from before 0.5: its launcher and login entry move to dev.arcade.Wheel.
    if (!smokeTest && !headlessPlatform && !query) {
        AppInstaller::Engine::Environment environment;
        QString error;
        if (!AppInstaller::Engine::Environment::discover(&environment, &error)
            || !AppInstaller::migrateIntegration(environment, AppInstaller::Engine::Runtime::discover(), &error))
            migrationNote += (migrationNote.isEmpty() ? QString() : QStringLiteral(" "))
                + QStringLiteral("Couldn't move the launcher to its new name: ") + error;
    }
#endif
    const QString instanceKey = instanceKeyFor(WheelPaths::dirs().config);
    const QString serverName = serverNameFor(instanceKey);
    if (command == QStringLiteral("--restarting")) {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(150)) {
            // Wait on the old command socket, whose teardown precedes lock
            // release. No repeated sleeps or probes on the startup thread.
            probe.waitForDisconnected(3000);
        }
    } else {
        QLocalSocket existing;
        existing.connectToServer(serverName);
        if (existing.waitForConnected(150)) {
            QByteArray request;
            if (command == QStringLiteral("--settings-import")) {
                request = QJsonDocument(QJsonObject{{QStringLiteral("command"), command},
                    {QStringLiteral("path"), QFileInfo(cliArguments.at(1)).absoluteFilePath()}}).toJson(QJsonDocument::Compact) + '\n';
                if (request.size() > 16000) {
                    QTextStream(stderr) << "The settings path is too long.\n";
                    return 2;
                }
            } else request = command.toUtf8() + '\n';
            existing.write(request);
            existing.waitForBytesWritten(300);
            const QByteArray reply = WheelCommands::readReply(existing);
            if (!reply.isEmpty()) {
                if (command == QStringLiteral("--status")) QTextStream(stdout) << reply;
                else if (command == QStringLiteral("--settings-import")) {
                    if (reply.startsWith("OK")) {
                        QTextStream(stdout) << "Imported settings into the running Arcade Wheel.\n";
                        return 0;
                    }
                    QTextStream(stderr) << reply;
                    return 1;
                }
            }
            if (command == QStringLiteral("--settings-import")) {
                QTextStream(stderr) << "Arcade Wheel did not confirm the settings import.\n";
                return 1;
            }
            // A resident that is still starting accepts the connection before
            // its event loop answers; report that instead of an empty success.
            if (command == QStringLiteral("--status") && reply.isEmpty()) {
                QTextStream(stderr) << "Arcade Wheel did not answer.\n";
                return 1;
            }
            return 0;
        }
        if (command == QStringLiteral("--release") || command == QStringLiteral("--cancel")
            || command == QStringLiteral("--hide") || command == QStringLiteral("--quit")) return 0;
        if (command == QStringLiteral("--status")) {
            // One line of JSON either way (STANDARD §10); exit status 1 when not running.
            QTextStream(stdout) << QJsonDocument(QJsonObject{{QStringLiteral("running"), false},
                {QStringLiteral("version"), QStringLiteral(ARCADE_VERSION)}}).toJson(QJsonDocument::Compact) << '\n';
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
    // Started at login or by Tools, stderr often goes to /dev/null: keep
    // Qt's warnings in the log folder the manifest advertises instead.
    ArcadeLink::captureStderr(WheelPaths::logsDir(), QStringLiteral("arcade-wheel"));
    if (!migrationNote.isEmpty()) qWarning("%s", qPrintable(migrationNote));

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
    // Apps installed or removed while Wheel runs: rescan their launchers once
    // a burst of changes settles, instead of waiting for Settings → Refresh.
    // A folder that does not exist yet is watched through its nearest parent.
    QFileSystemWatcher applicationFolders;
    QTimer applicationRescan;
    applicationRescan.setSingleShot(true);
    applicationRescan.setInterval(1000);
    const auto watchApplicationFolders = [&] {
        QStringList wanted;
        for (QString dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
            while (!QFileInfo(dir).isDir() && QFileInfo(dir).path() != dir) dir = QFileInfo(dir).path();
            if (QFileInfo(dir).isDir() && !wanted.contains(dir)) wanted.append(dir);
        }
        const QStringList watched = applicationFolders.directories();
        for (const QString &dir : watched) if (!wanted.contains(dir)) applicationFolders.removePath(dir);
        for (const QString &dir : wanted) if (!watched.contains(dir)) applicationFolders.addPath(dir);
    };
    if (!smokeTest) {
        QObject::connect(&applicationFolders, &QFileSystemWatcher::directoryChanged, &applicationRescan,
                         qOverload<>(&QTimer::start));
        QObject::connect(&applicationRescan, &QTimer::timeout, &controller, [&] {
            watchApplicationFolders();
            controller.refreshApplications();
        });
        watchApplicationFolders();
    }
    // Arcade Link, set up once the event loop runs. Test instances (the smoke
    // test, offscreen platforms) stay out of the real registry unless they
    // were given their own ARCADE_HOME.
    const bool headless = QGuiApplication::platformName() == QStringLiteral("offscreen")
        || QGuiApplication::platformName() == QStringLiteral("minimal");
    std::unique_ptr<WheelLink> link;
    if (!smokeTest && (!headless || !qEnvironmentVariableIsEmpty("ARCADE_HOME"))) {
        link = std::make_unique<WheelLink>(QStringLiteral(ARCADE_VERSION), nullptr, command == QStringLiteral("--background"));
        link->setController(&controller);
        QObject::connect(link.get(), &WheelLink::diagnosticsChanged, &controller, &WheelController::setLinkDiagnostics);
        QTimer::singleShot(0, link.get(), [&] { link->apply(QJsonObject::fromVariantMap(controller.config())); });
        QObject::connect(&controller, &WheelController::configChanged, link.get(),
                         [&] { link->apply(QJsonObject::fromVariantMap(controller.config())); });
    }
    ArcadeLink::ArcadeTrayHost arcadeTrayHost(ArcadeLink::Locations::discover(), ArcadeLink::Ids::Wheel);
    const bool hasTray = !smokeTest && !headless;
    const auto syncTrayState = [&](const QString &state) { if (link) link->setTrayState(state); };
    QObject::connect(&arcadeTrayHost, &ArcadeLink::ArcadeTrayHost::stateChanged, &app, syncTrayState);
    QObject::connect(&controller, &WheelController::configChanged, &arcadeTrayHost, [&] {
        arcadeTrayHost.setLinkEnabled(controller.config().value("link").toMap().value("enabled", true).toBool());
    });
    arcadeTrayHost.start(controller.config().value("link").toMap().value("enabled", true).toBool(), hasTray);
    syncTrayState(arcadeTrayHost.state());
#ifdef Q_OS_LINUX
    // Inside the AppImage Qt cannot load the host's platform theme plugin
    // (qtengine, qt6ct, ...) and falls back to "hicolor", which lacks most
    // application icons. Use the theme the user configured instead.
    IconTheme::applyUserTheme();
#endif
    QIcon appIcon(QStringLiteral(":/assets/arcade-wheel.png"));
    if (appIcon.isNull()) appIcon = QIcon::fromTheme(QStringLiteral("applications-system"));
    app.setWindowIcon(appIcon);

    QQuickView overlay;
    overlay.setColor(Qt::transparent);
    overlay.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                     Qt::WindowDoesNotAcceptFocus);
    overlay.setResizeMode(QQuickView::SizeRootObjectToView);
    overlay.engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
    overlay.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    overlay.loadFromModule("ArcadeWheel", "Overlay");
    if (overlay.status() == QQuickView::Error) return 1;
    controller.setOverlayView(&overlay);
    // Inter at 13 px (the kit's body size) for every window, Qt Widgets menus
    // and dialogs included. The kit's Fonts singleton has already loaded it
    // from the resources.
    if (auto *fonts = overlay.engine()->singletonInstance<QObject *>("Arcade", "Fonts")) {
        QFont font(fonts->property("sans").toString());
        font.setPixelSize(13);
        QApplication::setFont(font);
    }

    // Settings holds its own QML engine, about 80 MiB. Keep it for a quick
    // reopen, then let it go.
    // Declared before the view: a view destroyed while visible (at exit)
    // reports itself hidden, and that handler restarts this timer.
    QTimer releaseSettings;
    releaseSettings.setSingleShot(true);
    releaseSettings.setInterval(10000);
    std::unique_ptr<QQuickView> settings;
    QObject::connect(&releaseSettings, &QTimer::timeout, &app, [&] {
        if (!settings || settings->isVisible()) return;
        settings.reset();
        releaseFreedHeap();
    });
    const auto showSettings = [&] {
        releaseSettings.stop();
        if (!settings) {
            settings = std::make_unique<QQuickView>();
            settings->setTitle(QStringLiteral("Settings — Arcade Wheel"));
            // The editor needs room for the deck list, preview and slots;
            // the kit's own minimum is 720 x 520.
            settings->setMinimumSize(QSize(720, 520));
            settings->resize(1080, 720);
            settings->setResizeMode(QQuickView::SizeRootObjectToView);
            settings->engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
            settings->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
            settings->loadFromModule("ArcadeWheel", "SettingsWindow");
            if (settings->status() == QQuickView::Error) {
                app.exit(1);
                return;
            }
            // Closing Settings gives up an action another app asked to add.
            QObject::connect(settings.get(), &QWindow::visibleChanged, &controller, [&](bool visible) {
                if (visible) return;
                controller.finishLinkAction(false);
                releaseSettings.start();
            });
        }
        settings->show();
        settings->raise();
        settings->requestActivate();
    };

    // Activity is a third QML engine. Its jobs live in the controller, so it
    // is released like Settings and rebuilt with them on the next job.
    QTimer releaseActivity;
    releaseActivity.setSingleShot(true);
    releaseActivity.setInterval(10000);
    std::unique_ptr<QQuickView> activity;
    QObject::connect(&releaseActivity, &QTimer::timeout, &app, [&] {
        if (!activity || activity->isVisible()) return;
        activity.reset();
        releaseFreedHeap();
    });
    QObject::connect(&controller, &WheelController::linkActivityRequested, &app, [&] {
        releaseActivity.stop();
        if (!activity) {
            activity = std::make_unique<QQuickView>();
            activity->setTitle(QStringLiteral("Activity — Arcade Wheel"));
            activity->setResizeMode(QQuickView::SizeRootObjectToView);
            activity->resize(540, 320);
            activity->engine()->addImageProvider(QStringLiteral("icons"), new IconImageProvider);
            activity->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
            activity->loadFromModule("ArcadeWheel", "LinkActivity");
            QObject::connect(activity.get(), &QWindow::visibleChanged, &releaseActivity, [&](bool visible) {
                if (!visible) releaseActivity.start();
            });
        }
        activity->show();
    });

    QSystemTrayIcon tray;
    tray.setToolTip(QStringLiteral("Arcade Wheel"));
    const QIcon glyph = trayIcon();
    tray.setIcon(glyph.isNull() ? appIcon : glyph);
    QMenu menu;
    auto *wheelAction = menu.addAction(QStringLiteral("Open wheel"));
    QObject::connect(wheelAction, &QAction::triggered, &controller, &WheelController::showWheel);
    auto *openAction = menu.addAction(QStringLiteral("Settings"));
    menu.addSeparator();
    auto *restartAction = menu.addAction(QStringLiteral("Restart Arcade Wheel"));
    auto *quitAction = menu.addAction(QStringLiteral("Quit Arcade Wheel"));
    QObject::connect(openAction, &QAction::triggered, &app, showSettings);
    QObject::connect(quitAction, &QAction::triggered, &app, &QCoreApplication::quit);
    QObject::connect(&controller, &WheelController::settingsRequested, &app, showSettings);
    QObject::connect(&controller, &WheelController::quitRequested, &app, &QCoreApplication::quit);
    const auto restart = [&] {
        if (!controller.flushPendingSave()) return;
        // Quitting without a successor would silently remove the launcher.
        QString executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_LINUX
        if (!qEnvironmentVariableIsEmpty("APPIMAGE")) executable = qEnvironmentVariable("APPIMAGE");
#endif
        // The successor runs from a new mount; it mustn't keep this one alive.
        QProcess successor;
        successor.setProgram(executable);
        successor.setArguments({QStringLiteral("--restarting")});
        ArcadeLink::detachFromImage(successor);
        if (!successor.startDetached()) {
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
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showSettings();
    });
    const auto updateTray = [&] {
        tray.setVisible(hasTray && arcadeTrayHost.ready() && arcadeTrayHost.state() == u"own"
                        && QSystemTrayIcon::isSystemTrayAvailable());
    };
    QObject::connect(&arcadeTrayHost, &ArcadeLink::ArcadeTrayHost::stateChanged, &tray, updateTray);
    QObject::connect(&arcadeTrayHost, &ArcadeLink::ArcadeTrayHost::startupReady, &tray, updateTray);
    // Quitting while Tools hosts the tray must not flash Wheel's own icon.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &arcadeTrayHost, [&arcadeTrayHost] { arcadeTrayHost.finish(); });
    updateTray();
#ifdef Q_OS_LINUX
    // At login Wheel often starts before the panel that hosts tray icons, and
    // panels restart. Show (or re-register) the icon whenever the tray host
    // appears; this waits on D-Bus, so it costs nothing while idle.
    auto *trayHost = new QDBusServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                             QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForRegistration, &app);
    QObject::connect(trayHost, &QDBusServiceWatcher::serviceRegistered, &app, [&] {
        tray.hide();
        updateTray();
    });
#endif

    const auto handleCommand = [&](const QString &raw, const QString &value = QString()) {
        const QString cmd = raw.trimmed();
        if (cmd == QStringLiteral("--show")) controller.showWheel();
        else if (cmd == QStringLiteral("--press")) controller.pressTrigger();
        else if (cmd == QStringLiteral("--release")) controller.releaseTrigger();
        else if (cmd == QStringLiteral("--preview")) controller.previewOverlay();
        // --hide is the standard name for --cancel (STANDARD §10).
        else if (cmd == QStringLiteral("--cancel") || cmd == QStringLiteral("--hide")) controller.cancelTrigger();
        else if (cmd == QStringLiteral("--toggle")) {
            if (controller.overlayVisible()) controller.cancelTrigger();
            else controller.showWheel();
        }
        else if (cmd == QStringLiteral("--quit")) app.quit();
        else if (cmd == QStringLiteral("--restart")) restart();
        else if (cmd == QStringLiteral("--settings") || cmd.isEmpty()) showSettings();
        else if (cmd == QStringLiteral("--settings-import")) {
            if (value.isEmpty() || !controller.importConfig(value)) return false;
            // Confirm the saved import before the first Settings/QML load.
            QTimer::singleShot(0, &app, showSettings);
        }
        return true;
    };
    const auto acceptCommands = [&] {
        while (auto *socket = server.nextPendingConnection()) {
            const auto receive = [&, socket] {
                if (!socket->canReadLine()) return;
                const QByteArray line = socket->readLine(16384).trimmed();
                QString cmd = QString::fromUtf8(line);
                QString value;
                QJsonParseError parseError;
                const auto envelope = QJsonDocument::fromJson(line, &parseError);
                if (parseError.error == QJsonParseError::NoError && envelope.isObject()) {
                    cmd = envelope.object().value(QStringLiteral("command")).toString();
                    value = envelope.object().value(QStringLiteral("path")).toString();
                }
                if (cmd == QStringLiteral("--status")) {
                    const bool linkEnabled = controller.config().value(QStringLiteral("link")).toMap()
                                                 .value(QStringLiteral("enabled"), true).toBool();
                    const QJsonObject status{{QStringLiteral("running"), true},
                        {QStringLiteral("version"), QStringLiteral(ARCADE_VERSION)},
                        {QStringLiteral("link"), QJsonObject{{QStringLiteral("enabled"), linkEnabled},
                                                             {QStringLiteral("listening"), link && link->listening()}}},
                        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                        {QStringLiteral("tray"), arcadeTrayHost.state()},
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
                    const bool ok = handleCommand(cmd, value);
                    if (cmd == QStringLiteral("--settings-import") && !ok)
                        socket->write((QStringLiteral("ERROR: ") + controller.lastError() + QLatin1Char('\n')).toUtf8());
                    else socket->write("OK\n");
                }
                socket->flush();
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &app, receive);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            receive(); // Data may have arrived before readyRead was connected.
        }
    };
    QObject::connect(&server, &QLocalServer::newConnection, &app, acceptCommands);
    // A command sent while Wheel was starting was announced before this
    // handler existed, and Qt does not announce it again: answer it once the
    // event loop runs.
    QTimer::singleShot(0, &app, acceptCommands);
    if (smokeTest) {
        QTimer::singleShot(0, &app, showSettings);
        QTimer::singleShot(1500, &app, [&] {
            app.exit(settings && settings->status() == QQuickView::Ready
                     && overlay.status() == QQuickView::Ready ? 0 : 1);
        });
    } else if (command != QStringLiteral("--background"))
        QTimer::singleShot(0, &app, [&] {
            if (!handleCommand(command, command == QStringLiteral("--settings-import") ? cliArguments.value(1) : QString())
                && command == QStringLiteral("--settings-import")) app.exit(1);
        });
    return app.exec();
}
