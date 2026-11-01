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
};

QTEST_GUILESS_MAIN(WheelPathsTest)
#include "tst_WheelPaths.moc"
