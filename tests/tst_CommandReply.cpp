#include "core/CommandReply.h"
#include <QLocalServer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtTest>

class CommandReplyTest final : public QObject {
    Q_OBJECT
private slots:
    void replies_data()
    {
        QTest::addColumn<int>("delay");
        QTest::addColumn<QByteArray>("first");
        QTest::addColumn<QByteArray>("second");
        QTest::addColumn<int>("timeout");
        QTest::addColumn<bool>("buffered");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("slow import") << 1100 << QByteArray("O") << QByteArray("K\n")
            << 0 << false << QByteArray("OK\n");
        QTest::newRow("already buffered") << 0 << QByteArray("OK\n") << QByteArray()
            << 5000 << true << QByteArray("OK\n");
        QTest::newRow("unconfirmed partial reply") << 0 << QByteArray("OK") << QByteArray()
            << 5000 << false << QByteArray();
        QTest::newRow("deadline") << 300 << QByteArray("OK\n") << QByteArray()
            << 100 << false << QByteArray();
    }
    void replies()
    {
        QFETCH(int, delay);
        QFETCH(QByteArray, first);
        QFETCH(QByteArray, second);
        QFETCH(int, timeout);
        QFETCH(bool, buffered);
        QFETCH(QByteArray, expected);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
#ifdef Q_OS_WIN
        const QString name = QStringLiteral("wheel-command-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
#else
        const QString name = directory.filePath("reply.sock");
#endif
        QSemaphore ready;
        bool listening = false;
        auto *worker = QThread::create([&] {
            QLocalServer server;
            listening = server.listen(name);
            ready.release();
            if (!listening || !server.waitForNewConnection(2000)) return;
            auto *peer = server.nextPendingConnection();
            QThread::msleep(delay);
            peer->write(first);
            if (peer->bytesToWrite()) peer->waitForBytesWritten(1000);
            if (!second.isEmpty()) {
                QThread::msleep(20);
                peer->write(second);
                if (peer->bytesToWrite()) peer->waitForBytesWritten(1000);
            }
            peer->disconnectFromServer();
        });
        QLocalSocket client;
        const auto cleanup = qScopeGuard([&] { client.abort(); worker->wait(); delete worker; });
        worker->start();
        QVERIFY(ready.tryAcquire(1, 3000));
        QVERIFY(listening);
        client.connectToServer(name);
        QVERIFY(client.waitForConnected(1000));
        if (buffered) QVERIFY(client.waitForReadyRead(1000));
        QCOMPARE(timeout ? WheelCommands::readReply(client, timeout) : WheelCommands::readReply(client), expected);
    }
};
QTEST_GUILESS_MAIN(CommandReplyTest)
#include "tst_CommandReply.moc"
