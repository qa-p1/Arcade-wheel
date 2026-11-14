// The move from Qt's "Arcade Wheel/Arcade Wheel" folders to arcade/wheel
// (STANDARD §10): everything arrives, config.json becomes settings.json, and
// nothing is lost when the new folder already has settings.
#include "config/ConfigStore.h"
#include "core/WheelPaths.h"

#include <QDir>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void write(const QString &path, const QByteArray &data)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), data.size());
}
QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class WheelPathsTest final : public QObject {
    Q_OBJECT
private slots:
    void linuxFoldersMoveAndSettingsAreRenamed()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto user = ArcadeLink::UserDirs::under(root.path());
        const QString oldConfig = root.filePath(".config/Arcade Wheel/Arcade Wheel");
        const QString oldCache = root.filePath(".cache/Arcade Wheel/Arcade Wheel");
        write(oldConfig + "/config.json", R"({"schemaVersion":4})");
        write(oldConfig + "/config.json.backup-1-a.json", "backup");
        write(oldCache + "/logs/arcade-wheel.log", "log");

        QCOMPARE(WheelPaths::migrate(user, "linux"), QString());
        const auto dirs = ArcadeLink::appDirs(user, "linux", "arcade.wheel");
        QCOMPARE(dirs.config, root.filePath(".config/arcade/wheel"));
        QCOMPARE(read(dirs.settingsFile()), QByteArray(R"({"schemaVersion":4})"));
        QVERIFY(!QFile::exists(dirs.config + "/config.json"));
        QCOMPARE(read(dirs.config + "/config.json.backup-1-a.json"), QByteArray("backup"));
        QCOMPARE(read(dirs.cache + "/logs/arcade-wheel.log"), QByteArray("log"));
        QVERIFY(!QFileInfo::exists(oldConfig));
        QVERIFY(!QFileInfo::exists(root.filePath(".config/Arcade Wheel")));
        QVERIFY(!QFileInfo::exists(oldCache));

        // Idempotent.
        QCOMPARE(WheelPaths::migrate(user, "linux"), QString());
        QCOMPARE(read(dirs.settingsFile()), QByteArray(R"({"schemaVersion":4})"));
    }
    void anOldCacheThatConflictsWithTheNewOneIsRetired()
    {
        // The owner's machine: 0.5.0 left Qt's pipeline cache in both places.
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto user = ArcadeLink::UserDirs::under(root.path());
        const QString oldCache = root.filePath(".cache/Arcade Wheel/Arcade Wheel");
        write(oldCache + "/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl", "old");
        write(root.filePath(".cache/arcade/wheel/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl"), "new");
        QCOMPARE(WheelPaths::migrate(user, "linux"), QString());
        QVERIFY(!QFileInfo::exists(root.filePath(".cache/Arcade Wheel")));
        QCOMPARE(read(root.filePath(".cache/arcade/wheel/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl")), QByteArray("new"));
    }
    void anOldCacheHoldingLogsIsKeptWhenItConflicts()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto user = ArcadeLink::UserDirs::under(root.path());
        write(root.filePath(".cache/Arcade Wheel/Arcade Wheel/logs/arcade-wheel.log"), "old log");
        write(root.filePath(".cache/arcade/wheel/logs/arcade-wheel.log"), "new log");
        const QString note = WheelPaths::migrate(user, "linux");
        QVERIFY2(!note.isEmpty(), "a conflict with real files is reported, not deleted");
        QCOMPARE(read(root.filePath(".cache/Arcade Wheel/Arcade Wheel/logs/arcade-wheel.log")), QByteArray("old log"));
    }
    void existingSettingsAreNeverOverwritten()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto user = ArcadeLink::UserDirs::under(root.path());
        const auto dirs = ArcadeLink::appDirs(user, "linux", "arcade.wheel");
        write(dirs.settingsFile(), "new");
        write(dirs.config + "/config.json", "old");
        const QString note = WheelPaths::migrate(user, "linux");
        QVERIFY2(note.contains("settings.json already exists"), qPrintable(note));
        QCOMPARE(read(dirs.settingsFile()), QByteArray("new"));
        QCOMPARE(read(dirs.config + "/config.json"), QByteArray("old"));
    }
    void settingsStartFromTheOldFileWhenTheMoveFailed()
    {
#ifdef Q_OS_LINUX
        QTemporaryDir root;
        QVERIFY(root.isValid());
        qputenv("XDG_CONFIG_HOME", root.filePath(".config").toUtf8());
        auto saved = ConfigStore::defaults();
        auto general = saved.value("general").toObject();
        general.insert("holdThresholdMs", 140);
        saved.insert("general", general);
        const QString old = root.filePath(".config/Arcade Wheel/Arcade Wheel/config.json");
        write(old, QJsonDocument(saved).toJson());

        ConfigStore store;
        QCOMPARE(store.path(), root.filePath(".config/arcade/wheel/settings.json"));
        const auto loaded = store.load({});
        QCOMPARE(loaded.value("general").toObject().value("holdThresholdMs").toInt(), 140);
        QVERIFY(QFile::exists(store.path()));
        QVERIFY(QFile::exists(old)); // the original stays for the next move
        qunsetenv("XDG_CONFIG_HOME");
#else
        QSKIP("Linux folder names");
#endif
    }
    // WH-2 / RD-WHEEL-1: Qt's own cache (the scenegraph pipeline cache) is
    // written to CacheLocation, which comes from the organisation and
    // application names. It must be the folder Wheel uses, so an upgrade
    // leaves one cache tree and Qt does not recreate "Arcade Wheel/Arcade Wheel".
    void qtWritesItsCacheIntoTheOneWheelFolder()
    {
#ifdef Q_OS_LINUX
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray savedCache = qgetenv("XDG_CACHE_HOME");
        const QByteArray savedConfig = qgetenv("XDG_CONFIG_HOME");
        const QByteArray savedData = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_CACHE_HOME", root.filePath(".cache").toUtf8());
        qputenv("XDG_CONFIG_HOME", root.filePath(".config").toUtf8());
        qputenv("XDG_DATA_HOME", root.filePath(".local/share").toUtf8());
        const QString oldName = QCoreApplication::applicationName();
        const QString oldOrg = QCoreApplication::organizationName();
        const QString oldDomain = QCoreApplication::organizationDomain();
        WheelPaths::applyApplicationIdentity();

        const auto dirs = WheelPaths::dirs();
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::CacheLocation), dirs.cache);
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation), dirs.config);
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation), dirs.data);

        QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrg);
        QCoreApplication::setOrganizationDomain(oldDomain);
        for (const auto &[name, value] : {std::pair{"XDG_CACHE_HOME", savedCache}, std::pair{"XDG_CONFIG_HOME", savedConfig}, std::pair { "XDG_DATA_HOME",
                                                                                                                                          savedData }})
            value.isEmpty() ? qunsetenv(name) : qputenv(name, value);
