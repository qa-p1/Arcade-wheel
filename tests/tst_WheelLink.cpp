// Arcade Link: wheel.add_action drafts and the confirm-in-Settings flow.
#include "core/WheelController.h"
#include "link/WheelLink.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using namespace ArcadeLink;

class LinkBackend final : public PlatformBackend {
public:
    QString name() const override { return "test"; }
    void configureTrigger(const QJsonObject &) override {}
    QString shortcutConflict(const QString &) const override { return {}; }
    bool prepareOverlay(QQuickWindow *, QString *) override { return true; }
    QPointF cursorPosition() const override { return {5, 5}; }
    QVector<DiscoveredApplication> applications() const override { return {}; }
    bool launchApplication(const QString &, bool, QString *) override { return true; }
    bool performSystemAction(const QString &, QString *) override { return true; }
    bool setStartOnLogin(bool, QString *) override { return true; }
    bool startOnLogin() const override { return false; }
    void setShortcutRecording(bool) override {}
};

template <typename F>
static void onWorker(F &&f)
{
    QThread *t = QThread::create(std::forward<F>(f));
    t->start();
    QTRY_VERIFY_WITH_TIMEOUT(t->isFinished(), 15000);
    delete t;
}

class WheelLinkTest final : public QObject {
    Q_OBJECT
private slots:
    void drafts()
    {
        QString error;
        auto url = WheelLink::draftFor(textContent("text/url", "https://example.com/docs"), &error);
        QCOMPARE(url.value("type").toString(), QStringLiteral("url"));
        QCOMPARE(url.value("name").toString(), QStringLiteral("example.com"));
        auto cmd = WheelLink::draftFor(textContent("text/plain", "/usr/bin/kitty --directory /tmp", {"command"}), &error);
        QCOMPARE(cmd.value("type").toString(), QStringLiteral("command"));
        QCOMPARE(cmd.value("name").toString(), QStringLiteral("kitty"));
        QVERIFY(WheelLink::draftFor(textContent("text/plain", "just words"), &error).isEmpty());
        QTemporaryDir dir;
        auto file = WheelLink::draftFor(fileContent(dir.path()), &error);
        QCOMPARE(file.value("type").toString(), QStringLiteral("file"));
        QVERIFY(WheelLink::draftFor(fileContent(dir.filePath("missing.txt")), &error).isEmpty());
    }

    void structuredActionPreservesOptions()
    {
        QString error;
        const QJsonObject data{{"app", Ids::Box}, {"action", "box.pipeline.run"}, {"version", 1},
            {"title", "Send optimized screenshot"}, {"input", "clipboard"},
            {"preset", "share"}, {"options", QJsonObject{{"pipeline", "p-optimized-screenshot"}}}};
        auto draft = WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", data}}, &error);
        QCOMPARE(draft.value("type").toString(), QStringLiteral("arcade"));
        const auto payload = QJsonObject::fromVariantMap(draft.value("payload").toMap());
        QCOMPARE(payload.value("options"), data.value("options"));
        QCOMPARE(payload.value("input").toString(), QStringLiteral("clipboard"));
        QCOMPARE(payload.value("preset").toString(), QStringLiteral("share"));
        auto invalid = data; invalid.insert("input", "shell");
        QVERIFY(WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", invalid}}, &error).isEmpty());
        invalid = data; invalid.insert("options", "shell command");
        QVERIFY(WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", invalid}}, &error).isEmpty());
        bool offered = false;
        for (const auto &value : WheelLink::actions())
            if (value.toObject().value("id").toString() == "wheel.add_action")
                offered = value.toObject().value("accepts").toArray().contains("structured/arcade-action");
        QVERIFY(offered);
    }

    void addActionWaitsForTheUser()
    {
        QTemporaryDir dir;
        qputenv("ARCADE_HOME", dir.filePath("arcade").toUtf8());
        LinkBackend backend;
        WheelController controller(&backend, nullptr, dir.filePath("config.json"));
        controller.initialize();
        WheelLink link(QStringLiteral("9.9"));
        link.setController(&controller);
        link.apply(QJsonObject::fromVariantMap(controller.config()));
        QTRY_VERIFY_WITH_TIMEOUT(link.listening(), 5000);
        QSignalSpy opened(&controller, &WheelController::settingsRequested);
        const Locations loc = link.locations();

        // The user saves: the job succeeds.
        QJsonObject result;
        Error error;
        bool ok = false;
        QThread *t = QThread::create([&] {
            auto c = Client::connect(loc, Ids::Wheel, "arcade.lens", "1", 1000, &error);
            if (c) ok = c->invoke({{"action", "wheel.add_action"}, {"inputs", QJsonArray{textContent("text/url", "https://example.com")}},
                                   {"context", QJsonObject{{"source", "arcade.lens"}}}}, &result, &error);
        });
        t->start();
        QTRY_VERIFY(controller.linkPending());
        QCOMPARE(opened.count(), 1);
        QCOMPARE(controller.linkSource(), QStringLiteral("Arcade Lens"));
        // Nothing was written to a slot yet.
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(controller.config())).toJson().contains("https://example.com"));
        const QString deck = controller.currentDeck().value("id").toString();
        controller.setAction(deck, 0, controller.linkDraft());
        controller.finishLinkAction(true, "Applications");
        QTRY_VERIFY_WITH_TIMEOUT(t->isFinished(), 5000);
        delete t;
        QVERIFY2(ok, qPrintable(error.message));
        QCOMPARE(result.value("message").toString(), QStringLiteral("Added to Applications"));

        // The user cancels: the caller gets user_cancelled; a second request meanwhile is busy.
        Error cancelled, busy;
        t = QThread::create([&] {
            auto c = Client::connect(loc, Ids::Wheel, "arcade.lens", "1", 1000, &cancelled);
            if (c) c->invoke({{"action", "wheel.add_action"}, {"inputs", QJsonArray{textContent("text/url", "https://two.example")}}}, nullptr, &cancelled);
        });
        t->start();
        QTRY_VERIFY(controller.linkPending());
        onWorker([&] {
            auto c = Client::connect(loc, Ids::Wheel, "arcade.box", "1", 1000, &busy);
            if (c) c->invoke({{"action", "wheel.add_action"}, {"inputs", QJsonArray{textContent("text/url", "https://three.example")}}}, nullptr, &busy);
        });
        QCOMPARE(busy.code, QStringLiteral("busy"));
        controller.finishLinkAction(false);
        QTRY_VERIFY_WITH_TIMEOUT(t->isFinished(), 5000);
        delete t;
        QCOMPARE(cancelled.code, QStringLiteral("denied"));
        QCOMPARE(cancelled.reason, QStringLiteral("user_cancelled"));
        QCOMPARE(cancelled.userMessage("Arcade Wheel"), QStringLiteral("Cancelled."));
        qunsetenv("ARCADE_HOME");
    }
};

QTEST_MAIN(WheelLinkTest)
#include "tst_WheelLink.moc"
