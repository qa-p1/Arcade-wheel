#include "core/WheelController.h"

#include "core/WheelLogic.h"

#include <QCursor>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QKeySequence>
#include <QQuickView>
#include <QScreen>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
// Matches hideOverlay()'s close fallback plus a compositor frame to unmap the
// surface, so a screenshot taken from the wheel does not capture the wheel.
constexpr int kScreenCaptureDelayMs = 320;
// Live settings (slider drags) are applied at once but written to disk only
// after this quiet period; every save is an atomic replace with fdatasync.
constexpr int kCoalescedSaveDelayMs = 250;

bool capturesScreen(const QJsonArray &actions)
{
    return std::any_of(actions.begin(), actions.end(), [](const QJsonValue &value) {
        const auto action = value.toObject();
        return action.value(QStringLiteral("type")).toString() == QStringLiteral("system")
            && action.value(QStringLiteral("payload")).toObject().value(QStringLiteral("id")).toString()
                   == QStringLiteral("screenshot");
    });
}
}

WheelController::WheelController(PlatformBackend *backend, QObject *parent, QString configPath)
    : QObject(parent), m_backend(backend), m_store(std::move(configPath)), m_dispatcher(backend)
{
    m_holdTimer.setSingleShot(true);
    m_closeTimer.setSingleShot(true);
    m_centerClickTimer.setSingleShot(true);
    m_centerHoldTimer.setSingleShot(true);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kCoalescedSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &WheelController::flushPendingSave);
    connect(&m_centerClickTimer, &QTimer::timeout, this, &WheelController::finishCenterClicks);
    connect(&m_centerHoldTimer, &QTimer::timeout, this, [this] {
        if (m_visible && m_centerPressed) executeCenterGesture(QStringLiteral("longPress"));
    });
    connect(&m_closeTimer, &QTimer::timeout, this, &WheelController::finishClose);
    connect(backend, &PlatformBackend::triggerCancelled, this, &WheelController::cancelTrigger);
    connect(backend, &PlatformBackend::actionFailed, this, [this](const QString &error) {
        reportError(error);
        emit actionFailed(error);
    });
    connect(&m_holdTimer, &QTimer::timeout, this, &WheelController::openOverlay);
    connect(backend, &PlatformBackend::triggerPressed, this, &WheelController::pressTrigger);
    connect(backend, &PlatformBackend::triggerReleased, this, &WheelController::releaseTrigger);
    connect(backend, &PlatformBackend::triggerStatusChanged, this, [this](const QString &message) {
        if (m_triggerStatus == message) return;
        m_triggerStatus = message;
        emit triggerStatusChanged();
    });
}

WheelController::~WheelController()
{
    flushPendingSave();
}

bool WheelController::flushPendingSave()
{
    if (!m_savePending) return true;
    m_saveTimer.stop();
    m_savePending = false;
    QString error;
    if (m_store.save(m_config, &error)) return true;
    reportError(error);
    return false;
}

void WheelController::initialize()
{
    refreshApplications();
    QString error;
    m_config = m_store.load(m_applications, &error);
    if (!error.isEmpty()) reportError(error);
    // The login entry is owned by the OS and can change outside the app.
    // Show what is actually installed rather than a stale saved flag; the
    // next saved change records it.
    auto general = m_config.value(QStringLiteral("general")).toObject();
    general.insert(QStringLiteral("startOnLogin"), m_backend->startOnLogin());
    m_config.insert(QStringLiteral("general"), general);
    m_backend->configureTrigger(m_config.value(QStringLiteral("trigger")).toObject());
    emit configChanged();
    emit currentDeckChanged();
}

