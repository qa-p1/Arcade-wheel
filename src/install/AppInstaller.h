#pragma once

#include "link/qt/ArcadeInstall.h"
#include <QStringList>

// Wheel's UI/CLI policy around Link's shared installation engine. File and
// process operations run on a worker, including first-run detection.
class AppInstaller {
public:
    using Engine = ArcadeLink::ArcadeInstall;
    struct Options {
        bool startAtLogin = false;
        bool removeDownload = true;
        QString channel = QStringLiteral("stable");
    };
    static Engine::AppInfo appInfo();
    static QString promptKind(const Engine::Detection &detection, const QString &version);
    static QJsonObject dialogText(const Engine::Detection &detection, const QString &version);
    static bool dismissed(const QString &path, const QString &configDirectory);
    static bool dismiss(const QString &path, const QString &configDirectory, QString *error = nullptr);
    static QString installMethod(); // read-only, also used by --arcade-manifest
    static bool installCopy(const Engine &engine, const Engine::Runtime &runtime,
                            const Options &options, const QString &version,
                            QJsonObject *receipt, QString *error);
    static bool setStartOnLogin(const Engine &engine, bool enabled, QString *error);
    static bool repairDesktop(const Engine &engine, QString *error);
    // Returns -1 for ordinary app commands; otherwise the CLI exit status.
    static int command(const QStringList &arguments, const QString &version);
    // Called before creating the resident UI. True means continue this copy.
    static bool firstRun(const QStringList &arguments, const QString &version, bool noPrompt = false);
};
