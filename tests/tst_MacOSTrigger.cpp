#include "platform/macos/MacOSBackend.h"
#include <QSignalSpy>
#include <QtTest>

class MacOSTriggerTest final : public QObject {
    Q_OBJECT
private slots:
    void validatesRecorderShortcuts()
    {
        MacOSBackend backend;
        for (const char *key : {"F8", "Ctrl+Space", "Meta+Space", "Alt+F10", "Shift+Tab", "PgDown"})
            QVERIFY2(backend.shortcutValidationError(QString::fromLatin1(key)).isEmpty(), key);
        for (const char *key : {"", "Mouse 4", "Ctrl+A, Ctrl+B", "NotAKey", "Volume Mute"})
            QVERIFY2(!backend.shortcutValidationError(QString::fromLatin1(key)).isEmpty(), key);
    }
    void headlessSessionDoesNotRegisterShortcut()
    {
        qputenv("ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT", "1");
        MacOSBackend backend;
        QSignalSpy status(&backend, &PlatformBackend::triggerStatusChanged);
        backend.configureTrigger({{QStringLiteral("shortcut"), QStringLiteral("F8")}});
        QCOMPARE(status.size(), 1);
        QVERIFY(status.first().first().toString().contains(QStringLiteral("disabled")));
        qunsetenv("ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT");
    }
};
QTEST_GUILESS_MAIN(MacOSTriggerTest)
#include "tst_MacOSTrigger.moc"