void WheelController::setOverlayView(QQuickView *view)
{
    m_overlay = view;
    connect(view, &QQuickWindow::frameSwapped, this, [this] {
        if (!m_visible || !m_revealOnFrame) return;
        m_revealOnFrame = false;
        // Map the input surface before warping. A pre-map warp leaves Wayland
        // pointer focus on the underlying app until the next physical motion,
        // which also sends wheel events to that app.
        m_openClock.restart();
        m_waitingForCenter = m_backend->movePointer(
            QPointF(m_overlay->screen()->geometry().topLeft()) + m_origin);
        m_lastPointer = m_origin;
        setSelectedIndex(-1);
        m_revealed = true;
        emit overlayRevealedChanged();
    }, Qt::QueuedConnection);
    connect(view, &QQuickView::widthChanged, this, &WheelController::updateOverlayGeometry);
    connect(view, &QQuickView::heightChanged, this, &WheelController::updateOverlayGeometry);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { cancelTrigger(); });
}

QJsonArray WheelController::decks() const
{
    return m_config.value(QStringLiteral("decks")).toArray();
}

QJsonObject WheelController::deckAt(int index) const
{
    const auto all = decks();
    return index >= 0 && index < all.size() ? all.at(index).toObject() : QJsonObject{};
}

int WheelController::deckIndexById(const QString &id) const
{
    const auto all = decks();
    for (int i = 0; i < all.size(); ++i)
        if (all.at(i).toObject().value(QStringLiteral("id")).toString() == id) return i;
    return -1;
}

QVariantMap WheelController::currentDeck() const
{
    return deckAt(m_deckIndex).toVariantMap();
}

int WheelController::deckCount() const
{
    return decks().size();
}

QVariantList WheelController::applications() const
{
    QVariantList items;
    items.reserve(m_applications.size());
    for (const auto &app : m_applications)
        items.append(QVariantMap{{QStringLiteral("id"), app.id},
                                 {QStringLiteral("name"), app.name},
                                 {QStringLiteral("icon"), app.icon},
                                 {QStringLiteral("description"), app.description}});
    return items;
}

void WheelController::pressTrigger()
{
    if (m_pressed || m_recordingShortcut) return;
    if (m_visible) cancelTrigger();
    m_mode = Mode::Hold;
    m_pressed = true;
    m_triggerPressClock.restart();
    const int threshold = std::clamp(m_config.value(QStringLiteral("trigger")).toObject()
                                         .value(QStringLiteral("holdThresholdMs")).toInt(), 0, 1000);
    if (threshold == 0) openOverlay();
    else m_holdTimer.start(threshold);
}

void WheelController::releaseTrigger()
{
    if (m_mode == Mode::Preview) { cancelTrigger(); return; }
    if (!m_pressed) return;
    m_pressed = false;
    m_holdTimer.stop();
    if (m_centerClicks > 0 && !m_centerPressed) {
        finishCenterClicks();
        if (!m_visible) return;
    }
    const QString shortcut = m_config.value(QStringLiteral("trigger")).toObject()
                                 .value(QStringLiteral("shortcut")).toString();
    const QKeySequence sequence(shortcut, QKeySequence::PortableText);
    const bool muteKey = (!sequence.isEmpty() && sequence[0].key() == Qt::Key_VolumeMute)
        || shortcut.compare(QStringLiteral("XF86AudioMute"), Qt::CaseInsensitive) == 0;
    if (m_mode == Mode::Hold && muteKey && m_triggerPressClock.isValid()
        && m_triggerPressClock.elapsed() < 35) {
        // A consumer-control pulse contains no usable physical release edge.
        // Keep hold semantics; never silently substitute a click/toggle mode.
        cancelTrigger();
        m_triggerStatus = QStringLiteral("Volume Mute sends an immediate release on this keyboard. Use a key that reports a hold, such as Alt+Space.");
        emit triggerStatusChanged();
        return;
    }
    if (m_visible && m_mode == Mode::Hold) executeSelection(m_selectedIndex);
}

void WheelController::showWheel()
{
    if (m_visible && m_mode == Mode::Click) { cancelTrigger(); return; }
    cancelTrigger();
    m_mode = Mode::Click;
    openOverlay();
}

