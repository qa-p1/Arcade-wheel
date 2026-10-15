#include "core/IconTheme.h"

#include <QDir>
#include <QFile>
#include <QIcon>
#include <QTemporaryDir>
#include <QtTest>

class IconThemeTest final : public QObject {
    Q_OBJECT
    const QString root = QStringLiteral(ARCADE_ICON_THEME_FIXTURES);
    QString config() const { return root + QStringLiteral("/config"); }
    QStringList icons() const { return {QStringLiteral(":/not-a-user-theme"), root + QStringLiteral("/icons")}; }
    IconTheme::Sources sources(const QString &platform, const QString &gsettings = {}, int *calls = nullptr) const {
        IconTheme::Sources s;
        s.platformTheme = platform;
        s.configHome = config();
        s.searchPaths = icons();
        s.gsettings = [gsettings, calls] { if (calls) ++*calls; return gsettings; };
        return s;
    }
private slots:
    void readsEachConfigFormat() {
        QCOMPARE(IconTheme::readQtEngine(config() + "/qtengine/config.json"), QStringLiteral("Papirus-Dark"));
        QCOMPARE(IconTheme::readIniValue(config() + "/qt6ct/qt6ct.conf", "Appearance", "icon_theme"), QStringLiteral("breeze"));
        QCOMPARE(IconTheme::readIniValue(config() + "/kdeglobals", "Icons", "Theme"), QStringLiteral("Oxygen"));
        QCOMPARE(IconTheme::readIniValue(config() + "/gtk-3.0/settings.ini", "Settings", "gtk-icon-theme-name"), QStringLiteral("Adwaita"));
        QCOMPARE(IconTheme::readIniValue(config() + "/missing.conf", "Icons", "Theme"), QString());
        QCOMPARE(IconTheme::parseGsettings("'Papirus-Dark'\n"), QStringLiteral("Papirus-Dark"));
        QCOMPARE(IconTheme::parseGsettings(""), QString());
    }
    void acceptsOnlyInstalledThemes() {
        QVERIFY(IconTheme::isInstalled("Papirus-Dark", icons()));
        QVERIFY(!IconTheme::isInstalled("NoIndex", icons()));
        QVERIFY(!IconTheme::isInstalled("Missing", icons()));
        QVERIFY(!IconTheme::isInstalled("../icons/breeze", icons()));
    }
    void platformThemeConfigWinsOverStaleFiles() {
        int calls = 0;
        // The owner's setup: qtengine is current, qt6ct and GTK are stale.
        QCOMPARE(IconTheme::detect(sources("qtengine", "'Adwaita'", &calls)), QStringLiteral("Papirus-Dark"));
        QCOMPARE(IconTheme::detect(sources("qt6ct", "'Adwaita'", &calls)), QStringLiteral("breeze"));
        QCOMPARE(IconTheme::detect(sources("kde", "'Adwaita'", &calls)), QStringLiteral("Oxygen"));
        QCOMPARE(calls, 0); // gsettings is not started when it is not needed
    }
    void unsetPlatformThemeUsesQtEngineWhenPresent() {
        QCOMPARE(IconTheme::detect(sources({}, "'Adwaita'")), QStringLiteral("Papirus-Dark"));
    }
    void fallsBackThroughGsettingsKdeAndGtk() {
        QCOMPARE(IconTheme::detect(sources("gtk3", "'breeze'\n")), QStringLiteral("breeze"));
        // Not installed or hicolor: keep looking.
        QCOMPARE(IconTheme::detect(sources("gtk3", "'NoIndex'")), QStringLiteral("Oxygen"));
        QCOMPARE(IconTheme::detect(sources("gtk3", "'hicolor'")), QStringLiteral("Oxygen"));

        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("gtk-3.0"));
        QVERIFY(QFile::copy(config() + "/gtk-3.0/settings.ini", dir.filePath("gtk-3.0/settings.ini")));
        auto gtkOnly = sources("qt6ct");
        gtkOnly.configHome = dir.path();
        QCOMPARE(IconTheme::detect(gtkOnly), QStringLiteral("Adwaita"));

        QTemporaryDir empty;
        auto nothing = sources({});
        nothing.configHome = empty.path();
        QCOMPARE(IconTheme::detect(nothing), QString());
    }
    void appliesOnlyOverHicolor() {
        qputenv("XDG_CONFIG_HOME", config().toUtf8());
        qputenv("QT_QPA_PLATFORMTHEME", "qtengine");
        QIcon::setThemeSearchPaths(icons());
        QIcon::setThemeName("breeze");
        QCOMPARE(IconTheme::applyUserTheme(), QString());
        QCOMPARE(QIcon::themeName(), QStringLiteral("breeze"));
        QIcon::setThemeName("hicolor");
        QCOMPARE(IconTheme::applyUserTheme(), QStringLiteral("Papirus-Dark"));
        QCOMPARE(QIcon::themeName(), QStringLiteral("Papirus-Dark"));
        QCOMPARE(QIcon::fallbackThemeName(), QStringLiteral("hicolor"));
    }
};

QTEST_MAIN(IconThemeTest)
#include "tst_IconTheme.moc"