#else
        QSKIP("Linux folder names");
#endif
    }
    void anUpgradeLeavesOneCacheFolderEvenAfterQtHasWritten()
    {
#ifdef Q_OS_LINUX
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QByteArray savedCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", root.filePath(".cache").toUtf8());
        const QString oldName = QCoreApplication::applicationName();
        const QString oldOrg = QCoreApplication::organizationName();
        // 0.5.0 left Qt's pipeline cache in the doubled folder.
        write(root.filePath(".cache/Arcade Wheel/Arcade Wheel/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl"), "cache");

        const auto user = ArcadeLink::UserDirs::under(root.path());
        QCOMPARE(WheelPaths::migrate(user, "linux"), QString());
        // The next start: Qt draws a window and writes its pipeline cache.
        WheelPaths::applyApplicationIdentity();
        const QString qtCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        write(qtCache + "/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl", "again");
        QCOMPARE(QDir(root.filePath(".cache")).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{"arcade"});
        QVERIFY(QFileInfo::exists(root.filePath(".cache/arcade/wheel/qtpipelinecache-x86_64-little_endian-lp64/qqpc_opengl")));

        QCoreApplication::setApplicationName(oldName);
        QCoreApplication::setOrganizationName(oldOrg);
        savedCache.isEmpty() ? qunsetenv("XDG_CACHE_HOME") : qputenv("XDG_CACHE_HOME", savedCache);
#else
        QSKIP("Linux folder names");
#endif
    }
};

QTEST_GUILESS_MAIN(WheelPathsTest)
#include "tst_WheelPaths.moc"
