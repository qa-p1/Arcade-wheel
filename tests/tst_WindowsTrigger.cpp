#include "platform/windows/WindowsBackend.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class WindowsTriggerTest final : public QObject {
    Q_OBJECT
private slots:
    void recorderKeyNamesAreAccepted()
    {
        WindowsBackend backend;
        // Names produced by the Settings recorder (QKeySequence::PortableText).
        for (const char *key : {"F8", "Ctrl+Space", "Meta+Space", "Ctrl+Plus", "PgUp", "PgDown", "Ins", "Del",
                                "Print", "NumLock", "ScrollLock", "Menu", "CapsLock", "Volume Mute",
                                "Volume Up", "Volume Down", "Media Play", "Media Stop", "Media Next",
                                "Media Previous", "Toggle Media Play/Pause"})
            QVERIFY2(backend.shortcutValidationError(QString::fromLatin1(key)).isEmpty(), key);
    }

    void unsupportedShortcutsAreRejectedBeforeSaving()
    {
        WindowsBackend backend;
        QVERIFY(!backend.shortcutValidationError(QStringLiteral("Mouse 4")).isEmpty());
        QVERIFY(!backend.shortcutValidationError(QStringLiteral("Ctrl+A+B")).isEmpty());
        QVERIFY(!backend.shortcutValidationError(QStringLiteral("NotAKey")).isEmpty());
    }

    void workingTriggerReportsReady()
    {
        WindowsBackend backend;
        QSignalSpy status(&backend, &PlatformBackend::triggerStatusChanged);
        backend.configureTrigger(QJsonObject{{QStringLiteral("shortcut"), QStringLiteral("F8")}});
        QVERIFY(!status.isEmpty());
        const QString message = status.last().first().toString();
        if (message.startsWith(QStringLiteral("Could not register")))
            QSKIP("This session cannot install a low-level keyboard hook");
        QVERIFY2(message.startsWith(QStringLiteral("Ready")), qPrintable(message));
    }
};

QTEST_GUILESS_MAIN(WindowsTriggerTest)
#include "tst_WindowsTrigger.moc"
