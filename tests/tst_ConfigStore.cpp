#include "config/ConfigStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class ConfigStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void centerGesturesMigrateAndRoundTrip()
    {
        auto old = ConfigStore::defaults();
        old.insert("schemaVersion",2);
        old.remove("centerGestures");
        old.insert("trigger",QJsonObject{{"shortcut","Tab"},{"overrideConflict",true}});
        auto migrated=ConfigStore::normalize(old);
        QCOMPARE(migrated.value("trigger").toObject().value("shortcut").toString(),QString("Tab"));
        auto gestures=migrated.value("centerGestures").toObject();
        QVERIFY(!gestures.value("doubleClick").toObject().value("enabled").toBool());
        auto withoutSingle=migrated;
        auto existingGestures=gestures;
        existingGestures.remove("singleClick");
        withoutSingle.insert("centerGestures",existingGestures);
        const auto single=ConfigStore::normalize(withoutSingle).value("centerGestures").toObject().value("singleClick").toObject();
        QVERIFY(!single.value("enabled").toBool());
        QVERIFY(single.value("actions").toArray().isEmpty());
        QJsonArray actions;
        for (int i=0;i<5;++i)
            actions.append(QJsonObject{{"id","duplicate"},{"type","application"},{"name",QString::number(i)},
                {"payload",QJsonObject{{"desktopId",QString::number(i)+".desktop"}}}});
        gestures.insert("doubleClick",QJsonObject{{"name","Work"},{"enabled",true},{"actions",actions}});
        gestures.insert("clickIntervalMs",1);
        migrated.insert("centerGestures",gestures);
        migrated=ConfigStore::normalize(migrated);
        QCOMPARE(migrated.value("centerGestures").toObject().value("clickIntervalMs").toInt(),160);
        const auto normalized=migrated.value("centerGestures").toObject().value("doubleClick").toObject().value("actions").toArray();
        QSet<QString> ids;
        for(const auto &a:normalized) ids.insert(a.toObject().value("id").toString());
        QCOMPARE(ids.size(),5);
        QTemporaryDir dir; ConfigStore store(dir.filePath("config.json")); QString error;
        QVERIFY2(store.save(migrated,&error),qPrintable(error));
        QVERIFY2(store.exportTo(migrated,dir.filePath("export.json"),&error),qPrintable(error));
        QJsonObject imported;
        QVERIFY2(store.importFrom(dir.filePath("export.json"),&imported,&error),qPrintable(error));
        QCOMPARE(imported,migrated);
        QCOMPARE(store.load({},&error),migrated);
    }
    void defaultsUseSixDiscoveredApplications()
    {
        QVector<DiscoveredApplication> applications;
        for (int i = 0; i < 8; ++i) {
            applications.append({QStringLiteral("org.example.app%1.desktop").arg(i),
                                 QStringLiteral("Application %1").arg(i),
                                 QStringLiteral("app-icon-%1").arg(i), {}, {}});
        }

        const QJsonObject config = ConfigStore::defaults(applications);
        QCOMPARE(config.value(QStringLiteral("schemaVersion")).toInt(), ConfigStore::SchemaVersion);
        const QJsonArray decks = config.value(QStringLiteral("decks")).toArray();
        QCOMPARE(decks.size(), 1);
        const QJsonArray actions = decks.first().toObject().value(QStringLiteral("actions")).toArray();
        QCOMPARE(actions.size(), 6);
        for (int i = 0; i < actions.size(); ++i) {
            const QJsonObject action = actions.at(i).toObject();
            QCOMPARE(action.value(QStringLiteral("type")).toString(), QStringLiteral("application"));
            QCOMPARE(action.value(QStringLiteral("name")).toString(), QStringLiteral("Application %1").arg(i));
            QCOMPARE(action.value(QStringLiteral("payload")).toObject()
                         .value(QStringLiteral("desktopId")).toString(),
                     QStringLiteral("org.example.app%1.desktop").arg(i));
            QVERIFY(!action.value(QStringLiteral("id")).toString().isEmpty());
        }
    }

    void defaultsFillRemainingSlotsWithPlaceholders()
    {
        const QVector<DiscoveredApplication> applications{
            {QStringLiteral("org.example.only.desktop"), QStringLiteral("Only app"), {}, {}, {}}};
        const QJsonArray actions = ConfigStore::defaults(applications)
                                       .value(QStringLiteral("decks")).toArray().first().toObject()
                                       .value(QStringLiteral("actions")).toArray();
        QCOMPARE(actions.size(), 6);
        QCOMPARE(actions.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Only app"));
        QCOMPARE(actions.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("none"));
    }

    void migratesVersionZeroPagesAndItems()
    {
        const QJsonArray legacyItems{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("url")},
                        {QStringLiteral("name"), QStringLiteral("Legacy link")},
                        {QStringLiteral("payload"), QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.invalid")}}}}
        };
        const QJsonObject legacy{{QStringLiteral("pages"), QJsonArray{QJsonObject{{QStringLiteral("items"), legacyItems}}}}};

        QString error;
        const QJsonObject migrated = ConfigStore::normalize(legacy, &error);
        QVERIFY2(!migrated.isEmpty(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(migrated.value(QStringLiteral("schemaVersion")).toInt(), ConfigStore::SchemaVersion);
        QVERIFY(!migrated.contains(QStringLiteral("pages")));

        const QJsonArray decks = migrated.value(QStringLiteral("decks")).toArray();
        QCOMPARE(decks.size(), 1);
        const QJsonObject deck = decks.first().toObject();
        QVERIFY(!deck.value(QStringLiteral("id")).toString().isEmpty());
        QCOMPARE(deck.value(QStringLiteral("name")).toString(), QStringLiteral("Deck"));
        const QJsonArray actions = deck.value(QStringLiteral("actions")).toArray();
        QCOMPARE(actions.size(), 4);
        QCOMPARE(actions.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Legacy link"));
        QCOMPARE(actions.first().toObject().value(QStringLiteral("payload")).toObject()
                     .value(QStringLiteral("url")).toString(),
                 QStringLiteral("https://example.invalid"));
        QVERIFY(!actions.first().toObject().value(QStringLiteral("id")).toString().isEmpty());
    }

    void normalizationRepairsDecksAndLimitsActionCount()
    {
        QJsonArray actions;
        for (int i = 0; i < 10; ++i) {
            actions.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("action-%1").arg(i)},
                                       {QStringLiteral("type"), QStringLiteral("command")},
                                       {QStringLiteral("name"), QStringLiteral("Action %1").arg(i)},
                                       {QStringLiteral("payload"), QJsonValue(QStringLiteral("invalid-payload"))}});
        }
        const QJsonObject input{{QStringLiteral("schemaVersion"), 1},
                                {QStringLiteral("decks"), QJsonArray{QJsonObject{
                                     {QStringLiteral("actions"), actions}}}}};

        QString error;
        const QJsonObject normalized = ConfigStore::normalize(input, &error);
        QVERIFY2(!normalized.isEmpty(), qPrintable(error));
        QVERIFY(error.isEmpty());
        const QJsonObject deck = normalized.value(QStringLiteral("decks")).toArray().first().toObject();
        QVERIFY(!deck.value(QStringLiteral("id")).toString().isEmpty());
        QCOMPARE(deck.value(QStringLiteral("name")).toString(), QStringLiteral("Deck"));
        const QJsonArray repaired = deck.value(QStringLiteral("actions")).toArray();
        QCOMPARE(repaired.size(), 8);
        for (const QJsonValue &value : repaired) {
            const QJsonObject action = value.toObject();
            QVERIFY(action.value(QStringLiteral("payload")).isObject());
            QVERIFY(!action.value(QStringLiteral("icon")).toString().isEmpty());
        }

        const QJsonObject tooSmall{{QStringLiteral("schemaVersion"), 1},
                                   {QStringLiteral("decks"), QJsonArray{QJsonObject{
                                        {QStringLiteral("id"), QStringLiteral("short")},
                                        {QStringLiteral("actions"), QJsonArray{QJsonObject{}}}}}}};
        const QJsonArray padded = ConfigStore::normalize(tooSmall).value(QStringLiteral("decks"))
                                      .toArray().first().toObject().value(QStringLiteral("actions")).toArray();
        QCOMPARE(padded.size(), 4);
    }

    void importExportRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString runtimePath = directory.filePath(QStringLiteral("runtime/config.json"));
        const QString exportPath = directory.filePath(QStringLiteral("exported.json"));
        ConfigStore store(runtimePath);
        QJsonObject original = ConfigStore::defaults();
        original.insert(QStringLiteral("general"),
                        QJsonObject{{QStringLiteral("startOnLogin"), true},
                                    {QStringLiteral("showNotifications"), false}});
        QVERIFY2(store.exportTo(original, exportPath), "Export should write a valid JSON file");

        QJsonObject imported;
        QString error;
        QVERIFY2(store.importFrom(exportPath, &imported, &error), qPrintable(error));
        QCOMPARE(imported, original);
        QCOMPARE(imported.value(QStringLiteral("general")).toObject()
                     .value(QStringLiteral("startOnLogin")).toBool(), true);
    }

    void invalidConfigurationsAreRejected()
    {
        QString error;
        QVERIFY(ConfigStore::normalize({}, &error).isEmpty());
        QVERIFY(!error.isEmpty());

        error.clear();
        const QJsonObject future{{QStringLiteral("schemaVersion"), ConfigStore::SchemaVersion + 1}};
        QVERIFY(ConfigStore::normalize(future, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("Unsupported")));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString invalidPath = directory.filePath(QStringLiteral("broken.json"));
        QFile file(invalidPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{ this is not json"), qint64(18));
        file.close();

        QJsonObject imported;
        error.clear();
        QVERIFY(!ConfigStore(invalidPath).importFrom(invalidPath, &imported, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(imported.isEmpty());
    }
};

QTEST_APPLESS_MAIN(ConfigStoreTest)
#include "tst_ConfigStore.moc"