void WheelController::previewOverlay()
{
    cancelTrigger();
    m_mode = Mode::Preview;
    openOverlay();
}

void WheelController::activateSlot(int index)
{
    if (!m_visible) return;
    if (m_mode == Mode::Preview) return;
    m_pressed = false;
    m_holdTimer.stop();
    executeSelection(index);
}

void WheelController::executeSelection(int index)
{
    const auto actions = deckAt(m_deckIndex).value(QStringLiteral("actions")).toArray();
    const QJsonObject action = index >= 0 && index < actions.size() ? actions.at(index).toObject() : QJsonObject{};
    executeActions(action.isEmpty() ? QJsonArray{} : QJsonArray{action});
}

void WheelController::executeActions(const QJsonArray &actions)
{
    m_pressed = false;
    m_holdTimer.stop();
    hideOverlay();
    if (actions.isEmpty()) return;
    // Execution never waits for the exit animation. Keep its selection visible
    // only for the short visual collapse, then release the native surface.
    // Screen captures are the exception: they would record the fading wheel.
    QTimer::singleShot(capturesScreen(actions) ? kScreenCaptureDelayMs : 0, this, [this, actions] {
        QStringList errors;
        const bool focus = m_config.value(QStringLiteral("behaviour")).toObject()
                               .value(QStringLiteral("focusExisting")).toBool(true);
        for (const auto &value : actions) {
            const auto action = value.toObject();
            if (action.value(QStringLiteral("type")).toString() == QStringLiteral("none")) continue;
            QString error;
            if (!m_dispatcher.execute(action, focus, &error))
                errors << action.value(QStringLiteral("name")).toString() + QStringLiteral(": ") + error;
        }
        if (!errors.isEmpty()) {
            const QString message = errors.join(QLatin1Char('\n'));
            reportError(message);
            emit actionFailed(message);
        }
    });
}

void WheelController::cancelTrigger()
{
    m_holdTimer.stop();
    m_pressed = false;
    hideOverlay();
}

void WheelController::openOverlay()
{
    if (m_visible || !m_overlay) return;
    const QPointF cursor = m_backend->cursorPosition();
    QScreen *screen = QGuiApplication::screenAt(cursor.toPoint());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    m_closeTimer.stop();
    m_overlay->setScreen(screen);
    m_overlay->setGeometry(screen->geometry());
    QString error;
    if (!m_backend->prepareOverlay(m_overlay, &error)) {
        reportError(error);
        m_pressed = false;
        return;
    }
    updateOverlayGeometry();
    m_lastPointer = m_origin;
    m_scrollClock.invalidate();
    setSelectedIndex(-1);
    m_openClock.restart();
    m_waitingForCenter = false;
    m_visible = true;
    m_revealed = false;
    m_revealOnFrame = true;
    emit overlayRevealedChanged();
    // The QML scene stays loaded and receives an explicit opened change on
    // every invocation, including reopen during the previous exit animation.
    m_overlay->show();
    emit overlayVisibleChanged();
    m_overlay->requestUpdate();
}

void WheelController::updateOverlayGeometry()
{
    if (!m_overlay) return;
    m_visualCenter = QPointF(m_overlay->width() / 2.0, m_overlay->height() / 2.0);
    m_origin = m_visualCenter;
    emit geometryChanged();
}

void WheelController::hideOverlay()
{
    cancelCenterGesture();
    if (!m_visible) return;
    m_visible = false;
    m_revealed = false;
    m_revealOnFrame = false;
    emit overlayRevealedChanged();
    m_waitingForCenter = false;
    // A bounded fallback also closes the surface if the renderer is suspended.
    m_closeTimer.start(240);
    emit overlayVisibleChanged();
}

void WheelController::finishClose()
{
    if (m_visible) return; // Ignore a stale animation completion after reopen.
    m_closeTimer.stop();
    if (m_overlay) m_overlay->hide();
    setSelectedIndex(-1);
}

void WheelController::clearError()
{
    m_lastError.clear();
    emit lastErrorChanged();
}

