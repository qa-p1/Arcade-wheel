#pragma once

#include <ArcadePaths.h>

#include <QString>

// Wheel's folders (STANDARD §10): ~/.config/arcade/wheel/settings.json,
// ~/.cache/arcade/wheel/ and ~/.local/share/arcade/wheel/ on Linux;
// %APPDATA%\Arcade\Wheel on Windows; ~/Library/Application Support/Arcade/Wheel
// on macOS. Before 0.5 they were Qt's "Arcade Wheel/Arcade Wheel" folders.
namespace WheelPaths {

ArcadeLink::AppDirs dirs();
QString settingsFile();
QString cacheDir();
QString logsDir();
// The settings file to read without starting Wheel (--arcade-manifest,
// --settings-export): settings.json, or the config.json from before 0.5
// while Wheel hasn't started since the update and moved it.
QString readableSettingsFile();

// Copies the folders from before 0.5 to the new ones, verifies them, then
// removes the old ones (Link's migrateAppDirs), and renames config.json to
// settings.json. Anything that can't move stays where it was. Returns a
// sentence for the log when something was kept, else an empty string.
QString migrate();
QString migrate(const ArcadeLink::UserDirs &user, const QString &platform);

} // namespace WheelPaths
