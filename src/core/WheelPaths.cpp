#include "core/WheelPaths.h"

#include <ArcadeLink.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>

namespace WheelPaths {
namespace {
const QString kAppId = QStringLiteral("arcade.wheel");
}

void applyApplicationIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("Arcade Wheel"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("arcadewheel.org"));
    QCoreApplication::setApplicationName(QStringLiteral("Arcade Wheel"));
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

QString migrate() { return migrate(ArcadeLink::UserDirs::discover(), ArcadeLink::currentPlatform()); }

QString migrate(const ArcadeLink::UserDirs &user, const QString &platform)
{
    QStringList kept;
    const auto report = ArcadeLink::migrateAppDirs(user, platform, kAppId);
    for (const auto &[path, reason] : report.kept) kept << path + QStringLiteral(": ") + reason;
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