void WheelController::setSelectedIndex(int index)
{
    if (m_selectedIndex == index) return;
    m_selectedIndex = index;
    emit selectedIndexChanged();
}

void WheelController::pointerMoved(qreal localX, qreal localY)
{
    if (!m_visible) return;
    if ((m_centerPressed || m_centerClicks > 0) && !inCenter(localX, localY)) cancelCenterGesture();
    const QPointF pointer(localX, localY);
    if (m_waitingForCenter) {
        if (std::hypot(pointer.x() - m_origin.x(), pointer.y() - m_origin.y()) > 32 &&
            m_openClock.elapsed() < 50) return;
        m_waitingForCenter = false;
    }
    m_lastPointer = pointer;
    const auto behaviour = m_config.value(QStringLiteral("behaviour")).toObject();
    const qreal deadZone = std::clamp(behaviour.value(QStringLiteral("deadZone")).toDouble(64.0), 20.0, 180.0);
    const qreal sensitivity = std::clamp(behaviour.value(QStringLiteral("selectionSensitivity")).toDouble(1.0), 0.5, 2.0);
    const int count = deckAt(m_deckIndex).value(QStringLiteral("actions")).toArray().size();
    setSelectedIndex(WheelLogic::selectedSlot(m_lastPointer, m_origin, count, deadZone / sensitivity));
}

void WheelController::scrollDeck(int steps)
{
    if (!m_visible || deckCount() <= 1 || steps == 0) return;
    const auto behaviour = m_config.value(QStringLiteral("behaviour")).toObject();
    const int cooldown = std::clamp(behaviour.value(QStringLiteral("scrollCooldownMs")).toInt(110), 0, 500);
    if (m_scrollClock.isValid() && m_scrollClock.elapsed() < cooldown) return;
    m_scrollClock.restart();
    const int next = WheelLogic::scrollIndex(m_deckIndex, deckCount(), steps,
                                             behaviour.value(QStringLiteral("wrapDecks")).toBool(true),
                                             behaviour.value(QStringLiteral("scrollReverse")).toBool(false));
    if (next == m_deckIndex) return;
    cancelCenterGesture();
    m_deckIndex = next;
    emit currentDeckChanged();
    pointerMoved(m_lastPointer.x(), m_lastPointer.y());
}

void WheelController::selectDeck(int index)
{
    if (index < 0 || index >= deckCount() || index == m_deckIndex) return;
    m_deckIndex = index;
    emit currentDeckChanged();
    if (m_visible) pointerMoved(m_lastPointer.x(), m_lastPointer.y());
}

bool WheelController::commit(QJsonObject updated, bool triggerChanged, Persist persist)
{
    QString error;
    updated = ConfigStore::normalize(updated, &error);
    if (updated.isEmpty()) {
        reportError(error);
        return false;
    }
    if (persist == Persist::Coalesced) {
        m_savePending = true;
        m_saveTimer.start();
    } else {
        if (!m_store.save(updated, &error)) {
            reportError(error);
            return false;
        }
        // This write also contains any coalesced change still waiting.
        m_savePending = false;
        m_saveTimer.stop();
    }
    m_config = updated;
    m_deckIndex = std::clamp(m_deckIndex, 0, std::max(0, deckCount() - 1));
    if (triggerChanged) m_backend->configureTrigger(m_config.value(QStringLiteral("trigger")).toObject());
    emit configChanged();
    emit currentDeckChanged();
    return true;
}

