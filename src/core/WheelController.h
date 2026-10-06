#pragma once

#include "actions/ActionDispatcher.h"
#include "config/ConfigStore.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QQuickView;

class WheelController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    Q_PROPERTY(QVariantMap currentDeck READ currentDeck NOTIFY currentDeckChanged)
    Q_PROPERTY(int currentDeckIndex READ currentDeckIndex NOTIFY currentDeckChanged)
    Q_PROPERTY(int deckCount READ deckCount NOTIFY configChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(qreal visualCenterX READ visualCenterX NOTIFY geometryChanged)
    Q_PROPERTY(qreal visualCenterY READ visualCenterY NOTIFY geometryChanged)
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY overlayVisibleChanged)
    Q_PROPERTY(bool overlayRevealed READ overlayRevealed NOTIFY overlayRevealedChanged)
    Q_PROPERTY(bool centerGesturePressed READ centerGesturePressed NOTIFY centerGestureChanged)
    Q_PROPERTY(QString shortcutConflict READ shortcutConflict NOTIFY shortcutConflictChanged)
    Q_PROPERTY(bool shortcutPeerConflict READ shortcutPeerConflict NOTIFY shortcutConflictChanged)
    Q_PROPERTY(QVariantList applications READ applications NOTIFY applicationsChanged)
    Q_PROPERTY(QVariantList arcadeActions READ arcadeActions NOTIFY providersChanged)
    Q_PROPERTY(QVariantList linkJobs READ linkJobs NOTIFY linkJobsChanged)
    Q_PROPERTY(QVariantList connectedApps READ connectedApps NOTIFY providersChanged)
    Q_PROPERTY(QVariantMap linkDiagnostics READ linkDiagnostics NOTIFY linkDiagnosticsChanged)
    Q_PROPERTY(QString triggerStatus READ triggerStatus NOTIFY triggerStatusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    // Arcade Link: an action another app asked to add, waiting for the user
    // to choose a slot and save it.
    Q_PROPERTY(QVariantMap linkDraft READ linkDraft NOTIFY linkDraftChanged)
    Q_PROPERTY(QString linkSource READ linkSource NOTIFY linkDraftChanged)
public:
    explicit WheelController(PlatformBackend *backend, QObject *parent = nullptr, QString configPath = {});
    ~WheelController() override;
    void initialize();
    bool flushPendingSave();
    void setOverlayView(QQuickView *view);

    QVariantMap config() const { return m_config.toVariantMap(); }
    QVariantMap currentDeck() const;
    int currentDeckIndex() const { return m_deckIndex; }
    int deckCount() const;
    int selectedIndex() const { return m_selectedIndex; }
    qreal visualCenterX() const { return m_visualCenter.x(); }
    qreal visualCenterY() const { return m_visualCenter.y(); }
    bool overlayVisible() const { return m_visible; }
    bool overlayRevealed() const { return m_revealed; }
    bool centerGesturePressed() const { return m_centerPressed; }
    QString shortcutConflict() const { return m_shortcutConflict; }
    bool shortcutPeerConflict() const { return m_shortcutPeerConflict; }
    QVariantList applications() const;
    QVariantList arcadeActions() const { return m_dispatcher.arcade().tools(); }
    QVariantList linkJobs() const { return m_linkJobs; }
    QVariantList connectedApps() const { return m_dispatcher.arcade().connectedApps(); }
    QString peerDisplayName(const QString &app) const;
    QVariantMap linkDiagnostics() const;
    void setLinkDiagnostics(const QString &endpoint, const QString &error);
    QString triggerStatus() const { return m_triggerStatus; }
    QString lastError() const { return m_lastError; }
    QString configPath() const { return m_store.path(); }
    QVariantMap linkDraft() const { return m_linkDraft; }
    QString linkSource() const { return m_linkSource; }
    QObject *arcadeWorker() const { return m_dispatcher.arcade().ioWorker(); }
    bool linkPending() const { return !m_linkDraft.isEmpty(); }
    // Starts a pending add (false if one is already waiting) and opens Settings.
    bool beginLinkAction(const QVariantMap &draft, const QString &sourceName);
    // The user saved the draft into a slot, or gave up (Cancel, closing Settings).
    Q_INVOKABLE void finishLinkAction(bool saved, const QString &where = {});

    Q_INVOKABLE void pressTrigger();
    Q_INVOKABLE void releaseTrigger();
    Q_INVOKABLE void previewOverlay();
    Q_INVOKABLE void showWheel();
    Q_INVOKABLE void activateSlot(int index);
    Q_INVOKABLE void finishClose();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE QString shortcutFromKey(int key, int modifiers) const;
    Q_INVOKABLE bool applyShortcut(const QString &shortcut, bool overrideConflict = false);
    Q_INVOKABLE void setShortcutRecording(bool recording);
    Q_INVOKABLE void cancelTrigger();
    Q_INVOKABLE void pointerMoved(qreal localX, qreal localY);
    Q_INVOKABLE bool centerPressed(qreal x, qreal y);
    Q_INVOKABLE void centerReleased(qreal x, qreal y);
    Q_INVOKABLE void cancelCenterGesture();
    Q_INVOKABLE void setCenterGesture(const QString &gesture, const QString &name, bool enabled);
    Q_INVOKABLE void setCenterGestureAction(const QString &gesture, int slot, const QVariantMap &action);
    Q_INVOKABLE void removeCenterGestureAction(const QString &gesture, int slot);
    Q_INVOKABLE void moveCenterGestureAction(const QString &gesture, int from, int to);
    Q_INVOKABLE void scrollDeck(int steps);
    Q_INVOKABLE void selectDeck(int index);
    Q_INVOKABLE void updateSetting(const QString &section, const QString &key, const QVariant &value);
    Q_INVOKABLE void setDeckName(const QString &deckId, const QString &name);
    Q_INVOKABLE void createDeck();
    Q_INVOKABLE void duplicateDeck(const QString &deckId);
    Q_INVOKABLE void deleteDeck(const QString &deckId);
    Q_INVOKABLE void moveDeck(int from, int to);
    Q_INVOKABLE void resizeDeck(const QString &deckId, int size);
    Q_INVOKABLE bool setAction(const QString &deckId, int slot, const QVariantMap &action);
    Q_INVOKABLE void moveAction(const QString &deckId, int from, int to);
    Q_INVOKABLE QString actionUnavailableReason(const QVariantMap &action) const;
    Q_INVOKABLE void refreshApplications();
    Q_INVOKABLE void refreshProviders();
    Q_INVOKABLE void setPeerEnabled(const QString &app, bool enabled);
    Q_INVOKABLE void getArcadeApp(const QString &app);
    Q_INVOKABLE void cancelLinkJob(const QString &job);
    Q_INVOKABLE void dismissLinkJobs();
    Q_INVOKABLE QVariantMap arcadeActionInfo(const QVariantMap &action) const;
    Q_INVOKABLE bool importConfig(const QString &urlOrPath);
    Q_INVOKABLE bool exportConfig(const QString &urlOrPath);
    Q_INVOKABLE void resetDefaults();
    Q_INVOKABLE void requestSettings() { emit settingsRequested(); }
    Q_INVOKABLE void requestQuit() { emit quitRequested(); }
    Q_INVOKABLE void requestRestart() { emit restartRequested(); }

signals:
    void centerGestureChanged();
    void configChanged();
    void currentDeckChanged();
    void selectedIndexChanged();
    void geometryChanged();
    void overlayVisibleChanged();
    void overlayRevealedChanged();
    void shortcutConflictChanged();
    void applicationsChanged();
    void providersChanged();
    void linkJobsChanged();
    void linkDiagnosticsChanged();
    void linkActivityRequested();
    void triggerStatusChanged();
    void lastErrorChanged();
    void settingsRequested();
    void quitRequested();
    void restartRequested();
    void actionFailed(const QString &message);
    void linkDraftChanged();
    void linkActionFinished(bool saved, const QString &where);

private:
    QJsonArray decks() const;
    QJsonObject deckAt(int index) const;
    int deckIndexById(const QString &id) const;
    enum class Persist { Now, Coalesced };
    bool commit(QJsonObject updated, bool triggerChanged = false, Persist persist = Persist::Now);
    void applyStartOnLogin();
    void openOverlay();
    void hideOverlay();
    void executeSelection(int index);
    void executeActions(const QJsonArray &actions);
    bool inCenter(qreal x, qreal y) const;
    bool centerGestureEnabled(const QString &gesture) const;
    void finishCenterClicks();
    void executeCenterGesture(const QString &gesture);
    void updateOverlayGeometry();
    void setSelectedIndex(int index);
    void reportError(const QString &error);
    static QString localPath(const QString &urlOrPath);

    PlatformBackend *m_backend;
    ConfigStore m_store;
    ActionDispatcher m_dispatcher;
    QVector<DiscoveredApplication> m_applications;
    QJsonObject m_config;
    QQuickView *m_overlay = nullptr;
    QTimer m_holdTimer;
    QTimer m_closeTimer;
    QTimer m_centerClickTimer;
    QTimer m_centerHoldTimer;
    QTimer m_saveTimer;
    bool m_savePending = false;
    int m_centerClicks = 0;
    bool m_centerPressed = false;
    QElapsedTimer m_openClock;
    QElapsedTimer m_scrollClock;
    QElapsedTimer m_triggerPressClock;
    QPointF m_origin;
    QPointF m_visualCenter;
    QPointF m_lastPointer;
    int m_deckIndex = 0;
    int m_selectedIndex = -1;
    bool m_pressed = false;
    enum class Mode { Hold, Click, Preview };
    Mode m_mode = Mode::Hold;
    bool m_waitingForCenter = false;
    bool m_visible = false;
    bool m_revealed = false;
    bool m_revealOnFrame = false;
    bool m_recordingShortcut = false;
    QString m_shortcutConflict;
    bool m_shortcutPeerConflict = false;
    QString m_triggerStatus;
    QString m_lastError;
    QString m_linkEndpointState = QStringLiteral("Starting");
    QString m_linkLastError;
    QVariantList m_linkJobs;
    QVariantMap m_linkDraft;
    QString m_linkSource;
};
