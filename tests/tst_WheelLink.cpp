// Arcade Link: wheel.add_action drafts and the confirm-in-Settings flow.
#include "core/WheelController.h"
#include "link/WheelLink.h"

#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <QQuickView>

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
    void manifestPublishesCanonicalConfiguredHoldTriggerAndMenu()
    {
        auto config = ConfigStore::defaults();
        auto trigger = config.value(QStringLiteral("trigger")).toObject();
        trigger.insert(QStringLiteral("shortcut"), QStringLiteral("Tab"));
        config.insert(QStringLiteral("trigger"), trigger);
        auto manifest = WheelLink::manifest(config, QStringLiteral("0.3.0"));
        auto shortcuts = manifest.value(QStringLiteral("shortcuts")).toArray();
        QCOMPARE(shortcuts.size(), 1);
        QCOMPARE(shortcuts.first().toObject().value(QStringLiteral("id")).toString(), QStringLiteral("trigger"));
        QCOMPARE(shortcuts.first().toObject().value(QStringLiteral("accelerator")).toString(), QStringLiteral("Tab"));
        QCOMPARE(manifest.value(QStringLiteral("menu")).toArray().first().toObject().value(QStringLiteral("id")).toString(),
                 QStringLiteral("show-wheel"));
        QCOMPARE(manifest.value(QStringLiteral("docs")).toObject().value(QStringLiteral("version")).toString(),
                 QStringLiteral("0.3.0"));

        trigger.insert(QStringLiteral("shortcut"), QStringLiteral("Alt+Control+Space"));
        config.insert(QStringLiteral("trigger"), trigger);
        manifest = WheelLink::manifest(config, QStringLiteral("0.3.0"));
#ifdef Q_OS_MACOS
        const QString expected = QStringLiteral("Alt+Super+Space");
#else
        const QString expected = QStringLiteral("Ctrl+Alt+Space");
#endif
        QCOMPARE(manifest.value(QStringLiteral("shortcuts")).toArray().first().toObject()
                     .value(QStringLiteral("accelerator")).toString(), expected);
    }

    void statusReportsTheOriginalLaunchMode_data()
    {
        QTest::addColumn<bool>("background");
        QTest::newRow("foreground") << false;
        QTest::newRow("background") << true;
    }
    void statusReportsTheOriginalLaunchMode()
    {
        QFETCH(bool, background);
        QTemporaryDir dir; const auto previous = qgetenv("ARCADE_HOME");
        qputenv("ARCADE_HOME", dir.filePath("arcade").toUtf8());
        {
            LinkBackend backend; WheelController controller(&backend, nullptr, dir.filePath("config.json")); controller.initialize();
            WheelLink link("1", nullptr, background); link.setController(&controller); link.apply(QJsonObject::fromVariantMap(controller.config()));
            QTRY_VERIFY(link.listening());
            Error error; QJsonObject result;
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) result = client->call("app.status", {}, &error).toObject();
            });
            QVERIFY2(!error.isError(), qPrintable(error.message));
            QCOMPARE(result.value("status").toObject().value("mode").toString(), background ? QStringLiteral("background") : QStringLiteral("foreground"));
        }
        qputenv("ARCADE_HOME", previous);
    }
    void hubMethodsRoundTripAndValidation()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto previous = qgetenv("ARCADE_HOME");
        qputenv("ARCADE_HOME", dir.filePath("arcade").toUtf8());
        {
            LinkBackend backend; WheelController controller(&backend, nullptr, dir.filePath("config.json")); controller.initialize();
            QQuickView overlay; controller.setOverlayView(&overlay);
            WheelLink link("0.3.0"); link.setController(&controller);
            connect(&controller, &WheelController::configChanged, &link, [&] { link.apply(QJsonObject::fromVariantMap(controller.config())); });
            link.apply(QJsonObject::fromVariantMap(controller.config())); link.setTrayState("hosted");
            QTRY_VERIFY(link.listening());
            QSignalSpy settings(&controller, &WheelController::settingsRequested), restarted(&controller, &WheelController::restartRequested);
            Error error; QJsonObject description, status, rebound, imported; QJsonArray menu, outputs;
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "0.3.0", 1000, &error);
                if (!client) return;
                description = client->describe(&error);
                status = client->call("app.status", {}, &error).toObject();
                menu = client->menu(&error);
                if (!client->settings(&error)) return;
                rebound = client->shortcutsSet("trigger", "Alt+Control+Space", &error);
                if (error.isError()) return;
                outputs = client->settingsExport(&error);
                if (error.isError()) return;
                imported = client->settingsImport(outputs, &error);
                if (!error.isError()) client->restart({}, &error);
            });
            QVERIFY2(!error.isError(), qPrintable(error.message));
            QCOMPARE(settings.count(), 1); QTRY_COMPARE(restarted.count(), 1);
            const auto methods = description.value("methods").toArray();
            for (const auto &method : {"app.settings", "app.restart", "app.menu", "app.menu.invoke", "app.shortcuts.set", "app.settings.export", "app.settings.import"})
                QVERIFY2(methods.contains(QString::fromLatin1(method)), method);
            QCOMPARE(menu, WheelLink::menuItems());
            QCOMPARE(status.value("status").toObject().value("tray").toString(), QStringLiteral("hosted"));
            QVERIFY(rebound.value("applied").toBool()); QCOMPARE(rebound.value("via").toString(), QStringLiteral("native"));
            QVERIFY(imported.value("imported").toBool()); QVERIFY(!imported.value("restartRequired").toBool());
            QCOMPARE(outputs.size(), 1); QVERIFY(QFileInfo::exists(outputs.first().toObject().value("path").toString()));
            QVERIFY(!QDir(dir.path()).entryList({"config.json.backup-*.json"}, QDir::Files).isEmpty());
            QTRY_VERIFY([&] {
                QFile file(link.locations().manifestPath(Ids::Wheel));
                if (!file.open(QIODevice::ReadOnly)) return false;
                return QJsonDocument::fromJson(file.readAll()).object().value("shortcuts").toArray().first().toObject().value("accelerator").toString() == u"Ctrl+Alt+Space";
            }());
            const auto saved = controller.config();
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->shortcutsSet("trigger", "Ctrl+DefinitelyNotAKey", &error);
            });
            QCOMPARE(error.code, QStringLiteral("denied")); QCOMPARE(controller.config(), saved);
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->menuInvoke("missing", &error);
            });
            QCOMPARE(error.code, QStringLiteral("unavailable"));
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->menuInvoke("show-wheel", &error);
            });
            QVERIFY2(!error.isError(), qPrintable(error.message));
            QVERIFY(controller.overlayVisible()); controller.cancelTrigger();
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->settingsImport({QJsonObject{{"type", "file/any"}, {"path", "relative.json"}}}, &error);
            });
            QCOMPARE(error.code, QStringLiteral("unsupported_input"));
            QFile bad(dir.filePath("bad.json")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("{}"); bad.close();
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->settingsImport({QJsonObject{{"type", "file/any"}, {"path", bad.fileName()}}}, &error);
            });
            QCOMPARE(error.code, QStringLiteral("denied")); QCOMPARE(controller.config(), saved);
            QString draftError;
            QVERIFY(controller.beginLinkAction(WheelLink::draftFor(textContent("text/url", "https://example.com"), &draftError), "Test"));
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->restart({}, &error);
            });
            QCOMPARE(error.code, QStringLiteral("busy"));
            onWorker([&] {
                auto client = Client::connect(link.locations(), Ids::Wheel, Ids::Tools, "1", 1000, &error);
                if (client) client->restart("force", &error);
            });
            QVERIFY2(!error.isError(), qPrintable(error.message)); QTRY_COMPARE(restarted.count(), 2);
            controller.finishLinkAction(false);
        }
        if (previous.isNull()) qunsetenv("ARCADE_HOME"); else qputenv("ARCADE_HOME", previous);
    }

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

    void shelfActionPreservesNamedCollectionAndInputMode()
    {
        QString error;
        QJsonObject data{{"app", "arcade.shelf"}, {"action", "shelf.show"}, {"version", 1},
            {"title", "Open Research Shelf"}, {"input", "none"}, {"options", QJsonObject{{"shelf", "research"}}}};
        auto draft = WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", data}}, &error);
        QVERIFY2(!draft.isEmpty(), qPrintable(error));
        auto payload = QJsonObject::fromVariantMap(draft.value("payload").toMap());
        QCOMPARE(payload.value("app").toString(), QStringLiteral("arcade.shelf"));
        QCOMPARE(payload.value("options").toObject().value("shelf").toString(), QStringLiteral("research"));
        data.insert("action", "shelf.add"); data.insert("input", "file-selection");
        draft = WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", data}}, &error);
        QVERIFY(!draft.isEmpty());
        payload = QJsonObject::fromVariantMap(draft.value("payload").toMap());
        QCOMPARE(payload.value("input").toString(), QStringLiteral("file-selection"));
        data.insert("app", "shell.invalid");
        QVERIFY(WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", data}}, &error).isEmpty());
    }

    void findShowAcceptsClipboardAndFileSelection()
    {
        QString error;
        QJsonObject data{{"app", "arcade.find"}, {"action", "find.show"}, {"version", 1},
            {"title", "Search in Find"}, {"input", "clipboard"}};
        auto draft = WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", data}}, &error);
        QVERIFY2(!draft.isEmpty(), qPrintable(error));
        auto payload = QJsonObject::fromVariantMap(draft.value("payload").toMap());
        QCOMPARE(payload.value("app").toString(), QStringLiteral("arcade.find"));
        QCOMPARE(payload.value("input").toString(), QStringLiteral("clipboard"));
        data.insert("input", "file-selection");
        draft = WheelLink::draftFor({{"type", "structured/arcade-action"}, {"data", data}}, &error);
        QVERIFY(!draft.isEmpty());
    }

    void masterSwitchStopsPresenceAndPersistsToggles()
    {
        QTemporaryDir dir;
        const auto previous = qgetenv("ARCADE_HOME");
        qputenv("ARCADE_HOME", dir.filePath("arcade").toUtf8());
        {
            LinkBackend backend;
            WheelController controller(&backend, nullptr, dir.filePath("config.json"));
            controller.initialize();
            WheelLink link("9.9"); link.setController(&controller);
            connect(&controller, &WheelController::configChanged, &link, [&] { link.apply(QJsonObject::fromVariantMap(controller.config())); });
            link.apply(QJsonObject::fromVariantMap(controller.config()));
            QTRY_VERIFY(link.listening());
            controller.setPeerEnabled(Ids::Box, false);
            QCOMPARE(controller.config().value("link").toMap().value("disabledPeers").toList(), QVariantList{Ids::Box});
            controller.updateSetting("link", "enabled", false);
            QTRY_VERIFY(!link.listening());
            QTRY_VERIFY(!QFileInfo::exists(link.locations().endpointPath(Ids::Wheel)));
            QFile manifestFile(link.locations().manifestPath(Ids::Wheel));
            QVERIFY(manifestFile.open(QIODevice::ReadOnly));
            auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
            QVERIFY(!manifest.value("settings").toObject().value("linkEnabled").toBool());
            QVERIFY(manifest.value("actions").toArray().isEmpty());
            auto stored = ConfigStore(dir.filePath("config.json")).load({});
            QVERIFY(!stored.value("link").toObject().value("enabled").toBool());
            QCOMPARE(stored.value("link").toObject().value("disabledPeers").toArray(), QJsonArray{Ids::Box});
            controller.setPeerEnabled(Ids::Box, true);
            controller.updateSetting("link", "enabled", true);
            QTRY_VERIFY(link.listening());
            QCOMPARE(controller.config().value("link").toMap().value("disabledPeers").toList(), QVariantList{});
        }
        qputenv("ARCADE_HOME", previous);
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