void WheelController::updateSetting(const QString &section, const QString &key, const QVariant &value)
{
    if (section != QStringLiteral("general") && section != QStringLiteral("trigger") &&
        section != QStringLiteral("appearance") && section != QStringLiteral("behaviour") &&
        section != QStringLiteral("centerGestures")) return;
    if (key.isEmpty()) return;
    const bool loginSetting = section == QStringLiteral("general") && key == QStringLiteral("startOnLogin");
    const bool wasStartingOnLogin = loginSetting && m_backend->startOnLogin();
    if (loginSetting) {
        QString error;
        if (!m_backend->setStartOnLogin(value.toBool(), &error)) {
            reportError(error);
            return;
        }
    }
    auto updated = m_config;
    auto object = updated.value(section).toObject();
    object.insert(key, QJsonValue::fromVariant(value));
    updated.insert(section, object);
    // Hold timing is local to the controller; changing it must not tear down
    // the global shortcut while the slider is moving.
    const bool saved = commit(updated, section == QStringLiteral("trigger") && key != QStringLiteral("holdThresholdMs"),
                              loginSetting ? Persist::Now : Persist::Coalesced);
    // Keep the OS entry and the saved flag in agreement.
    if (!saved && loginSetting) m_backend->setStartOnLogin(wasStartingOnLogin, nullptr);
    if (saved && section == QStringLiteral("trigger") && key == QStringLiteral("holdThresholdMs"))
        m_backend->setTriggerHoldThreshold(value.toInt());
}

void WheelController::setDeckName(const QString &deckId, const QString &name)
{
    const int index = deckIndexById(deckId);
    if (index < 0 || name.trimmed().isEmpty()) return;
    auto updated = m_config;
    auto all = decks();
    auto deck = all.at(index).toObject();
    deck.insert(QStringLiteral("name"), name.trimmed());
    all.replace(index, deck);
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
}

void WheelController::createDeck()
{
    auto updated = m_config;
    auto all = decks();
    QJsonArray actions;
    for (int i = 0; i < 6; ++i) actions.append(ConfigStore::emptyAction());
    all.append(QJsonObject{{QStringLiteral("id"), ConfigStore::freshId()},
                           {QStringLiteral("name"), QStringLiteral("New deck")},
                           {QStringLiteral("actions"), actions}});
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
    selectDeck(all.size() - 1);
}

void WheelController::duplicateDeck(const QString &deckId)
{
    const int index = deckIndexById(deckId);
    if (index < 0) return;
    auto updated = m_config;
    auto all = decks();
    auto copy = all.at(index).toObject();
    copy.insert(QStringLiteral("id"), ConfigStore::freshId());
    copy.insert(QStringLiteral("name"), copy.value(QStringLiteral("name")).toString() + QStringLiteral(" copy"));
    auto actions = copy.value(QStringLiteral("actions")).toArray();
    for (int i = 0; i < actions.size(); ++i) {
        auto action = actions.at(i).toObject();
        action.insert(QStringLiteral("id"), ConfigStore::freshId());
        actions.replace(i, action);
    }
    copy.insert(QStringLiteral("actions"), actions);
    all.insert(index + 1, copy);
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
    selectDeck(index + 1);
}

void WheelController::deleteDeck(const QString &deckId)
{
    auto all = decks();
    if (all.size() <= 1) return;
    const int index = deckIndexById(deckId);
    if (index < 0) return;
    all.removeAt(index);
    auto updated = m_config;
    updated.insert(QStringLiteral("decks"), all);
    if (m_deckIndex >= index && m_deckIndex > 0) --m_deckIndex;
    commit(updated);
}

void WheelController::moveDeck(int from, int to)
{
    auto all = decks();
    if (from < 0 || from >= all.size() || to < 0 || to >= all.size() || from == to) return;
    const auto activeId = deckAt(m_deckIndex).value(QStringLiteral("id")).toString();
    const auto moved = all.takeAt(from);
    all.insert(to, moved);
    auto updated = m_config;
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
    selectDeck(deckIndexById(activeId));
}

void WheelController::resizeDeck(const QString &deckId, int size)
{
    const int index = deckIndexById(deckId);
    if (index < 0) return;
    size = std::clamp(size, 4, 8);
    auto all = decks();
    auto deck = all.at(index).toObject();
    auto actions = deck.value(QStringLiteral("actions")).toArray();
    while (actions.size() < size) actions.append(ConfigStore::emptyAction());
    while (actions.size() > size) actions.removeLast();
    deck.insert(QStringLiteral("actions"), actions);
    all.replace(index, deck);
    auto updated = m_config;
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
}

