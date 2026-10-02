#include "platform/linux/LinuxBackend.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSemaphore>
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
    void missingApplicationReportsFailure() {
        LinuxBackend backend; QString error;
        QVERIFY(!backend.launchApplication("nonexistent-arcade-fixture.desktop",false,&error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(LinuxLaunchTest)
#include "tst_LinuxLaunch.moc"
