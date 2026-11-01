#include "install/AppInstaller.h"
#include "config/ConfigStore.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace ArcadeLink;
class AppInstallerTest final : public QObject {
    Q_OBJECT
private slots:
    void sharedDialogStringsAndVersionDecisions()
    {
        AppInstaller::Engine::Detection d; d.state = "notInstalled"; d.isAppImage = true;
        auto text = AppInstaller::dialogText(d, "0.3.0");
#ifdef Q_OS_MACOS
        d.runningFrom = "/Volumes/Wheel/Arcade Wheel.app";
        QCOMPARE(AppInstaller::promptKind(d, "0.3.0"), QStringLiteral("macos"));
        QCOMPARE(AppInstaller::dialogText(d, "0.3.0").value("title").toString(), QStringLiteral("Move Arcade Wheel to Applications?"));
#endif
        QCOMPARE(text.value("title").toString(), QStringLiteral("Install Arcade Wheel?"));
        QCOMPARE(text.value("buttons"), AppInstaller::Engine::strings().value("notInstalled").toObject().value("buttons"));
        d.state = "installedElsewhere"; d.receipt = {{"version", "0.2.0"}};
        QCOMPARE(AppInstaller::promptKind(d, "0.3.0"), QStringLiteral("older"));
        QCOMPARE(AppInstaller::dialogText(d, "0.3.0").value("title").toString(), QStringLiteral("Update Arcade Wheel 0.2.0 → 0.3.0?"));
        d.receipt.insert("version", "0.3.0");
        QCOMPARE(AppInstaller::promptKind(d, "0.3.0"), QStringLiteral("sameOrNewer"));
        d.receipt.insert("version", "0.10.0");
        // An older file says a newer copy is installed (numeric, not string, order).
        QCOMPARE(AppInstaller::promptKind(d, "0.9.0"), QStringLiteral("newerInstalled"));
        QCOMPARE(AppInstaller::dialogText(d, "0.9.0").value("title").toString(), QStringLiteral("A newer Arcade Wheel is already installed."));
        QCOMPARE(AppInstaller::dialogText(d, "0.9.0").value("body").toString(), QStringLiteral("The installed copy is 0.10.0; this file is the older 0.9.0."));
        d.state = "installedHere"; QVERIFY(AppInstaller::promptKind(d, "0.3.0").isEmpty());
        d.state = "devBuild"; QVERIFY(AppInstaller::promptKind(d, "0.3.0").isEmpty());
    }
    void dismissalBelongsToThisExactFile()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path = dir.filePath("download.AppImage");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("one"); file.close();
        const QString config = dir.filePath("settings"); QString error;
        QVERIFY(!AppInstaller::dismissed(path, config));
        QVERIFY2(AppInstaller::dismiss(path, config, &error), qPrintable(error));
        QVERIFY(AppInstaller::dismissed(path, config));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append)); file.write("changed"); file.close();
        QVERIFY(!AppInstaller::dismissed(path, config));
        QVERIFY2(AppInstaller::dismiss(path, config, &error), qPrintable(error));
        QVERIFY(AppInstaller::dismissed(path, config));
        QFile saved(QDir(config).filePath("install-dismissed.json")); QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).array().size(), 1);
#ifdef Q_OS_UNIX
        QVERIFY(!QFile::permissions(saved.fileName()).testFlag(QFileDevice::ReadOther));