void WheelController::setAction(const QString &deckId, int slot, const QVariantMap &action)
{
    const int index = deckIndexById(deckId);
    if (index < 0) return;
    auto all = decks();
    auto deck = all.at(index).toObject();
    auto actions = deck.value(QStringLiteral("actions")).toArray();
    if (slot < 0 || slot >= actions.size()) return;
    auto replacement = QJsonObject::fromVariantMap(action);
    replacement.insert(QStringLiteral("id"), actions.at(slot).toObject().value(QStringLiteral("id")).toString());
    if (!replacement.value(QStringLiteral("payload")).isObject())
        replacement.insert(QStringLiteral("payload"), QJsonObject{});
    actions.replace(slot, replacement);
    deck.insert(QStringLiteral("actions"), actions);
    all.replace(index, deck);
    auto updated = m_config;
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
}

void WheelController::moveAction(const QString &deckId, int from, int to)
{
    const int index = deckIndexById(deckId);
    if (index < 0) return;
    auto all = decks();
    auto deck = all.at(index).toObject();
    auto actions = deck.value(QStringLiteral("actions")).toArray();
    if (from < 0 || from >= actions.size() || to < 0 || to >= actions.size() || from == to) return;
    const auto moved = actions.takeAt(from);
    actions.insert(to, moved);
    deck.insert(QStringLiteral("actions"), actions);
    all.replace(index, deck);
    auto updated = m_config;
    updated.insert(QStringLiteral("decks"), all);
    commit(updated);
}

QString WheelController::actionUnavailableReason(const QVariantMap &action) const
{
    return m_dispatcher.unavailableReason(QJsonObject::fromVariantMap(action));
}

