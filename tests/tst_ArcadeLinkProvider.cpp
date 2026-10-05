#include "providers/ArcadeLinkProvider.h"
#include "link/WheelInvoke.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMimeData>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace ArcadeLink;

namespace {
QJsonObject fixture(const QString &name)
{
    QFile file(QStringLiteral(ARCADE_PROVIDER_FIXTURES) + '/' + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QString writeFixture(const QTemporaryDir &dir, const QJsonObject &json, const QString &name = "fixture.json")
{
    const auto path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(json).toJson());
    return path;
}
struct MockPeer {
    QProcess process;
    bool start(const QString &app, const QString &path, const QString &log = {})
    {
        auto env = QProcessEnvironment::systemEnvironment();
        if (!log.isEmpty()) env.insert("ARCADE_MOCK_LOG", log);
        process.setProcessEnvironment(env);
        process.start(QStandardPaths::findExecutable("arcade-link"), {"mock", "--as", app, "--actions", path});
        return process.waitForStarted(3000);
    }
    ~MockPeer() { if (process.state() != QProcess::NotRunning) { process.terminate(); process.waitForFinished(3000); } }
};
QJsonObject slot(const QString &app, const QString &action, const QString &input = "none", const QJsonObject &options = {})
{
    return {{"type", "arcade"}, {"name", "Test action"}, {"payload", QJsonObject{{"app", app}, {"action", action},
        {"version", 1}, {"input", input}, {"options", options}}}};
}
void setClipboardFile(const QString &path)
{
    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QGuiApplication::clipboard()->setMimeData(mime);
}
QList<QJsonObject> invocations(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    QList<QJsonObject> rows;
    for (const auto &line : file.readAll().split('\n')) if (!line.isEmpty()) rows << QJsonDocument::fromJson(line).object();
    return rows;
}
}

class ArcadeLinkProviderTest final : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> root;
    QByteArray previousHome;
private slots:
    void init()
    {
        previousHome = qgetenv("ARCADE_HOME");
        root = std::make_unique<QTemporaryDir>();
        QVERIFY(root->isValid());
        qputenv("ARCADE_HOME", root->filePath("arcade").toUtf8());
        QGuiApplication::clipboard()->clear();
    }
    void cleanup()
    {
        qputenv("ARCADE_HOME", previousHome);
        root.reset();
    }
    void standaloneHasNoArcadeEntries()
    {
        ArcadeLinkProvider provider;
        QSignalSpy changed(&provider, &ArcadeLinkProvider::changed);
        provider.apply({{"enabled", true}});
        QTRY_VERIFY(changed.count() >= 2);
        QVERIFY(provider.tools().isEmpty());
        QCOMPARE(provider.unavailableReason(slot(Ids::Box, "box.open")), QStringLiteral("Arcade Box isn't installed."));
        QCOMPARE(provider.connectedApps().size(), 5);
        for (const auto &row : provider.connectedApps()) QCOMPARE(row.toMap().value("state").toString(), QStringLiteral("Not installed"));
        QVERIFY(provider.shortcutOwner("F8").isEmpty());
    }
    void mockDiscoveryBeforeAndAfterWheel()
    {
        if (QStandardPaths::findExecutable("arcade-link").isEmpty()) QSKIP("arcade-link mock is not on PATH");
        MockPeer box;
        QVERIFY(box.start(Ids::Box, QStringLiteral(ARCADE_PROVIDER_FIXTURES) + "/box.json"));
        ArcadeLinkProvider provider;
        provider.apply({{"enabled", true}});
        QTRY_VERIFY_WITH_TIMEOUT(provider.tools().size() >= 6, 5000);
        for (const auto &row : provider.tools()) QVERIFY(row.toMap().value("id").toString() != "box:arcade.pdf.ocr");
        QCOMPARE(provider.shortcutOwner("Alt+Control+space"), QStringLiteral("Arcade Box"));
        MockPeer lens;
        QVERIFY(lens.start(Ids::Lens, QStringLiteral(ARCADE_PROVIDER_FIXTURES) + "/lens.json"));
        QTRY_VERIFY_WITH_TIMEOUT(!provider.manifestFor(Ids::Lens).isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(provider.connectedApps().at(1).toMap().value("state").toString(), QStringLiteral("Running · v0.0.0-mock"), 5000);
        const auto image = slot(Ids::Box, "box:arcade.image.convert#webp", "lens-selection");
        QTRY_VERIFY(provider.unavailableReason(image).isEmpty());
        provider.apply({{"enabled", true}, {"disabledPeers", QJsonArray{Ids::Box}}});
        for (const auto &row : provider.tools()) QVERIFY(row.toMap().value("app").toString() != Ids::Box);
        QCOMPARE(provider.unavailableReason(image), QStringLiteral("Arcade Box has connections to other Arcade apps turned off."));
        provider.apply({{"enabled", false}});
        QVERIFY(provider.tools().isEmpty());
        provider.apply({{"enabled", true}});
        QTRY_VERIFY(!provider.tools().isEmpty());
        auto mismatched = image; auto payload = mismatched.value("payload").toObject();
        payload.insert("version", 99); mismatched.insert("payload", payload);
        QCOMPARE(provider.unavailableReason(mismatched), QStringLiteral("Arcade Box needs an update to work with this app."));
#ifdef Q_OS_LINUX
        for (const auto &row : provider.tools()) QVERIFY(!row.toMap().value("inputModes").toStringList().contains("file-selection"));
#endif
    }
    void optionsProgressAndCancellationDoNotBlockUi()
    {
        if (QStandardPaths::findExecutable("arcade-link").isEmpty()) QSKIP("arcade-link mock is not on PATH");
        const auto log = root->filePath("calls.jsonl");
        MockPeer box;
        QVERIFY(box.start(Ids::Box, QStringLiteral(ARCADE_PROVIDER_FIXTURES) + "/box.json", log));
        ArcadeLinkProvider provider;
        provider.apply({{"enabled", true}});
        QTRY_VERIFY_WITH_TIMEOUT(!provider.manifestFor(Ids::Box).isEmpty(), 5000);
        QSignalSpy started(&provider, &ArcadeLinkProvider::jobStarted);
        QSignalSpy finished(&provider, &ArcadeLinkProvider::jobFinished);
        QSignalSpy progress(&provider, &ArcadeLinkProvider::jobProgress);
        QString error;
        QVERIFY2(provider.execute(slot(Ids::Box, "box.pipeline.run", "none", {{"pipeline", "p-optimized-screenshot"}}), &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY(finished.first().at(2).toString().isEmpty());
        QCOMPARE(invocations(log).first().value("options").toObject().value("pipeline").toString(), QStringLiteral("p-optimized-screenshot"));
        QVERIFY(progress.count() > 0);
        const auto video = root->filePath("in.mp4");
        QFile file(video); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("video"); file.close();
        setClipboardFile(video);
        const auto action = slot(Ids::Box, "box:arcade.video.compress#share-25mb", "clipboard");
        QTRY_VERIFY(provider.unavailableReason(action).isEmpty());
        int heartbeats = 0;
        QTimer heartbeat; heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, this, [&heartbeats] { ++heartbeats; }); heartbeat.start();
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY2(provider.execute(action, &error), qPrintable(error));
        QVERIFY2(elapsed.elapsed() < 100, "execute blocked the UI");
        QTRY_VERIFY(heartbeats > 10);
        provider.cancel(started.last().first().toString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 3000);
        QCOMPARE(finished.last().at(2).toString(), QStringLiteral("Cancelled."));
    }
    void lensSelectionRunsBeforeTheTarget()
    {
        if (QStandardPaths::findExecutable("arcade-link").isEmpty()) QSKIP("arcade-link mock is not on PATH");
        const auto imagePath = root->filePath("region.png"); QImage image(2, 2, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(imagePath));
        auto lensFixture = fixture("lens.json"); auto actions = lensFixture.value("actions").toArray(); auto capture = actions.first().toObject();
        capture.insert("mock", QJsonObject{{"result", QJsonObject{{"outputs", QJsonArray{
            QJsonObject{{"type", "file/image"}, {"path", imagePath}, {"owner", Ids::Lens}},
            QJsonObject{{"type", "screen/region"}, {"data", QJsonObject{}}}}}}}});
        actions.replace(0, capture); lensFixture.insert("actions", actions);
        const auto log = root->filePath("calls.jsonl");
        MockPeer box, lens;
        QVERIFY(box.start(Ids::Box, QStringLiteral(ARCADE_PROVIDER_FIXTURES) + "/box.json", log));
        QVERIFY(lens.start(Ids::Lens, writeFixture(*root, lensFixture), log));
        ArcadeLinkProvider provider; provider.apply({{"enabled", true}});
        const auto action = slot(Ids::Box, "box:arcade.image.convert#webp", "lens-selection");
        QTRY_VERIFY_WITH_TIMEOUT(provider.unavailableReason(action).isEmpty(), 5000);
        QSignalSpy finished(&provider, &ArcadeLinkProvider::jobFinished);
        QString error; QVERIFY2(provider.execute(action, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QVERIFY2(finished.first().at(2).toString().isEmpty(), qPrintable(finished.first().at(2).toString()));
        const auto calls = invocations(log); QCOMPARE(calls.size(), 2);
        QCOMPARE(calls[0].value("action").toString(), QStringLiteral("lens.capture"));
        QCOMPARE(calls[1].value("inputs").toArray().size(), 1);
        QCOMPARE(calls[1].value("inputs").toArray().first().toObject().value("path").toString(), imagePath);
    }
    void clipboardLimitIsDisabledBeforeInvoke()
    {
        if (QStandardPaths::findExecutable("arcade-link").isEmpty()) QSKIP("arcade-link mock is not on PATH");
        const QJsonObject json{{"actions", QJsonArray{QJsonObject{{"id", "box.small"}, {"title", "Small input"}, {"accepts", QJsonArray{"text/plain"}}, {"maxBytes", 4}}}}};
        MockPeer box; QVERIFY(box.start(Ids::Box, writeFixture(*root, json)));
        ArcadeLinkProvider provider; provider.apply({{"enabled", true}});
        QGuiApplication::clipboard()->setText("12345");
        const auto action = slot(Ids::Box, "box.small", "clipboard");
        QTRY_COMPARE_WITH_TIMEOUT(provider.unavailableReason(action), QStringLiteral("Too large for Arcade Box (limit 4 bytes)."), 5000);
        QString error; QVERIFY(!provider.execute(action, &error)); QCOMPARE(error, QStringLiteral("Too large for Arcade Box (limit 4 bytes)."));
    }
    void peerErrorsAndTimeout_data()
    {
        QTest::addColumn<QJsonObject>("behavior"); QTest::addColumn<QString>("message");
        QTest::newRow("private") << QJsonObject{{"error", "denied"}, {"reason", "private_mode"}} << QStringLiteral("Arcade Box is in Private mode.");
        QTest::newRow("secret") << QJsonObject{{"error", "denied"}, {"reason", "secret"}} << QStringLiteral("Not sent: this looks like a password or key.");
        QTest::newRow("timeout") << QJsonObject{{"steps", 20}, {"stepMs", 100}} << QStringLiteral("Arcade Box didn't respond in time.");
        QTest::newRow("crash") << QJsonObject{{"steps", 20}, {"stepMs", 40}, {"crashAfterMs", 80}} << QStringLiteral("Arcade Box isn't running.");
    }
    void peerErrorsAndTimeout()
    {
        if (QStandardPaths::findExecutable("arcade-link").isEmpty()) QSKIP("arcade-link mock is not on PATH");
        QFETCH(QJsonObject, behavior); QFETCH(QString, message);
        const QJsonObject json{{"actions", QJsonArray{QJsonObject{{"id", "box.test"}, {"title", "Test"}, {"mock", behavior}}}}};
        MockPeer box; QVERIFY(box.start(Ids::Box, writeFixture(*root, json)));
        ArcadeLinkProvider provider(nullptr, 300); provider.apply({{"enabled", true}});
        QTRY_VERIFY_WITH_TIMEOUT(!provider.manifestFor(Ids::Box).isEmpty(), 5000);
        QSignalSpy finished(&provider, &ArcadeLinkProvider::jobFinished);
        QString error; QVERIFY2(provider.execute(slot(Ids::Box, "box.test"), &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
        QCOMPARE(finished.first().at(2).toString(), message);
    }
};
QTEST_MAIN(ArcadeLinkProviderTest)
#include "tst_ArcadeLinkProvider.moc"