#endif
    }
    void suppressedStartsAndDevBuildsNeverPrompt()
    {
        AppInstaller::Engine::Detection d; d.state = "notInstalled"; d.isAppImage = true;
        for (const auto &arg : {"--background", "--arcade-invoke", "--arcade-manifest", "--version", "--install", "--uninstall", "--repair", "--integration-status"})
            QVERIFY(!AppInstaller::Engine::shouldPrompt(d, {QString::fromLatin1(arg)}));
        QVERIFY(!AppInstaller::Engine::shouldPrompt(d, {}, true));
        QVERIFY(!AppInstaller::Engine::shouldPrompt(d, {}, false, true));
        d.state = "devBuild"; QVERIFY(!AppInstaller::Engine::shouldPrompt(d, {}));
        const auto info = AppInstaller::appInfo();
        QCOMPARE(info.desktopId, QStringLiteral("dev.arcade.Wheel"));
        QCOMPARE(info.autostartId, QStringLiteral("dev.arcade.Wheel"));
        QCOMPARE(info.legacyDesktopIds, (QStringList{"com.arcadewheel.ArcadeWheel", "arcade-wheel"}));
        QCOMPARE(info.backgroundArgs, QStringList{"--background"});
    }
    void cliValidatesOptionsWithoutMutation()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto previous = qgetenv("ARCADE_HOME"); qputenv("ARCADE_HOME", dir.path().toUtf8());
        QCOMPARE(AppInstaller::command({"--repair", "--remove-data"}, "0.3.0"), 2);
        QCOMPARE(AppInstaller::command({"--install", "--channel"}, "0.3.0"), 2);
        QCOMPARE(AppInstaller::command({"--install", "--channel", "unknown"}, "0.3.0"), 2);
        QCOMPARE(AppInstaller::command({"--uninstall", "--no-launch"}, "0.3.0"), 2);
        QCOMPARE(AppInstaller::command({"--integration-status"}, "0.3.0"), 0);
        QCOMPARE(AppInstaller::command({"--install", "--silent", "--no-launch"}, "0.3.0"), 1); // source-tree executable
        QVERIFY(!QFileInfo::exists(ReceiptStore(Locations::under(dir.path())).path(Ids::Wheel)));
        if (previous.isNull()) qunsetenv("ARCADE_HOME"); else qputenv("ARCADE_HOME", previous);
    }
    void wheelInstallRepairAndUninstall()
    {
#ifndef Q_OS_LINUX
        QSKIP("Linux installation engine; native receipt/move/shim helpers are covered by ReleasedLinkInstall");
#else
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto env = AppInstaller::Engine::Environment::under(dir.path()); AppInstaller::Engine engine(env);
        const QString source = dir.filePath("Downloads/Wheel.AppImage"), executable = dir.filePath("mount/usr/bin/arcade-wheel");
        QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath())); QVERIFY(QDir().mkpath(QFileInfo(executable).absolutePath()));
        for (const auto &path : {source, executable}) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("#!/bin/sh\nexit 0\n"); f.close(); }
        AppInstaller::Engine::Runtime runtime{executable, source, dir.filePath("mount"), false};
        AppInstaller::Options options; options.removeDownload = false; options.startAtLogin = true;
        QJsonObject receipt; QString error;
        QVERIFY2(AppInstaller::installCopy(engine, runtime, options, "0.3.0", &receipt, &error), qPrintable(error));
        QCOMPARE(receipt.value("method").toString(), QStringLiteral("appimage"));
        const auto integration = receipt.value("integration").toObject();
        QVERIFY(integration.value("desktopEntry").toString().endsWith("/applications/dev.arcade.Wheel.desktop"));
        QVERIFY(integration.value("autostart").toString().endsWith("/autostart/dev.arcade.Wheel.desktop"));
        QVERIFY(QFile::remove(integration.value("desktopEntry").toString()));
        QVERIFY2(engine.repair(AppInstaller::appInfo(), {}, nullptr, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(integration.value("desktopEntry").toString()));
        receipt.insert("toolsExtension", QJsonObject{{"keep", true}});
        auto extended = receipt.value("integration").toObject();
        extended.insert("futureIntegration", "keep"); receipt.insert("integration", extended);
        QVERIFY2(env.receipts().write(receipt, &error), qPrintable(error));
        for (const bool enabled : {false, true, false, true}) {
            QVERIFY2(AppInstaller::setStartOnLogin(engine, enabled, &error), qPrintable(error));
            QJsonObject changed;
            QVERIFY2(env.receipts().read(Ids::Wheel, &changed, &error), qPrintable(error));
            QCOMPARE(changed.value("toolsExtension"), receipt.value("toolsExtension"));
            QCOMPARE(changed.value("integration").toObject().value("futureIntegration"), QJsonValue("keep"));
            QCOMPARE(QFileInfo::exists(integration.value("autostart").toString()), enabled);
            QCOMPARE(changed.value("integration").toObject().value("autostart").isString(), enabled);
        }
        AppInstaller::Engine::UninstallOptions uninstall;
        QVERIFY2(engine.uninstall(Ids::Wheel, uninstall, nullptr, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(receipt.value("path").toString()));
        QVERIFY(!QFileInfo::exists(integration.value("autostart").toString()));
        QVERIFY(QFileInfo::exists(source));
#endif
    }
    // A copy installed before 0.5 (launcher com.arcadewheel.ArcadeWheel, login
    // entry arcade-wheel) moves to dev.arcade.Wheel on its first start, keeps
    // start at login on or off, and leaves no old entry behind.
    void integrationMovesToTheNewIdOnce_data()
    {
        QTest::addColumn<bool>("startAtLogin");
        QTest::addRow("starting at login") << true;
        QTest::addRow("not starting at login") << false;
    }
    void integrationMovesToTheNewIdOnce()
    {
#ifndef Q_OS_LINUX
        QSKIP("Linux launchers and login entries");
#else
        QFETCH(bool, startAtLogin);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto env = AppInstaller::Engine::Environment::under(dir.path()); AppInstaller::Engine engine(env);
        const QString source = dir.filePath("Downloads/Wheel.AppImage"), executable = dir.filePath("mount/usr/bin/arcade-wheel");
        QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath())); QVERIFY(QDir().mkpath(QFileInfo(executable).absolutePath()));
        for (const auto &path : {source, executable}) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("#!/bin/sh\nexit 0\n"); f.close(); }
        // Installed the way 0.4 did it.
        AppInstaller::Engine::InstallOptions old;
        old.app = AppInstaller::appInfo();
        old.app.desktopId = QStringLiteral("com.arcadewheel.ArcadeWheel");
        old.app.legacyDesktopIds = {QStringLiteral("arcade-wheel")};
        old.app.autostartId = QStringLiteral("arcade-wheel");
        old.runtime = {executable, source, dir.filePath("mount"), false};
        old.version = QStringLiteral("0.4.5");
        old.startAtLogin = startAtLogin;
        QJsonObject receipt; QString error;
        QVERIFY2(engine.install(old, &receipt, &error), qPrintable(error));
        receipt.insert("toolsExtension", QJsonObject{{"keep", true}});
        QVERIFY2(env.receipts().write(receipt, &error), qPrintable(error));
        const QString oldEntry = receipt.value("integration").toObject().value("desktopEntry").toString();
        const QString oldLogin = QDir(env.config).filePath("autostart/arcade-wheel.desktop");
        QVERIFY(oldEntry.endsWith("/com.arcadewheel.ArcadeWheel.desktop"));
        QCOMPARE(QFileInfo::exists(oldLogin), startAtLogin);

        // The installed copy starts (0.5).
        const QString installed = receipt.value("path").toString();
        const AppInstaller::Engine::Runtime running{executable, installed, dir.filePath("mount"), false};
        QVERIFY2(AppInstaller::migrateIntegration(env, running, &error), qPrintable(error));
        QJsonObject moved;
        QVERIFY2(env.receipts().read(Ids::Wheel, &moved, &error), qPrintable(error));
        const auto integration = moved.value("integration").toObject();
        QVERIFY(integration.value("desktopEntry").toString().endsWith("/applications/dev.arcade.Wheel.desktop"));
        QVERIFY(QFileInfo::exists(integration.value("desktopEntry").toString()));
        QVERIFY(!QFileInfo::exists(oldEntry));
        QVERIFY(!QFileInfo::exists(oldLogin));
        const QString newLogin = QDir(env.config).filePath("autostart/dev.arcade.Wheel.desktop");
        QCOMPARE(QFileInfo::exists(newLogin), startAtLogin);
        QCOMPARE(integration.value("autostart").isString(), startAtLogin);
        QCOMPARE(moved.value("toolsExtension"), receipt.value("toolsExtension"));
        QCOMPARE(moved.value("path").toString(), installed);

        // A second start finds nothing to move.
        const QByteArray before = QFile(env.receipts().path(Ids::Wheel)).exists() ? [&] {
            QFile f(env.receipts().path(Ids::Wheel)); f.open(QIODevice::ReadOnly); return f.readAll(); }() : QByteArray();
        QVERIFY2(AppInstaller::migrateIntegration(env, running, &error), qPrintable(error));
        QFile after(env.receipts().path(Ids::Wheel)); QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before);
#endif
    }
};
QTEST_MAIN(AppInstallerTest)
#include "tst_AppInstaller.moc"
