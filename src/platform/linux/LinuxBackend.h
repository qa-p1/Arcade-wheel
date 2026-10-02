#pragma once

#include "../PlatformBackend.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QQuickWindow>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
class QScreen;
class QLocalSocket;
class QDBusMessage;

class LinuxBackend;

// One object per portal Request::Response subscription. Keeping the request
// path on this object avoids confusing late responses after the trigger is
// changed while a portal dialog is still open.
class LinuxPortalRequest final : public QObject {
    Q_OBJECT
public:
    using Completion = std::function<void(uint, const QVariantMap &, const QString &)>;

    LinuxPortalRequest(const QDBusConnection &bus,
                       const QString &portalService,
                       const QString &predictedPath,
                       QObject *parent,
                       Completion completion);
    ~LinuxPortalRequest() override;

    void setRequestPath(const QString &path);
    QString requestPath() const { return m_path; }
    void cancel();

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    bool subscribe(const QString &path);
    void unsubscribe();

    QDBusConnection m_bus;
    QString m_portalService;
    QString m_path;
    Completion m_completion;
    bool m_finished = false;
};

class LinuxBackend final : public PlatformBackend {
    Q_OBJECT
public:
    explicit LinuxBackend(QObject *parent = nullptr);
    ~LinuxBackend() override;

    QString name() const override;
    void configureTrigger(const QJsonObject &trigger) override;
    void setTriggerHoldThreshold(int milliseconds) override;
    bool prepareOverlay(QQuickWindow *window, QString *error) override;
    QPointF cursorPosition() const override;
    bool movePointer(const QPointF &position) override;
    QString shortcutValidationError(const QString &shortcut) const override;
    QString shortcutConflict(const QString &shortcut) const override;
    void setShortcutRecording(bool recording) override;
    QVector<DiscoveredApplication> applications() const override;
    bool launchApplication(const QString &id, bool focusExisting, QString *error) override;
    bool performSystemAction(const QString &id, QString *error) override;
    bool setStartOnLogin(bool enabled, QString *error) override;
    bool startOnLogin() const override;

private slots:
    void onPortalOwnerChanged(const QString &service,
                              const QString &oldOwner,
                              const QString &newOwner);
    void onPortalSessionClosed(const QVariantMap &details, const QDBusMessage &message);
    void onShortcutActivated(const QDBusObjectPath &session,
                             const QString &shortcutId,
                             qulonglong timestamp,
                             const QVariantMap &options);
    void onShortcutDeactivated(const QDBusObjectPath &session,
                               const QString &shortcutId,
                               qulonglong timestamp,
                               const QVariantMap &options);

private:
    friend class LinuxPortalRequest;

    struct DesktopEntry {
        DiscoveredApplication application;
        QString desktopFile;
        QString exec;
        QString workingDirectory;
        QString startupWmClass;
        QString icon;
        bool terminal = false;
        bool dbusActivatable = false;
    };

    enum class PortalRequestKind { CreateSession, BindShortcuts };

    void portalRequestFinished(PortalRequestKind kind,
                               quint64 generation,
                               const QString &requestPath,
                               uint response,
                               const QVariantMap &results);
    void ensurePortalRegistered();
    bool ensureDesktopIdentity(QString *error);
    void startPortalSession();
    void bindPortalShortcut(const QString &sessionHandle, quint64 generation);
    void closePortalSession(const QString &sessionHandle);
    void clearActiveTrigger();
    QString triggerReadyStatus() const;
    bool replayTriggerTap(QString *error) const;
    void updateOverlayScreen();
    bool installHyprlandBinding(QString *error);
    void removeHyprlandBinding();
    void watchHyprlandReloads();
    QLocalSocket *m_hyprlandEvents = nullptr;
    QByteArray m_hyprlandEventBuffer;
    QString m_boundKey;
    int m_boundModifiers = 0;
    QJsonArray m_displacedBindings;
    bool m_bindingInstalled = false;
    bool m_luaBinding = false;
    bool m_displacedWithLua = false;
    bool m_recordingShortcut = false;
    bool m_overlayRuleInstalled = false;

    static QString preferredTriggerFromShortcut(const QString &shortcut, QString *error);

    QVector<DesktopEntry> scanDesktopEntries() const;
    DesktopEntry readDesktopEntry(const QString &path, const QString &desktopId) const;
    bool launchDesktopEntry(const DesktopEntry &entry, bool focusExisting, QString *error);
    bool focusHyprlandWindow(const DesktopEntry &entry) const;

    bool isHyprland() const;
    bool sendHyprlandCommand(const QString &command, QString *reply = nullptr, bool json = false) const;
    QString hyprlandSocketPath() const;
    bool launchDetached(const QString &program,
                        const QStringList &arguments,
                        const QString &workingDirectory,
                        QString *error) const;
    QString autostartDesktopFile() const;

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_portalWatcher = nullptr;
    QPointer<QQuickWindow> m_overlayWindow;
    bool m_overlayMapped = false;
    std::optional<QPointF> m_pendingPointer;
    QString m_portalOwner;
    QString m_sessionHandle;
    QString m_preferredTrigger;
    QString m_shortcutSetting;
    QString m_appId;
    QJsonObject m_triggerConfiguration;
    quint64 m_triggerGeneration = 0;
    quint64 m_requestCounter = 0;
    bool m_portalRegistered = false;
    bool m_registryRequestPending = false;
    bool m_sessionRequestPending = false;
    int m_identityRetries = 0;
    bool m_triggerSupported = false;
    bool m_triggerDown = false;
    int m_holdThresholdMs = 0;
    QElapsedTimer m_triggerClock;
    QSet<LinuxPortalRequest *> m_portalRequests;
    mutable QHash<QString, DesktopEntry> m_desktopEntries;
};
