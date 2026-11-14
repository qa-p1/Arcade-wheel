#include "core/WheelPaths.h"

#include <ArcadeLink.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QStringList>
#include <algorithm>

namespace WheelPaths {
namespace {
const QString kAppId = QStringLiteral("arcade.wheel");
}

void applyApplicationIdentity()
{
    // Qt's folders are <organisation>/<application> under the XDG base
    // directories: "arcade" and "wheel" give ~/.cache/arcade/wheel, the folder
    // dirs() names. The window title and tray text say "Arcade Wheel" themselves.
    QCoreApplication::setOrganizationName(QStringLiteral("arcade"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("arcadewheel.org"));
    QCoreApplication::setApplicationName(QStringLiteral("wheel"));
}

ArcadeLink::AppDirs dirs() { return ArcadeLink::appDirs(kAppId); }
QString settingsFile() { return dirs().settingsFile(); }
QString cacheDir() { return dirs().cache; }
QString logsDir() { return dirs().cache + QStringLiteral("/logs"); }

QString readableSettingsFile()
{
    const QString current = settingsFile();
    if (QFile::exists(current)) return current;
    for (const auto &legacy : ArcadeLink::legacyDirs(kAppId)) {
        const QString file = QDir(legacy.path).filePath(QStringLiteral("config.json"));
        if (legacy.kind == u"config" && QFile::exists(file)) return file;
    }
    return current;
}

namespace {
// Qt's pipeline cache and the QML cache are rebuilt on demand. When the move
// kept the old cache folder because the new one already has files (a 0.5.0
// install where Qt had written both), the old copy is simply deleted. A folder
// holding anything else, such as logs, stays where it is.
bool retireRebuildableCache(const QList<ArcadeLink::LegacyDir> &legacy, const QString &path)
{
    const auto isCache = std::any_of(legacy.begin(), legacy.end(), [&](const auto &dir) { return dir.kind == u"cache" && dir.path == path; });
    const QFileInfo info(path);
    if (!isCache || !info.isDir() || info.isSymLink()) return false;
    const QDir dir(path);
    for (const QString &entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
        if (!entry.startsWith(QLatin1String("qtpipelinecache")) && !entry.startsWith(QLatin1String("qmlcache"))) return false;
    if (!QDir(path).removeRecursively()) return false;
    QDir().rmdir(info.absolutePath()); // the "Arcade Wheel" folder above, when it is empty now
    return true;
}
} // namespace

QString migrate() { return migrate(ArcadeLink::UserDirs::discover(), ArcadeLink::currentPlatform()); }

QString migrate(const ArcadeLink::UserDirs &user, const QString &platform)
{
    QStringList kept;
    const auto report = ArcadeLink::migrateAppDirs(user, platform, kAppId);
    const QList<ArcadeLink::LegacyDir> legacy = ArcadeLink::legacyDirs(user, platform, kAppId);
    for (const auto &[path, reason] : report.kept) {
        if (retireRebuildableCache(legacy, path)) continue;
        kept << path + QStringLiteral(": ") + reason;
    }
    // Before 0.5 the file was config.json; the standard name is settings.json.
    const auto target = ArcadeLink::appDirs(user, platform, kAppId);
    const QString legacyFile = QDir(target.config).filePath(QStringLiteral("config.json"));
    if (QFile::exists(legacyFile)) {
        if (QFile::exists(target.settingsFile()))
            kept << legacyFile + QStringLiteral(": settings.json already exists");
        else if (!QFile::rename(legacyFile, target.settingsFile()))
            kept << legacyFile + QStringLiteral(": couldn't rename it to settings.json");
    }
    if (kept.isEmpty()) return {};
    return QStringLiteral("Couldn't move all of Arcade Wheel's settings to the new folder. The originals were kept. ") + kept.join(QStringLiteral("; "));
}

} // namespace WheelPaths
