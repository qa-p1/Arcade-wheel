#include "core/ResidentStart.h"
#include <QtTest>

class ResidentStartTest final : public QObject {
    Q_OBJECT
private slots:
    void keepsResident_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<bool>("resident");
        // Starts that become or keep the resident Wheel.
        QTest::newRow("no arguments (Settings)") << QStringList{} << true;
        QTest::newRow("--settings") << QStringList{"--settings"} << true;
        QTest::newRow("--background") << QStringList{"--background"} << true;
        QTest::newRow("--restarting") << QStringList{"--restarting"} << true;
        // One-shot commands that never keep a resident, and whose exit codes callers read.
        for (const char *command : {"--status", "--version", "-V", "--arcade-manifest", "--menu", "--help", "-h", "--smoke-test",
                                    "--install", "--uninstall", "--repair", "--integration-status"})
            QTest::newRow(command) << QStringList{command} << false;
        QTest::newRow("--install --silent --no-launch") << QStringList{"--install", "--silent", "--no-launch"} << false;
        QTest::newRow("--uninstall --silent --remove-data") << QStringList{"--uninstall", "--silent", "--remove-data"} << false;
        QTest::newRow("--settings-export FILE") << QStringList{"--settings-export", "wheel.json"} << false;
        QTest::newRow("--settings-import FILE") << QStringList{"--settings-import", "wheel.json"} << false;
        // Commands that only drive a running Wheel.
        for (const char *command : {"--show", "--preview", "--hide", "--cancel", "--toggle", "--press", "--release", "--restart", "--quit"})
            QTest::newRow(command) << QStringList{command} << false;
        // Usage errors exit 2 and must not start anything.
        QTest::newRow("unknown") << QStringList{"--nope"} << false;
        QTest::newRow("--background with an extra argument") << QStringList{"--background", "extra"} << false;
        QTest::newRow("--settings with an extra argument") << QStringList{"--settings", "--background"} << false;
        QTest::newRow("empty argument") << QStringList{QString()} << false;
    }
    void keepsResident()
    {
        QFETCH(QStringList, arguments);
        QFETCH(bool, resident);
        QCOMPARE(ResidentStart::keepsResident(arguments), resident);
    }
};

QTEST_APPLESS_MAIN(ResidentStartTest)
#include "tst_ResidentStart.moc"