void WheelController::refreshApplications()
{
    m_applications = m_backend->applications();
    std::sort(m_applications.begin(), m_applications.end(), [](const auto &a, const auto &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    m_dispatcher.setApplications(m_applications);
    emit applicationsChanged();
}

void WheelController::refreshProviders()
{
    m_dispatcher.arcadeBox().refresh();
    emit providersChanged();
}

QString WheelController::localPath(const QString &urlOrPath)
{
    const QUrl url(urlOrPath);
    return url.isLocalFile() ? url.toLocalFile() : urlOrPath;
}

bool WheelController::importConfig(const QString &urlOrPath)
{
    QJsonObject imported;
    QString error;
    if (!m_store.importFrom(localPath(urlOrPath), &imported, &error)) {
        reportError(error);
        return false;
    }
    if (!commit(imported, true)) return false;
    applyStartOnLogin();
    return true;
}

bool WheelController::exportConfig(const QString &urlOrPath)
{
    QString error;
    if (m_store.exportTo(m_config, localPath(urlOrPath), &error)) return true;
    reportError(error);
    return false;
}

void WheelController::resetDefaults()
{
    if (commit(ConfigStore::defaults(m_applications), true)) applyStartOnLogin();
    selectDeck(0);
}

void WheelController::applyStartOnLogin()
{
    // Import and reset replace the saved flag wholesale; make the OS login
    // entry follow it, or record the real state if the OS refuses.
    const bool wanted = m_config.value(QStringLiteral("general")).toObject()
                            .value(QStringLiteral("startOnLogin")).toBool();
    if (wanted == m_backend->startOnLogin()) return;
    QString error;
    if (m_backend->setStartOnLogin(wanted, &error)) return;
    reportError(error);
    auto updated = m_config;
    auto general = updated.value(QStringLiteral("general")).toObject();
    general.insert(QStringLiteral("startOnLogin"), m_backend->startOnLogin());
    updated.insert(QStringLiteral("general"), general);
    commit(updated);
}

void WheelController::reportError(const QString &error)
{
    if (error.isEmpty()) return;
    m_lastError = error;
    emit lastErrorChanged();
}

QString WheelController::shortcutFromKey(int key, int modifiers) const
{
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_unknown) return {};
    const auto mods = Qt::KeyboardModifiers(modifiers) &
        (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    QString sequence = QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
    if (key == Qt::Key_Plus && sequence.endsWith(QLatin1Char('+'))) sequence.chop(1), sequence += QStringLiteral("Plus");
    return sequence;
}

void WheelController::setShortcutRecording(bool recording)
{
    if (m_recordingShortcut == recording) return;
    m_recordingShortcut = recording;
    if (recording) cancelTrigger();
    m_backend->setShortcutRecording(recording);
}

bool WheelController::applyShortcut(const QString &shortcut, bool overrideConflict)
{
    const QString validationError = m_backend->shortcutValidationError(shortcut);
    if (!validationError.isEmpty()) {
        m_shortcutConflict.clear();
        emit shortcutConflictChanged();
        reportError(validationError);
        return false;
    }
    m_shortcutConflict = m_backend->shortcutConflict(shortcut);
    emit shortcutConflictChanged();
    if (!overrideConflict && !m_shortcutConflict.isEmpty()) return false;
    auto updated = m_config;
    auto trigger = updated.value(QStringLiteral("trigger")).toObject();
    trigger.remove(QStringLiteral("preferredTrigger"));
    trigger.insert(QStringLiteral("shortcut"),shortcut.trimmed());
    trigger.insert(QStringLiteral("overrideConflict"),overrideConflict);
    updated.insert(QStringLiteral("trigger"),trigger);
    if (!commit(updated,true)) return false;
    m_shortcutConflict.clear();
    emit shortcutConflictChanged();
    return true;
}

namespace {
bool validCenterGesture(const QString &gesture)
{
    return gesture == QStringLiteral("singleClick") || gesture == QStringLiteral("doubleClick")
        || gesture == QStringLiteral("tripleClick") || gesture == QStringLiteral("longPress");
}
}

bool WheelController::inCenter(qreal x, qreal y) const
{
    const auto appearance = m_config.value(QStringLiteral("appearance")).toObject();
    const qreal radius = std::clamp(appearance.value("centerRadius").toDouble(56), 32.0, 72.0)
        * std::clamp(appearance.value("scale").toDouble(1), 0.75, 1.4);
    return std::hypot(x - m_origin.x(), y - m_origin.y()) <= radius;
}

bool WheelController::centerGestureEnabled(const QString &gesture) const
{
    const auto group = m_config.value("centerGestures").toObject().value(gesture).toObject();
    return group.value("enabled").toBool() && !group.value("actions").toArray().isEmpty();
}

bool WheelController::centerPressed(qreal x, qreal y)
{
    if (!m_visible || !inCenter(x, y)) return false;
    if (m_mode == Mode::Preview) return true; // Safe preview never executes groups.
    m_centerClickTimer.stop();
    m_centerPressed = true;
    emit centerGestureChanged();
    if (centerGestureEnabled(QStringLiteral("longPress")))
        m_centerHoldTimer.start(m_config.value("centerGestures").toObject().value("longPressMs").toInt(500));
    return true;
}

void WheelController::centerReleased(qreal x, qreal y)
{
    m_centerHoldTimer.stop();
    if (!m_visible || !m_centerPressed) return;
    m_centerPressed = false;
    emit centerGestureChanged();
    if (!inCenter(x, y)) { cancelCenterGesture(); return; }
    ++m_centerClicks;
    const bool triple = centerGestureEnabled(QStringLiteral("tripleClick"));
    const bool doubleClick = centerGestureEnabled(QStringLiteral("doubleClick"));
    if (m_centerClicks >= 3 || (m_centerClicks == 2 && !triple) || (!doubleClick && !triple)) {
        finishCenterClicks();
    } else {
        m_centerClickTimer.start(m_config.value("centerGestures").toObject().value("clickIntervalMs").toInt(280));
    }
}

void WheelController::cancelCenterGesture()
{
    m_centerClickTimer.stop();
    m_centerHoldTimer.stop();
    m_centerClicks = 0;
    if (m_centerPressed) {
        m_centerPressed = false;
        emit centerGestureChanged();
    }
}

void WheelController::finishCenterClicks()
{
    const int clicks = m_centerClicks;
    cancelCenterGesture();
    if (!m_visible || m_mode == Mode::Preview) return;
    if (clicks >= 3 && centerGestureEnabled(QStringLiteral("tripleClick")))
        executeCenterGesture(QStringLiteral("tripleClick"));
    else if (clicks == 2 && centerGestureEnabled(QStringLiteral("doubleClick")))
        executeCenterGesture(QStringLiteral("doubleClick"));
    else if (clicks == 1 && centerGestureEnabled(QStringLiteral("singleClick")))
        executeCenterGesture(QStringLiteral("singleClick"));
    else cancelTrigger();
}

void WheelController::executeCenterGesture(const QString &gesture)
{
    if (!m_visible || m_mode == Mode::Preview || !centerGestureEnabled(gesture)) return;
    executeActions(m_config.value("centerGestures").toObject().value(gesture).toObject().value("actions").toArray());
}

void WheelController::setCenterGesture(const QString &gesture, const QString &name, bool enabled)
{
    if (!validCenterGesture(gesture)) return;
    auto updated = m_config;
    auto gestures = updated.value("centerGestures").toObject();
    auto group = gestures.value(gesture).toObject();
    group.insert("name", name.trimmed());
    group.insert("enabled", enabled);
    gestures.insert(gesture, group);
    updated.insert("centerGestures", gestures);
    cancelCenterGesture();
    commit(updated);
}

void WheelController::setCenterGestureAction(const QString &gesture, int slot, const QVariantMap &action)
{
    if (!validCenterGesture(gesture)) return;
    auto updated = m_config;
    auto gestures = updated.value("centerGestures").toObject();
    auto group = gestures.value(gesture).toObject();
    auto actions = group.value("actions").toArray();
    if (slot < -1 || slot >= actions.size() || (slot == -1 && actions.size() >= 16)) return;
    auto replacement = QJsonObject::fromVariantMap(action);
    replacement.insert("id", slot == -1 ? ConfigStore::freshId() : actions.at(slot).toObject().value("id").toString());
    if (slot == -1) {
        if (actions.isEmpty()) group.insert("enabled", true);
        actions.append(replacement);
    } else actions.replace(slot, replacement);
    group.insert("actions", actions);
    gestures.insert(gesture, group);
    updated.insert("centerGestures", gestures);
    cancelCenterGesture();
    commit(updated);
}

void WheelController::removeCenterGestureAction(const QString &gesture, int slot)
{
    if (!validCenterGesture(gesture)) return;
    auto updated = m_config;
    auto gestures = updated.value("centerGestures").toObject();
    auto group = gestures.value(gesture).toObject();
    auto actions = group.value("actions").toArray();
    if (slot < 0 || slot >= actions.size()) return;
    actions.removeAt(slot);
    group.insert("actions", actions);
    gestures.insert(gesture, group);
    updated.insert("centerGestures", gestures);
    cancelCenterGesture();
    commit(updated);
}

void WheelController::moveCenterGestureAction(const QString &gesture, int from, int to)
{
    if (!validCenterGesture(gesture)) return;
    auto updated = m_config;
    auto gestures = updated.value("centerGestures").toObject();
    auto group = gestures.value(gesture).toObject();
    auto actions = group.value("actions").toArray();
    if (from < 0 || to < 0 || from >= actions.size() || to >= actions.size() || from == to) return;
    actions.insert(to, actions.takeAt(from));
    group.insert("actions", actions);
    gestures.insert(gesture, group);
    updated.insert("centerGestures", gestures);
    cancelCenterGesture();
    commit(updated);
}
