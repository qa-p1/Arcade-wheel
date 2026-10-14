#include "ArcadeTrayHost.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace ArcadeLink;
class WheelTrayHostTest final : public QObject {
    Q_OBJECT
private slots:
    void toolsHostingCrashExclusionAndLinkOff()
    {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto loc = Locations::under(root.path()); QVERIFY(ensurePrivateDir(loc.runtime));
        QLocalServer host; const QString address = socketAddress(loc, Ids::Tools); QVERIFY(host.listen(address));
        Endpoint ep; ep.address = address; ep.protocol = {1}; ep.token = newToken();
        ep.pid = QCoreApplication::applicationPid(); ep.transport = "unix"; ep.startedAt = nowRfc3339(); QVERIFY(ep.write(loc, Ids::Tools));
        QJsonObject manifest{{"schema", 1}, {"id", Ids::Tools}, {"executable", QCoreApplication::applicationFilePath()},
            {"settings", QJsonObject{{"trayHost", QJsonObject{{"enabled", true}, {"excluded", QJsonArray{}}}}}}};
        QVERIFY(writeManifest(loc, manifest));
        QPointer<QLocalSocket> subscriber;
        connect(&host, &QLocalServer::newConnection, this, [&] {
            while (auto *socket = host.nextPendingConnection()) {
                connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
                    while (socket->canReadLine()) {
                        QJsonObject request; if (classify(socket->readLine(), &request) != Kind::Request) continue;
                        const auto id = request.value("id").toInteger();
                        if (request.value("method").toString() == u"hello") socket->write(responseLine(id, QJsonObject{{"protocol", 1}, {"server", QJsonObject{{"id", Ids::Tools}}}}));
                        if (request.value("method").toString() == u"subscribe") {
                            subscriber = socket;
                            socket->write(responseLine(id, QJsonObject{{"topics", QJsonArray{"tray.host"}}}));
                            socket->write(notificationLine("tray.host", {{"hosted", true}}));
                        }
                    }
                });
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        ArcadeTrayHost watcher(loc, Ids::Wheel);
        QSignalSpy ready(&watcher, &ArcadeTrayHost::startupReady);
        QStringList states;
        connect(&watcher, &ArcadeTrayHost::stateChanged, this, [&](const QString &state) { QCOMPARE(QThread::currentThread(), thread()); states.append(state); });
        watcher.start(); QTRY_VERIFY(watcher.ready()); QCOMPARE(ready.count(), 1);
        QCOMPARE(watcher.state(), QStringLiteral("hosted")); QVERIFY(!states.contains("own"));
        subscriber->write(notificationLine("tray.host", {{"hosted", false}})); QTRY_COMPARE(watcher.state(), QStringLiteral("own"));
        subscriber->write(notificationLine("tray.host", {{"hosted", true}})); QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
        auto settings = manifest.value("settings").toObject(); auto tray = settings.value("trayHost").toObject();
        tray.insert("excluded", QJsonArray{Ids::Wheel}); settings.insert("trayHost", tray); manifest.insert("settings", settings); QVERIFY(writeManifest(loc, manifest));
        QTRY_COMPARE(watcher.state(), QStringLiteral("own"));
        tray.insert("excluded", QJsonArray{}); settings.insert("trayHost", tray); manifest.insert("settings", settings); QVERIFY(writeManifest(loc, manifest));
        QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
        watcher.setLinkEnabled(false); QCOMPARE(watcher.state(), QStringLiteral("own"));
        watcher.setLinkEnabled(true); QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
        QElapsedTimer latency; latency.start(); subscriber->abort();
        QTRY_COMPARE_WITH_TIMEOUT(watcher.state(), QStringLiteral("own"), 950);
        QVERIFY(latency.elapsed() < 1000);
        ep.token = newToken(); QVERIFY(ep.write(loc, Ids::Tools));
        QTRY_COMPARE(watcher.state(), QStringLiteral("hosted"));
        subscriber->write(notificationLine("tray.host", {{"hosted", true}, {"restarting", true}})); subscriber->flush();
        QTest::qWait(30); subscriber->abort(); host.close();
        QTest::qWait(50); QCOMPARE(watcher.state(), QStringLiteral("hosted"));
        // Ending Link during restart grace must return ownership immediately.
        watcher.setLinkEnabled(false); QCOMPARE(watcher.state(), QStringLiteral("own"));
        watcher.stop(); watcher.start(false, false); QTRY_VERIFY(watcher.ready()); QCOMPARE(watcher.state(), QStringLiteral("none"));
    }
    void abruptToolsDisconnectReturnsWithinOneSecond()
    {
        TrayHostStateMachine state;
        state.begin(0, true); state.host(10, true); state.closed(11);
        QCOMPARE(state.state(), QStringLiteral("own"));
        QVERIFY(!state.deadline());
    }
};
QTEST_GUILESS_MAIN(WheelTrayHostTest)
#include "tst_WheelTrayHost.moc"
