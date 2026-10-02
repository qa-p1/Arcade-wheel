#include "actions/ActionDispatcher.h"

#include <QQuickWindow>
#include <QtTest>

class FakePlatformBackend final : public PlatformBackend {
public:
    QString name() const override { return QStringLiteral("fake"); }
    void configureTrigger(const QJsonObject &) override {}
    bool prepareOverlay(QQuickWindow *, QString *) override { return true; }
    QPointF cursorPosition() const override { return {}; }
    QVector<DiscoveredApplication> applications() const override { return availableApplications; }

    bool launchApplication(const QString &id, bool focusExisting, QString *error) override
    {
        ++launchCalls;
        launchedId = id;
        lastFocusExisting = focusExisting;
        if (!launchResult && error)
            *error = QStringLiteral("Platform rejected launch");
        return launchResult;
    }

    bool performSystemAction(const QString &id, QString *) override
    {
        ++systemActionCalls;
        lastSystemAction = id;
        return systemActionResult;
    }

    bool setStartOnLogin(bool, QString *) override { return true; }
    bool startOnLogin() const override { return false; }

    QVector<DiscoveredApplication> availableApplications;
    int launchCalls = 0;
    int systemActionCalls = 0;
    QString launchedId;
    QString lastSystemAction;
    bool lastFocusExisting = false;
    bool launchResult = true;
    bool systemActionResult = true;
};

class ActionDispatcherTest final : public QObject {
    Q_OBJECT

private slots:
    void unavailableReasonsAreReported()
    {
        FakePlatformBackend backend;
        ActionDispatcher dispatcher(&backend);

        QCOMPARE(dispatcher.unavailableReason({{QStringLiteral("type"), QStringLiteral("none")}}),
                 QStringLiteral("Choose an action"));
        QCOMPARE(dispatcher.unavailableReason(
                     {{QStringLiteral("type"), QStringLiteral("application")},
                      {QStringLiteral("payload"), QJsonObject{{QStringLiteral("desktopId"), QStringLiteral("missing.desktop")}}}}),
                 QStringLiteral("Application is not installed"));
        QCOMPARE(dispatcher.unavailableReason(
                     {{QStringLiteral("type"), QStringLiteral("command")},
                      {QStringLiteral("payload"), QJsonObject{{QStringLiteral("command"), QStringLiteral("   ")}}}}),
                 QStringLiteral("Command is empty"));
        QCOMPARE(dispatcher.unavailableReason(
                     {{QStringLiteral("type"), QStringLiteral("plugin")},
                      {QStringLiteral("payload"), QJsonObject{}}}),
                 QStringLiteral("Provider is unavailable"));
        QCOMPARE(dispatcher.unavailableReason({{QStringLiteral("type"), QStringLiteral("unsupported")}}),
                 QStringLiteral("Unknown action type"));
    }

    void unavailableExecutionDoesNotReachTheBackend()
    {
        FakePlatformBackend backend;
        ActionDispatcher dispatcher(&backend);
        const QJsonObject missingApplication{
            {QStringLiteral("type"), QStringLiteral("application")},
            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("desktopId"), QStringLiteral("missing.desktop")}}}};

        QString error;
        QVERIFY(!dispatcher.execute(missingApplication, true, &error));
        QCOMPARE(error, QStringLiteral("Application is not installed"));
        QCOMPARE(backend.launchCalls, 0);

        error.clear();
        QVERIFY(!dispatcher.execute({{QStringLiteral("type"), QStringLiteral("none")}}, true, &error));
        QCOMPARE(error, QStringLiteral("Choose an action"));
        QCOMPARE(backend.launchCalls, 0);
        QCOMPARE(backend.systemActionCalls, 0);
    }

    void installedApplicationIsDispatchedWithFocusPreference()
    {
        FakePlatformBackend backend;
        backend.availableApplications.append({QStringLiteral("org.example.editor.desktop"),
                                              QStringLiteral("Editor"), {}, {}, {}});
        ActionDispatcher dispatcher(&backend);
        dispatcher.setApplications(backend.availableApplications);

        const QJsonObject action{
            {QStringLiteral("type"), QStringLiteral("application")},
            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("desktopId"), QStringLiteral("org.example.editor.desktop")}}}};
        QString error;
        QVERIFY2(dispatcher.execute(action, false, &error), qPrintable(error));
        QCOMPARE(backend.launchCalls, 1);
        QCOMPARE(backend.launchedId, QStringLiteral("org.example.editor.desktop"));
        QVERIFY(!backend.lastFocusExisting);
        QVERIFY(error.isEmpty());
    }

    void failedBackendLaunchReturnsFailure()
    {
        FakePlatformBackend backend;
        backend.availableApplications.append({QStringLiteral("org.example.editor.desktop"),
                                              QStringLiteral("Editor"), {}, {}, {}});
        backend.launchResult = false;
        ActionDispatcher dispatcher(&backend);
        dispatcher.setApplications(backend.availableApplications);
        const QJsonObject action{
            {QStringLiteral("type"), QStringLiteral("application")},
            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("desktopId"), QStringLiteral("org.example.editor.desktop")}}}};

        QString error;
        QVERIFY(!dispatcher.execute(action, true, &error));
        QCOMPARE(backend.launchCalls, 1);
        QCOMPARE(error, QStringLiteral("Platform rejected launch"));
    }
};

QTEST_APPLESS_MAIN(ActionDispatcherTest)
#include "tst_ActionDispatcher.moc"
