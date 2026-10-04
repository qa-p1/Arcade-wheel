#include "platform/linux/LinuxBackend.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>

class CompositorFixture final : public QThread {
public:
    QString path;
    QSemaphore ready;
    std::atomic<bool> existing{false};
    std::atomic<int> focuses{0};
    void run() override {
        QLocalServer server;
        server.listen(path);
        QObject::connect(&server,&QLocalServer::newConnection,&server,[&] {
            while(auto *socket=server.nextPendingConnection()) {
                QObject::connect(socket,&QLocalSocket::readyRead,&server,[&,socket] {
                    const auto request=socket->readAll();
                    if(request.contains("clients")) socket->write(existing
                        ? "[{\"class\":\"ArcadeLaunchFixture\",\"mapped\":true,\"address\":\"0x123\"}]" : "[]");
                    else { if(request.contains("focuswindow")) ++focuses; socket->write("ok"); }
                    socket->disconnectFromServer();
                });
                QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
        ready.release(); exec();
    }
    ~CompositorFixture() override { quit(); wait(); }
};
class LinuxLaunchTest final : public QObject {
    Q_OBJECT
private slots:
    void launchWithFocusEnabledFallsBackWhenNoWindowExists() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        qputenv("XDG_RUNTIME_DIR",dir.path().toUtf8());
        qputenv("HYPRLAND_INSTANCE_SIGNATURE","fixture");
        qputenv("XDG_DATA_HOME",dir.path().toUtf8());
        qputenv("XDG_DATA_DIRS",dir.filePath("empty").toUtf8());
        QDir().mkpath(dir.filePath("hypr/fixture"));
        QDir().mkpath(dir.filePath("applications"));
        CompositorFixture compositor; compositor.path=dir.filePath("hypr/fixture/.socket.sock");
        compositor.start(); compositor.ready.acquire();
        const auto marker=dir.filePath("result");
        const auto program=dir.filePath("app with spaces");
        QFile executable(program); QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.write(("#!/bin/sh\nprintf '%s\\n' \"$@\" > \""+marker+"\"\n").toUtf8());
        executable.close(); executable.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        QFile desktop(dir.filePath("applications/fixture.desktop")); QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write(("[Desktop Entry]\nType=Application\nName=Test App\nStartupWMClass=ArcadeLaunchFixture\nExec=\""+program+"\" \"argument with spaces\" %c %%\n").toUtf8()); desktop.close();
        LinuxBackend backend; backend.applications(); QString error;
        QVERIFY2(backend.launchApplication("fixture.desktop",true,&error),qPrintable(error));
        QTRY_VERIFY(QFile::exists(marker));
        QFile result(marker); QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(),QByteArray("argument with spaces\nTest App\n%\n")); result.close();
        QCOMPARE(compositor.focuses.load(),0);
        QVERIFY(QFile::remove(marker)); compositor.existing=true;
        QVERIFY2(backend.launchApplication("fixture.desktop",true,&error),qPrintable(error));
        QCOMPARE(compositor.focuses.load(),1); QVERIFY(!QFile::exists(marker));
    }
    void autostartTogglingLeavesNoBackups() {
        if (QCoreApplication::applicationFilePath().startsWith("/tmp/"))
            QSKIP("Start on login deliberately refuses executables in temporary directories");
        QTemporaryDir dir; QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME",dir.path().toUtf8());
        LinuxBackend backend; QString error;
        const QDir autostart(dir.filePath("autostart"));
        for (int i=0;i<3;++i) {
            QVERIFY2(backend.setStartOnLogin(true,&error),qPrintable(error)); QVERIFY(backend.startOnLogin());
            QVERIFY2(backend.setStartOnLogin(false,&error),qPrintable(error)); QVERIFY(!backend.startOnLogin());
        }
        QCOMPARE(autostart.entryList(QDir::Files),QStringList());
        // A hand-edited entry is still preserved before it is replaced.
        QFile custom(autostart.filePath("arcade-wheel.desktop")); QVERIFY(custom.open(QIODevice::WriteOnly));
        custom.write("[Desktop Entry]\nType=Application\nName=My launcher\nExec=true\n"); custom.close();
        QVERIFY2(backend.setStartOnLogin(true,&error),qPrintable(error));
        QCOMPARE(autostart.entryList({"arcade-wheel.desktop.backup-*"},QDir::Files).size(),1);
        qunsetenv("XDG_CONFIG_HOME");
    }
    void appImageAutostartUsesDurableImagePath() {
        const auto previousImage = qgetenv("APPIMAGE");
        const auto previousConfig = qgetenv("XDG_CONFIG_HOME");
        const auto restore = qScopeGuard([&] {
            if (previousImage.isNull()) qunsetenv("APPIMAGE"); else qputenv("APPIMAGE", previousImage);
            if (previousConfig.isNull()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", previousConfig);
        });
        // Keep the executable fixture outside ephemeral roots: that is the
        // same durability requirement enforced for real user login entries.
        QTemporaryDir installation(QCoreApplication::applicationDirPath() + "/image-test-XXXXXX");
        QTemporaryDir config;
        QVERIFY(installation.isValid()); QVERIFY(config.isValid());
        if (installation.path().startsWith("/tmp/") || installation.path().startsWith("/run/"))
            QSKIP("The build itself is in a temporary directory");
        QFile image(installation.filePath("Arcade Wheel.AppImage"));
        QVERIFY(image.open(QIODevice::WriteOnly)); image.write("#!/bin/sh\nexit 0\n"); image.close();
        QVERIFY(image.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("APPIMAGE", image.fileName().toUtf8());
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        LinuxBackend backend; QString error;
        QVERIFY2(backend.setStartOnLogin(true, &error), qPrintable(error));
        QFile entry(config.filePath("autostart/arcade-wheel.desktop"));
        QVERIFY(entry.open(QIODevice::ReadOnly));
        const auto contents = entry.readAll(); entry.close();
        QVERIFY(contents.contains(image.fileName().toUtf8()));
        QVERIFY(!contents.contains(QCoreApplication::applicationFilePath().toUtf8()));
        QVERIFY(backend.setStartOnLogin(false, &error));

        QFile temporaryImage(config.filePath("Temporary.AppImage"));
        QVERIFY(temporaryImage.open(QIODevice::WriteOnly)); temporaryImage.write("#!/bin/sh\n"); temporaryImage.close();
        QVERIFY(temporaryImage.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        if (config.path().startsWith("/tmp/") || config.path().startsWith("/run/")) {
            qputenv("APPIMAGE", temporaryImage.fileName().toUtf8());
            QVERIFY(!backend.setStartOnLogin(true, &error));
            QVERIFY(error.contains("temporary"));
            QVERIFY(!QFile::exists(entry.fileName()));
        }
    }
    void missingApplicationReportsFailure() {
        LinuxBackend backend; QString error;
        QVERIFY(!backend.launchApplication("nonexistent-arcade-fixture.desktop",false,&error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(LinuxLaunchTest)
#include "tst_LinuxLaunch.moc"
