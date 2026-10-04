#include <gio/gdesktopappinfo.h>
#include "LinuxBackend.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <utility>

#ifdef ARCADE_HAVE_LAYERSHELLQT
#include <LayerShellQt/Window>
#endif

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kRegistryInterface = "org.freedesktop.host.portal.Registry";
constexpr auto kGlobalShortcutsInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";
constexpr auto kSessionInterface = "org.freedesktop.portal.Session";
constexpr auto kShortcutId = "arcade-wheel-trigger";

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument;
}

QString xdgUnescape(QString value)
{
    QString output;
    output.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (value.at(i) != QLatin1Char('\\') || i + 1 >= value.size()) {
            output.append(value.at(i));
            continue;
        }
        const QChar escaped = value.at(++i);
        if (escaped == QLatin1Char('s')) output.append(QLatin1Char(' '));
        else if (escaped == QLatin1Char('n')) output.append(QLatin1Char('\n'));
        else if (escaped == QLatin1Char('t')) output.append(QLatin1Char('\t'));
        else if (escaped == QLatin1Char('r')) output.append(QLatin1Char('\r'));
        else if (escaped == QLatin1Char('\\') || escaped == QLatin1Char(';')) output.append(escaped);
        else {
            // Preserve unknown escapes. Invalid entries will fail later when
            // their executable command is parsed.
            output.append(QLatin1Char('\\'));
            output.append(escaped);
        }
    }
    return output;
}

QHash<QString, QString> parseDesktopFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    QHash<QString, QString> values;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    bool inDesktopEntry = false;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inDesktopEntry = line == QStringLiteral("[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry) continue;
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) continue;
        values.insert(line.left(equals).trimmed(), xdgUnescape(line.mid(equals + 1).trimmed()));
    }
    return values;
}

QString localizedValue(const QHash<QString, QString> &values, const QString &key)
{
    const QLocale locale;
    const QString localeName = locale.name();
    const QString language = localeName.section(QLatin1Char('_'), 0, 0);
    const QString country = localeName.section(QLatin1Char('_'), 1, 1);
    const QStringList candidates = {
        key + QLatin1Char('[') + localeName + QLatin1Char(']'),
        country.isEmpty() ? QString() : key + QLatin1Char('[') + language + QLatin1Char('_') + country + QLatin1Char(']'),
        key + QLatin1Char('[') + language + QLatin1Char(']'),
        key
    };
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && values.contains(candidate)) return values.value(candidate);
    }
    return {};
}

QStringList semicolonList(const QString &value)
{
    QStringList result;
    for (const QString &item : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty()) result.append(trimmed);
    }
    return result;
}

bool parseDesktopBoolean(const QHash<QString, QString> &values, const QString &key)
{
    return values.value(key).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

QString preferredKeyName(const QString &rawKey, QString *error)
{
    const QString key = rawKey.trimmed();
    const QString lower = key.toLower();
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("space"), QStringLiteral("space")},
        {QStringLiteral("enter"), QStringLiteral("Return")},
        {QStringLiteral("return"), QStringLiteral("Return")},
        {QStringLiteral("esc"), QStringLiteral("Escape")},
        {QStringLiteral("escape"), QStringLiteral("Escape")},
        {QStringLiteral("backspace"), QStringLiteral("BackSpace")},
        {QStringLiteral("delete"), QStringLiteral("Delete")},
        {QStringLiteral("insert"), QStringLiteral("Insert")},
        {QStringLiteral("tab"), QStringLiteral("Tab")},
        {QStringLiteral("home"), QStringLiteral("Home")},
        {QStringLiteral("end"), QStringLiteral("End")},
        {QStringLiteral("pageup"), QStringLiteral("Page_Up")},
        {QStringLiteral("pgup"), QStringLiteral("Page_Up")},
        {QStringLiteral("page_up"), QStringLiteral("Page_Up")},
        {QStringLiteral("pagedown"), QStringLiteral("Page_Down")},
        {QStringLiteral("pgdown"), QStringLiteral("Page_Down")},
        {QStringLiteral("pgdn"), QStringLiteral("Page_Down")},
        {QStringLiteral("del"), QStringLiteral("Delete")},
        {QStringLiteral("ins"), QStringLiteral("Insert")},
        {QStringLiteral("page_down"), QStringLiteral("Page_Down")},
        {QStringLiteral("left"), QStringLiteral("Left")},
        {QStringLiteral("right"), QStringLiteral("Right")},
        {QStringLiteral("up"), QStringLiteral("Up")},
        {QStringLiteral("down"), QStringLiteral("Down")},
        {QStringLiteral("capslock"), QStringLiteral("Caps_Lock")},
        {QStringLiteral("numlock"), QStringLiteral("Num_Lock")},
        {QStringLiteral("scrolllock"), QStringLiteral("Scroll_Lock")},
        {QStringLiteral("printscreen"), QStringLiteral("Print")},
        {QStringLiteral("print"), QStringLiteral("Print")},
        {QStringLiteral("pause"), QStringLiteral("Pause")},
        {QStringLiteral("menu"), QStringLiteral("Menu")},
        {QStringLiteral("volume mute"), QStringLiteral("XF86AudioMute")},
        {QStringLiteral("volume up"), QStringLiteral("XF86AudioRaiseVolume")},
        {QStringLiteral("volume down"), QStringLiteral("XF86AudioLowerVolume")},
        {QStringLiteral("media play"), QStringLiteral("XF86AudioPlay")},
        {QStringLiteral("media pause"), QStringLiteral("XF86AudioPause")},
        {QStringLiteral("media play/pause"), QStringLiteral("XF86AudioPlay")},
        {QStringLiteral("toggle media play/pause"), QStringLiteral("XF86AudioPlay")},
        {QStringLiteral("media stop"), QStringLiteral("XF86AudioStop")},
        {QStringLiteral("media next"), QStringLiteral("XF86AudioNext")},
        {QStringLiteral("media previous"), QStringLiteral("XF86AudioPrev")},
        {QStringLiteral("plus"), QStringLiteral("plus")},
        {QStringLiteral("minus"), QStringLiteral("minus")},
        {QStringLiteral("comma"), QStringLiteral("comma")},
        {QStringLiteral("period"), QStringLiteral("period")},
        {QStringLiteral("slash"), QStringLiteral("slash")},
        {QStringLiteral("backslash"), QStringLiteral("backslash")},
        {QStringLiteral("semicolon"), QStringLiteral("semicolon")},
        {QStringLiteral("apostrophe"), QStringLiteral("apostrophe")},
        {QStringLiteral("grave"), QStringLiteral("grave")},
        {QStringLiteral("equal"), QStringLiteral("equal")},
        {QStringLiteral("bracketleft"), QStringLiteral("bracketleft")},
        {QStringLiteral("bracketright"), QStringLiteral("bracketright")}
    };
    if (aliases.contains(lower)) return aliases.value(lower);

    static const QRegularExpression functionKey(QStringLiteral("^F([1-9]|[12][0-9]|3[0-5])$"),
                                                QRegularExpression::CaseInsensitiveOption);
    if (functionKey.match(key).hasMatch()) return key.toUpper();
    static const QRegularExpression xf86Key(QStringLiteral("^XF86[A-Za-z0-9_]+$"));
    if (xf86Key.match(key).hasMatch()) return key;
    if (key.size() == 1) {
        const QChar ch = key.at(0);
        if (ch.isLetter()) return QString(ch.toLower());
        if (ch.isDigit()) return key;
        static const QHash<QChar, QString> punctuation = {
            {QLatin1Char('-'), QStringLiteral("minus")},
            {QLatin1Char('='), QStringLiteral("equal")},
            {QLatin1Char('['), QStringLiteral("bracketleft")},
            {QLatin1Char(']'), QStringLiteral("bracketright")},
            {QLatin1Char(';'), QStringLiteral("semicolon")},
            {QLatin1Char('\''), QStringLiteral("apostrophe")},
            {QLatin1Char(','), QStringLiteral("comma")},
            {QLatin1Char('.'), QStringLiteral("period")},
            {QLatin1Char('/'), QStringLiteral("slash")},
            {QLatin1Char('\\'), QStringLiteral("backslash")},
            {QLatin1Char('`'), QStringLiteral("grave")}
        };
        if (punctuation.contains(ch)) return punctuation.value(ch);
    }

    if (error) *error = QStringLiteral("Unsupported Linux shortcut key: %1").arg(rawKey);
    return {};
}

QString escapeDesktopExecPath(QString path)
{
    path.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    path.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    path.replace(QStringLiteral("`"), QStringLiteral("\\`"));
    path.replace(QStringLiteral("$"), QStringLiteral("\\$"));
    return QLatin1Char('"') + path + QLatin1Char('"');
}

} // namespace

Q_DECLARE_METATYPE(PortalShortcut)

LinuxPortalRequest::LinuxPortalRequest(const QDBusConnection &bus,
                                       const QString &portalService,
                                       const QString &predictedPath,
                                       QObject *parent,
                                       Completion completion)
    : QObject(parent)
    , m_bus(bus)
    , m_portalService(portalService)
    , m_completion(std::move(completion))
{
    setRequestPath(predictedPath);
    QTimer::singleShot(30000, this, [this] {
        if (m_finished) return;
        QDBusMessage close = QDBusMessage::createMethodCall(m_portalService, m_path,
            QString::fromLatin1(kRequestInterface), QStringLiteral("Close"));
        m_bus.asyncCall(close, 1500);
        onResponse(2, {{QStringLiteral("arcade_timeout"), true}});
    });
}

LinuxPortalRequest::~LinuxPortalRequest()
{
    unsubscribe();
}

bool LinuxPortalRequest::subscribe(const QString &path)
{
    if (path.isEmpty()) return false;
    return m_bus.connect(m_portalService,
                         path,
                         kRequestInterface,
                         "Response",
                         this,
                         SLOT(onResponse(uint,QVariantMap)));
}

void LinuxPortalRequest::unsubscribe()
{
    if (m_path.isEmpty()) return;
    m_bus.disconnect(m_portalService,
                     m_path,
                     kRequestInterface,
                     "Response",
                     this,
                     SLOT(onResponse(uint,QVariantMap)));
}

void LinuxPortalRequest::setRequestPath(const QString &path)
{
    if (m_finished || path.isEmpty() || path == m_path) return;
    unsubscribe();
    m_path = path;
    if (!subscribe(m_path)) {
        qWarning("Arcade Wheel: could not subscribe to an XDG portal request response");
    }
}

void LinuxPortalRequest::cancel()
{
    if (m_finished) return;
    m_finished = true;
    unsubscribe();
    m_completion = {};
    deleteLater();
}

void LinuxPortalRequest::onResponse(uint response, const QVariantMap &results)
{
    if (m_finished) return;
    m_finished = true;
    unsubscribe();
    const QString path = m_path;
    Completion completion = std::move(m_completion);
    if (completion) completion(response, results, path);
    deleteLater();
}

LinuxBackend::LinuxBackend(QObject *parent)
    : PlatformBackend(parent)
    // Registry.Register must be the first portal call on this connection.
    // Qt's theme/file-dialog plugins may already have used sessionBus().
    , m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus,
              QStringLiteral("arcade-wheel-shortcuts-%1").arg(QUuid::createUuid().toString(QUuid::Id128))))
    , m_appId(QStringLiteral("com.arcadewheel.ArcadeWheel"))
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
    m_portalWatcher = new QDBusServiceWatcher(QString::fromLatin1(kPortalService),
                                                m_bus,
                                                QDBusServiceWatcher::WatchForOwnerChange,
                                                this);
    connect(m_portalWatcher,
            &QDBusServiceWatcher::serviceOwnerChanged,
            this,
            &LinuxBackend::onPortalOwnerChanged);

    m_bus.connect(QString::fromLatin1(kPortalService),
                  QString::fromLatin1(kPortalPath),
                  QString::fromLatin1(kGlobalShortcutsInterface),
                  "Activated",
                  this,
                  SLOT(onShortcutActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    m_bus.connect(QString::fromLatin1(kPortalService),
                  QString::fromLatin1(kPortalPath),
                  QString::fromLatin1(kGlobalShortcutsInterface),
                  "Deactivated",
                  this,
                  SLOT(onShortcutDeactivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));

    if (m_bus.isConnected()) {
        const auto owner = m_bus.interface()->serviceOwner(QString::fromLatin1(kPortalService));
        if (owner.isValid()) m_portalOwner = owner.value();
    }
    if (!m_portalOwner.isEmpty()) ensurePortalRegistered();
}

LinuxBackend::~LinuxBackend()
{
    setShortcutRecording(false);
    if (m_overlayRuleInstalled)
        sendHyprlandCommand(QStringLiteral("eval if _G.arcadeWheelLayerRule then _G.arcadeWheelLayerRule:set_enabled(false) end"));
    removeHyprlandBinding();
    if (!m_sessionHandle.isEmpty()) closePortalSession(m_sessionHandle);
    for (LinuxPortalRequest *request : std::as_const(m_portalRequests)) {
        if (request) request->cancel();
    }
    m_portalRequests.clear();
    QDBusConnection::disconnectFromBus(m_bus.name());
}

QString LinuxBackend::name() const
{
    return QStringLiteral("Linux");
}

void LinuxBackend::configureTrigger(const QJsonObject &trigger)
{
    if (qEnvironmentVariableIsSet("ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT")) {
        emit triggerStatusChanged(QStringLiteral("Global shortcut disabled for this test instance"));
        return;
    }
    watchHyprlandReloads();
    ++m_triggerGeneration;
    m_sessionRequestPending = false;
    m_identityRetries = 0;
    m_triggerConfiguration = trigger;
    m_holdThresholdMs = std::clamp(trigger.value(QStringLiteral("holdThresholdMs")).toInt(), 0, 1000);
    m_shortcutSetting = trigger.value(QStringLiteral("shortcut")).toString(
        trigger.value(QStringLiteral("key")).toString(QStringLiteral("F8"))).trimmed();
    if (m_triggerDown) { m_triggerDown = false; m_triggerClock.invalidate(); emit triggerCancelled(); }
    removeHyprlandBinding();

    QString problem;
    m_preferredTrigger = preferredTriggerFromShortcut(
        trigger.value(QStringLiteral("preferredTrigger")).toString(m_shortcutSetting), &problem);
    m_triggerSupported = !m_preferredTrigger.isEmpty();
    if (!m_triggerSupported) {
        if (problem.isEmpty()) problem = QStringLiteral("Choose a keyboard key or key combination for the Linux trigger.");
        emit triggerStatusChanged(problem);
    }

    if (!m_sessionHandle.isEmpty()) {
        const QString session = std::exchange(m_sessionHandle, QString());
        closePortalSession(session);
    }
    if (m_triggerSupported) {
        emit triggerStatusChanged(QStringLiteral("Connecting shortcut…"));
        ensurePortalRegistered();
    }
}

void LinuxBackend::setTriggerHoldThreshold(int milliseconds)
{
    m_holdThresholdMs = std::clamp(milliseconds, 0, 1000);
    m_triggerConfiguration.insert(QStringLiteral("holdThresholdMs"), m_holdThresholdMs);
    if (!m_sessionHandle.isEmpty() && (!isHyprland() || m_bindingInstalled))
        emit triggerStatusChanged(triggerReadyStatus());
}

QString LinuxBackend::triggerReadyStatus() const
{
    if (m_holdThresholdMs <= 0)
        return QStringLiteral("Ready · hold %1 to open").arg(m_shortcutSetting);

    if (isHyprland() && m_bindingInstalled && m_luaBinding)
        return QStringLiteral("Ready · tap %1 for normal input; hold %2 ms to open")
            .arg(m_shortcutSetting).arg(m_holdThresholdMs);

    return QStringLiteral("Ready · hold %1 to open. This desktop cannot pass short taps to the active app.")
        .arg(m_shortcutSetting);
}

QString LinuxBackend::preferredTriggerFromShortcut(const QString &shortcut, QString *error)
{
    const QString text = shortcut.trimmed();
    if (text.isEmpty()) {
        if (error) *error = QStringLiteral("The shortcut is empty.");
        return {};
    }
    if (text.contains(QStringLiteral("mouse"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("button"), Qt::CaseInsensitive)) {
        if (error) *error = QStringLiteral("The XDG GlobalShortcuts portal supports keyboard shortcuts; mouse-button triggers are not available on Linux Wayland.");
        return {};
    }

    QStringList parts = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) parts.append(text);
    QStringList modifiers;
    QString keyPart;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QString token = parts.at(i).trimmed();
        if (token.isEmpty()) continue;
        const QString lower = token.toLower();
        QString modifier;
        if (lower == QStringLiteral("ctrl") || lower == QStringLiteral("control")) modifier = QStringLiteral("CTRL");
        else if (lower == QStringLiteral("alt")) modifier = QStringLiteral("ALT");
        else if (lower == QStringLiteral("shift")) modifier = QStringLiteral("SHIFT");
        else if (lower == QStringLiteral("meta") || lower == QStringLiteral("super") || lower == QStringLiteral("win") || lower == QStringLiteral("logo")) modifier = QStringLiteral("LOGO");
        else if (lower == QStringLiteral("num")) modifier = QStringLiteral("NUM");

        if (!modifier.isEmpty() && i + 1 < parts.size()) {
            if (!modifiers.contains(modifier)) modifiers.append(modifier);
            continue;
        }
        if (!modifier.isEmpty() && i + 1 == parts.size()) {
            if (error) *error = QStringLiteral("A modifier needs a key, for example Ctrl+Space.");
            return {};
        }
        if (!keyPart.isEmpty()) {
            if (error) *error = QStringLiteral("Linux Wayland accepts a single key with optional modifiers.");
            return {};
        }
        keyPart = token;
    }

    if (keyPart.isEmpty()) {
        if (error) *error = QStringLiteral("Choose a keyboard key for the Linux trigger.");
        return {};
    }
    QString keyError;
    const QString keyName = preferredKeyName(keyPart, &keyError);
    if (keyName.isEmpty()) {
        if (error) *error = keyError;
        return {};
    }

    // Use the conventional order shown in the XDG shortcuts specification.
    const QStringList order = {QStringLiteral("CTRL"), QStringLiteral("ALT"), QStringLiteral("SHIFT"),
                               QStringLiteral("NUM"), QStringLiteral("LOGO")};
    QStringList normalized;
    for (const QString &modifier : order) if (modifiers.contains(modifier)) normalized.append(modifier);
    normalized.append(keyName);
    return normalized.join(QLatin1Char('+'));
}

bool LinuxBackend::ensureDesktopIdentity(QString *error)
{
    const QString fileName = m_appId + QStringLiteral(".desktop");
    if (auto *entry = g_desktop_app_info_new(fileName.toUtf8().constData())) {
        g_object_unref(entry);
        return true;
    }
    // Installed packages supply this file. A build launched from its checkout
    // needs the same durable identity before Registry.Register can succeed.
    // AppImage's internal binary is mounted under /tmp and disappears on exit.
    const QString imagePath = qEnvironmentVariable("APPIMAGE");
    const QString executable = QFileInfo(imagePath.isEmpty()
        ? QCoreApplication::applicationFilePath() : imagePath).canonicalFilePath();
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    for (const QString &root : {QStringLiteral("/tmp"), QStringLiteral("/var/tmp"), QStringLiteral("/run")}) {
        if (executable.startsWith(root + QLatin1Char('/'))) {
            *error = QStringLiteral("Install Arcade Wheel or move its build out of temporary storage before registering a shortcut.");
            return false;
        }
    }
    const QString path = QDir(dataHome).filePath(QStringLiteral("applications/") + fileName);
    if (dataHome.isEmpty() || executable.isEmpty() || QFile::exists(path)) {
        *error = QStringLiteral("Arcade Wheel needs a valid %1 desktop entry to register its shortcut.").arg(fileName);
        return false;
    }
    const QByteArray contents = QStringLiteral(
        "[Desktop Entry]\nType=Application\nName=Arcade Wheel\n"
        "Comment=Radial command launcher\nExec=%1 --settings\n"
        "Icon=applications-system\nTerminal=false\nCategories=Utility;\n")
        .arg(escapeDesktopExecPath(executable)).toUtf8();
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(contents) != contents.size() || !file.commit()) {
        *error = QStringLiteral("Could not create Arcade Wheel's desktop entry: %1").arg(file.errorString());
        return false;
    }
    auto *entry = g_desktop_app_info_new_from_filename(path.toUtf8().constData());
    if (!entry) {
        *error = QStringLiteral("Arcade Wheel's desktop entry could not be loaded.");
        return false;
    }
    g_object_unref(entry);
    return true;
}

void LinuxBackend::ensurePortalRegistered()
{
    if (!m_triggerSupported) return;
    if (!m_bus.isConnected()) {
        emit triggerStatusChanged(QStringLiteral("The session D-Bus is unavailable; the global trigger cannot be registered."));
        return;
    }
    if (m_portalOwner.isEmpty()) {
        const auto owner = m_bus.interface()->serviceOwner(QString::fromLatin1(kPortalService));
        if (owner.isValid()) m_portalOwner = owner.value();
    }
    if (m_portalOwner.isEmpty()) {
        emit triggerStatusChanged(QStringLiteral("The XDG GlobalShortcuts portal is unavailable. Install xdg-desktop-portal and a desktop portal backend."));
        return;
    }
    if (m_portalRegistered) {
        startPortalSession();
        return;
    }
    if (m_registryRequestPending) return;

    QString identityError;
    if (!ensureDesktopIdentity(&identityError)) {
        emit triggerStatusChanged(identityError);
        return;
    }

    m_registryRequestPending = true;
    const QString owner = m_portalOwner;
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                           QString::fromLatin1(kPortalPath),
                                                           QString::fromLatin1(kRegistryInterface),
                                                           QStringLiteral("Register"));
    message << m_appId << QVariantMap{};
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 2500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, owner] {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        if (owner != m_portalOwner) return;
        m_registryRequestPending = false;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            // Only genuinely old portals may skip this handshake. An identity
            // failure must never be followed by an anonymous CreateSession.
            if (reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod")
                && reply.errorName() != QStringLiteral("org.freedesktop.DBus.Error.UnknownInterface")) {
                // GIO's desktop-file monitor can lag behind a newly created
                // local-build entry. Failed Register calls do not claim the ID.
                if (reply.errorMessage().contains(QStringLiteral("App info not found")) && m_identityRetries++ < 3) {
                    QTimer::singleShot(500, this, &LinuxBackend::ensurePortalRegistered);
                    return;
                }
                emit triggerStatusChanged(QStringLiteral("Could not register Arcade Wheel with the desktop portal: %1")
                                              .arg(reply.errorMessage()));
                return;
            }
        }
        m_portalRegistered = true;
        startPortalSession();
    });
}

void LinuxBackend::startPortalSession()
{
    if (!m_triggerSupported || !m_portalRegistered || m_portalOwner.isEmpty()
        || !m_sessionHandle.isEmpty() || m_sessionRequestPending) return;
    m_sessionRequestPending = true;
    const quint64 generation = m_triggerGeneration;
    const QString token = QStringLiteral("arcade_wheel_create_%1").arg(++m_requestCounter);
    QString sender = m_bus.baseService();
    if (sender.startsWith(QLatin1Char(':'))) sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath = QStringLiteral("%1/request/%2/%3")
                                     .arg(QString::fromLatin1(kPortalPath), sender, token);

    auto *request = new LinuxPortalRequest(
        m_bus,
        QString::fromLatin1(kPortalService),
        predictedPath,
        this,
        [this, generation](uint response, const QVariantMap &results, const QString &path) {
            for (auto it = m_portalRequests.begin(); it != m_portalRequests.end();) {
                if (*it && (*it)->requestPath() == path) it = m_portalRequests.erase(it);
                else ++it;
            }
            portalRequestFinished(PortalRequestKind::CreateSession, generation, path, response, results);
        });
    m_portalRequests.insert(request);

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("session_handle_token"), QStringLiteral("arcade_wheel_session_%1").arg(m_requestCounter));
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                           QString::fromLatin1(kPortalPath),
                                                           QString::fromLatin1(kGlobalShortcutsInterface),
                                                           QStringLiteral("CreateSession"));
    message << options;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 5000), this);
    QPointer<LinuxPortalRequest> requestGuard(request);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, requestGuard, generation] {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (requestGuard) {
                m_portalRequests.remove(requestGuard);
                requestGuard->cancel();
            }
            if (generation == m_triggerGeneration) {
                m_sessionRequestPending = false;
                emit triggerStatusChanged(QStringLiteral("The XDG GlobalShortcuts portal could not create a shortcut session: %1")
                                              .arg(reply.errorMessage()));
            }
            return;
        }
        const QList<QVariant> arguments = reply.arguments();
        if (requestGuard && !arguments.isEmpty()) {
            const QDBusObjectPath path = arguments.first().value<QDBusObjectPath>();
            if (!path.path().isEmpty()) requestGuard->setRequestPath(path.path());
        }
    });
}

void LinuxBackend::bindPortalShortcut(const QString &sessionHandle, quint64 generation)
{
    if (generation != m_triggerGeneration || !m_triggerSupported || sessionHandle.isEmpty()) {
        closePortalSession(sessionHandle);
        return;
    }
    const QString token = QStringLiteral("arcade_wheel_bind_%1").arg(++m_requestCounter);
    QString sender = m_bus.baseService();
    if (sender.startsWith(QLatin1Char(':'))) sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath = QStringLiteral("%1/request/%2/%3")
                                     .arg(QString::fromLatin1(kPortalPath), sender, token);

    auto *request = new LinuxPortalRequest(
        m_bus,
        QString::fromLatin1(kPortalService),
        predictedPath,
        this,
        [this, generation](uint response, const QVariantMap &results, const QString &path) {
            for (auto it = m_portalRequests.begin(); it != m_portalRequests.end();) {
                if (*it && (*it)->requestPath() == path) it = m_portalRequests.erase(it);
                else ++it;
            }
            portalRequestFinished(PortalRequestKind::BindShortcuts, generation, path, response, results);
        });
    m_portalRequests.insert(request);

    PortalShortcut shortcut;
    shortcut.id = QString::fromLatin1(kShortcutId);
    shortcut.properties.insert(QStringLiteral("description"), QStringLiteral("Open the Arcade Wheel"));
    shortcut.properties.insert(QStringLiteral("preferred_trigger"), m_preferredTrigger);
    const QList<PortalShortcut> shortcuts{shortcut};
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);

    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                           QString::fromLatin1(kPortalPath),
                                                           QString::fromLatin1(kGlobalShortcutsInterface),
                                                           QStringLiteral("BindShortcuts"));
    message << QDBusObjectPath(sessionHandle)
            << QVariant::fromValue(shortcuts)
            << QString()
            << options;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 5000), this);
    QPointer<LinuxPortalRequest> requestGuard(request);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, requestGuard, generation, sessionHandle] {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (requestGuard) {
                m_portalRequests.remove(requestGuard);
                requestGuard->cancel();
            }
            closePortalSession(sessionHandle);
            if (generation == m_triggerGeneration) {
                m_sessionHandle.clear();
                emit triggerStatusChanged(QStringLiteral("The desktop portal rejected the global shortcut: %1")
                                              .arg(reply.errorMessage()));
            }
            return;
        }
        const QList<QVariant> arguments = reply.arguments();
        if (requestGuard && !arguments.isEmpty()) {
            const QDBusObjectPath path = arguments.first().value<QDBusObjectPath>();
            if (!path.path().isEmpty()) requestGuard->setRequestPath(path.path());
        }
    });
}

void LinuxBackend::portalRequestFinished(PortalRequestKind kind,
                                         quint64 generation,
                                         const QString &requestPath,
                                         uint response,
                                         const QVariantMap &results)
{
    Q_UNUSED(requestPath)
    if (kind == PortalRequestKind::CreateSession && generation == m_triggerGeneration)
        m_sessionRequestPending = false;
    if (response != 0) {
        if (generation != m_triggerGeneration) return;
        if (results.value(QStringLiteral("arcade_timeout")).toBool()) {
            closePortalSession(std::exchange(m_sessionHandle, QString()));
            emit triggerStatusChanged(QStringLiteral("The desktop shortcut request timed out. Select the shortcut again to retry."));
        } else if (kind == PortalRequestKind::CreateSession) {
            emit triggerStatusChanged(QStringLiteral("The desktop declined the global shortcut session."));
        } else {
            closePortalSession(std::exchange(m_sessionHandle, QString()));
            emit triggerStatusChanged(QStringLiteral("The global shortcut was not granted in the portal."));
        }
        return;
    }

    if (kind == PortalRequestKind::CreateSession) {
        QString session;
        const QVariant value = results.value(QStringLiteral("session_handle"));
        if (value.canConvert<QDBusObjectPath>()) session = value.value<QDBusObjectPath>().path();
        if (session.isEmpty()) session = value.toString(); // The portal spec retains this historical string type.
        if (session.isEmpty() || !session.startsWith(QLatin1Char('/'))) {
            if (generation == m_triggerGeneration) {
                emit triggerStatusChanged(QStringLiteral("The global shortcuts portal returned an invalid session."));
            }
            return;
        }
        if (generation != m_triggerGeneration || !m_triggerSupported) {
            closePortalSession(session);
            return;
        }
        m_sessionHandle = session;
        m_bus.connect(QString::fromLatin1(kPortalService), session,
                      QString::fromLatin1(kSessionInterface), "Closed", this,
                      SLOT(onPortalSessionClosed(QVariantMap,QDBusMessage)));
        bindPortalShortcut(session, generation);
        return;
    }

    if (generation != m_triggerGeneration) return;
    QString bindingError;
    if (isHyprland() && !installHyprlandBinding(&bindingError)) {
        emit triggerStatusChanged(bindingError);
        return;
    }
    emit triggerStatusChanged(triggerReadyStatus());
}

void LinuxBackend::closePortalSession(const QString &sessionHandle)
{
    if (sessionHandle.isEmpty() || !sessionHandle.startsWith(QLatin1Char('/'))) return;
    m_bus.disconnect(QString::fromLatin1(kPortalService), sessionHandle,
                     QString::fromLatin1(kSessionInterface), "Closed", this,
                     SLOT(onPortalSessionClosed(QVariantMap,QDBusMessage)));
    QDBusMessage close = QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                                         sessionHandle,
                                                         QString::fromLatin1(kSessionInterface),
                                                         QStringLiteral("Close"));
    m_bus.asyncCall(close, 1500);
}

void LinuxBackend::onPortalSessionClosed(const QVariantMap &details, const QDBusMessage &message)
{
    Q_UNUSED(details)
    if (m_sessionHandle.isEmpty() || message.path() != m_sessionHandle) return;
    if (m_triggerDown) { m_triggerDown = false; m_triggerClock.invalidate(); emit triggerCancelled(); }
    removeHyprlandBinding();
    closePortalSession(std::exchange(m_sessionHandle, QString()));
    ++m_triggerGeneration;
    m_sessionRequestPending = false;
    emit triggerStatusChanged(QStringLiteral("Reconnecting shortcut…"));
    QTimer::singleShot(500, this, &LinuxBackend::ensurePortalRegistered);
}

void LinuxBackend::onPortalOwnerChanged(const QString &service,
                                        const QString &oldOwner,
                                        const QString &newOwner)
{
    Q_UNUSED(oldOwner)
    if (service != QString::fromLatin1(kPortalService)) return;
    ++m_triggerGeneration; // Invalidate any response tied to the previous portal owner.
    if (m_triggerDown) { m_triggerDown = false; emit triggerCancelled(); }
    removeHyprlandBinding();
    closePortalSession(std::exchange(m_sessionHandle, QString()));
    m_portalOwner = newOwner;
    m_portalRegistered = false;
    m_registryRequestPending = false;
    m_sessionRequestPending = false;
    m_identityRetries = 0;
    m_sessionHandle.clear();
    if (newOwner.isEmpty()) {
        emit triggerStatusChanged(QStringLiteral("The XDG GlobalShortcuts portal disconnected."));
        return;
    }
    ensurePortalRegistered();
}

void LinuxBackend::onShortcutActivated(const QDBusObjectPath &session,
                                       const QString &shortcutId,
                                       qulonglong timestamp,
                                       const QVariantMap &options)
{
    Q_UNUSED(timestamp)
    Q_UNUSED(options)
    if (m_recordingShortcut || !m_triggerSupported || m_triggerDown || shortcutId != QString::fromLatin1(kShortcutId)
        || session.path() != m_sessionHandle) {
        return;
    }
    m_triggerDown = true;
    m_triggerClock.restart();
    emit triggerPressed();
}

void LinuxBackend::onShortcutDeactivated(const QDBusObjectPath &session,
                                         const QString &shortcutId,
                                         qulonglong timestamp,
                                         const QVariantMap &options)
{
    Q_UNUSED(timestamp)
    Q_UNUSED(options)
    if (shortcutId != QString::fromLatin1(kShortcutId) || session.path() != m_sessionHandle) return;
    clearActiveTrigger();
}

void LinuxBackend::clearActiveTrigger()
{
    if (!m_triggerDown) return;
    const bool isTap = m_holdThresholdMs > 0 && m_triggerClock.isValid()
        && m_triggerClock.elapsed() < m_holdThresholdMs;
    m_triggerDown = false;
    m_triggerClock.invalidate();
    emit triggerReleased();
    if (isTap && isHyprland() && m_bindingInstalled && m_luaBinding) {
        QString error;
        if (!replayTriggerTap(&error)) {
            emit triggerStatusChanged(QStringLiteral("Could not pass the short tap to the active app: %1")
                                          .arg(error));
        }
    }
}

QPointF LinuxBackend::cursorPosition() const
{
    if (isHyprland()) {
        QString response;
        if (sendHyprlandCommand(QStringLiteral("cursorpos"), &response)) {
            const QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
            if (doc.isObject()) {
                const QJsonObject point = doc.object();
                if (point.value(QStringLiteral("x")).isDouble() && point.value(QStringLiteral("y")).isDouble()) {
                    return {point.value(QStringLiteral("x")).toDouble(), point.value(QStringLiteral("y")).toDouble()};
                }
            }
            static const QRegularExpression coordinates(QStringLiteral("^\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*$"));
            const auto match = coordinates.match(response.trimmed());
            if (match.hasMatch()) return {match.captured(1).toDouble(), match.captured(2).toDouble()};
        }
    }
    const QPoint cursor = QCursor::pos();
    return {static_cast<qreal>(cursor.x()), static_cast<qreal>(cursor.y())};
}

bool LinuxBackend::prepareOverlay(QQuickWindow *window, QString *error)
{
    if (!window) {
        if (error) *error = QStringLiteral("The wheel overlay window was not created.");
        return false;
    }
    if (m_overlayWindow != window) {
        m_overlayWindow = window;
        connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible) {
                m_overlayMapped = false;
                m_pendingPointer.reset();
            }
        });
    }
    window->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    window->setColor(Qt::transparent);

    QScreen *screen = window->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        window->setScreen(screen);
        window->setGeometry(screen->geometry());
    }

#ifdef ARCADE_HAVE_LAYERSHELLQT
    if (QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
        LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(window);
        if (!layerWindow) {
            if (error) *error = QStringLiteral("LayerShellQt could not attach the overlay to the Wayland layer shell.");
            return false;
        }
        layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorBottom;
        anchors |= LayerShellQt::Window::AnchorLeft;
        anchors |= LayerShellQt::Window::AnchorRight;
        layerWindow->setAnchors(anchors);
        layerWindow->setExclusiveZone(-1);
        layerWindow->setScope(QStringLiteral("arcade-wheel"));
        if (isHyprland()) {
            // Suppress only the compositor's layer slide. QML owns our motion.
            // Check the Lua handle each time, including after a compositor reload.
            QString response;
            const QString rule = QStringLiteral("eval if not _G.arcadeWheelLayerRule then _G.arcadeWheelLayerRule = hl.layer_rule({name='arcade-wheel-motion', match={namespace='^arcade-wheel$'}, no_anim=true}) else _G.arcadeWheelLayerRule:set_enabled(true) end");
            m_overlayRuleInstalled = sendHyprlandCommand(rule, &response) && response == QStringLiteral("ok");
            if (!m_overlayRuleInstalled)
                sendHyprlandCommand(QStringLiteral("keyword layerrule noanim, ^arcade-wheel$"));
        }
        if (screen) layerWindow->setDesiredSize(screen->geometry().size());
        return true;
    }
#endif

    if (QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
        if (error) {
            *error = QStringLiteral("LayerShellQt was not found at build time. The overlay will use Qt's best-effort topmost window behavior.");
        }
    }
    return true;
}

QVector<DiscoveredApplication> LinuxBackend::applications() const
{
    const QVector<DesktopEntry> entries = scanDesktopEntries();
    m_desktopEntries.clear();
    QVector<DiscoveredApplication> result;
    result.reserve(entries.size());
    for (const DesktopEntry &entry : entries) {
        m_desktopEntries.insert(entry.application.id, entry);
        result.append(entry.application);
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    return result;
}

QVector<LinuxBackend::DesktopEntry> LinuxBackend::scanDesktopEntries() const
{
    QStringList dataRoots;
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty()) dataRoots.append(dataHome);
    const QString xdgDataDirs = qEnvironmentVariable("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    for (const QString &root : xdgDataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        if (!root.isEmpty() && !dataRoots.contains(root)) dataRoots.append(root);
    }

    const QStringList currentDesktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
                                            .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    QSet<QString> seenIds;
    QVector<DesktopEntry> entries;
    for (const QString &root : std::as_const(dataRoots)) {
        const QString applicationsPath = QDir(root).filePath(QStringLiteral("applications"));
        if (!QDir(applicationsPath).exists()) continue;
        QDirIterator iterator(applicationsPath,
                              {QStringLiteral("*.desktop")},
                              QDir::Files | QDir::Readable,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString file = iterator.next();
            const QString relative = QDir(applicationsPath).relativeFilePath(file);
            QString desktopId = relative;
            desktopId.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seenIds.contains(desktopId)) continue;
            seenIds.insert(desktopId); // Hidden user entries mask a system entry with the same desktop ID.
            DesktopEntry entry = readDesktopEntry(file, desktopId);
            if (entry.application.id.isEmpty()) continue;

            const QHash<QString, QString> values = parseDesktopFile(file);
            const QStringList onlyShowIn = semicolonList(values.value(QStringLiteral("OnlyShowIn")));
            const QStringList notShowIn = semicolonList(values.value(QStringLiteral("NotShowIn")));
            bool allowedByOnly = onlyShowIn.isEmpty();
            bool deniedByNot = false;
            for (const QString &desktop : currentDesktops) {
                for (const QString &candidate : onlyShowIn) {
                    if (candidate.compare(desktop, Qt::CaseInsensitive) == 0) allowedByOnly = true;
                }
                for (const QString &candidate : notShowIn) {
                    if (candidate.compare(desktop, Qt::CaseInsensitive) == 0) deniedByNot = true;
                }
            }
            if (allowedByOnly && !deniedByNot) entries.append(entry);
        }
    }
    return entries;
}

LinuxBackend::DesktopEntry LinuxBackend::readDesktopEntry(const QString &path, const QString &desktopId) const
{
    DesktopEntry entry;
    const QHash<QString, QString> values = parseDesktopFile(path);
    if (values.isEmpty() || values.value(QStringLiteral("Type")) != QStringLiteral("Application")) return entry;
    if (parseDesktopBoolean(values, QStringLiteral("Hidden"))
        || parseDesktopBoolean(values, QStringLiteral("NoDisplay"))) return entry;

    entry.desktopFile = path;
    entry.exec = values.value(QStringLiteral("Exec"));
    entry.workingDirectory = values.value(QStringLiteral("Path"));
    entry.startupWmClass = values.value(QStringLiteral("StartupWMClass"));
    entry.icon = values.value(QStringLiteral("Icon"));
    entry.terminal = parseDesktopBoolean(values, QStringLiteral("Terminal"));
    entry.dbusActivatable = parseDesktopBoolean(values, QStringLiteral("DBusActivatable"));
    if (entry.exec.isEmpty() && !entry.dbusActivatable) return {};

    const QString tryExec = values.value(QStringLiteral("TryExec")).trimmed();
    if (!tryExec.isEmpty()) {
        if (QDir::isAbsolutePath(tryExec)) {
            const QFileInfo executable(tryExec);
            if (!executable.exists() || !executable.isExecutable()) return {};
        } else if (QStandardPaths::findExecutable(tryExec).isEmpty()) {
            return {};
        }
    }

    entry.application.id = desktopId;
    entry.application.name = localizedValue(values, QStringLiteral("Name"));
    if (entry.application.name.isEmpty()) entry.application.name = QFileInfo(path).completeBaseName();
    entry.application.icon = entry.icon;
    entry.application.description = localizedValue(values, QStringLiteral("Comment"));
    entry.application.launchTarget = path;
    return entry;
}

bool LinuxBackend::launchApplication(const QString &id, bool focusExisting, QString *error)
{
    DesktopEntry entry = m_desktopEntries.value(id);
    if (entry.application.id.isEmpty()) {
        applications();
        entry = m_desktopEntries.value(id);
    }
    if (entry.application.id.isEmpty()) {
        if (error) *error = QStringLiteral("The installed application is unavailable: %1").arg(id);
        return false;
    }
    return launchDesktopEntry(entry, focusExisting, error);
}

bool LinuxBackend::launchDesktopEntry(const DesktopEntry &entry, bool focusExisting, QString *error)
{
    if (focusExisting && focusHyprlandWindow(entry)) return true;
    // GIO implements the desktop-entry Exec grammar, field codes, D-Bus
    // activation and terminal handling. Do not interpret Exec as shell syntax.
    auto *info = g_desktop_app_info_new_from_filename(QFile::encodeName(entry.desktopFile).constData());
    if (!info) {
        if (error) *error = QStringLiteral("The application entry is invalid or its executable is missing: %1").arg(entry.application.name);
        return false;
    }
    GError *launchError = nullptr;
    const bool launched = g_app_info_launch(G_APP_INFO(info), nullptr, nullptr, &launchError);
    if (!launched && error)
        *error = QStringLiteral("Could not launch %1: %2").arg(entry.application.name,
            launchError ? QString::fromUtf8(launchError->message) : QStringLiteral("unknown desktop error"));
    if (launchError) g_error_free(launchError);
    g_object_unref(info);
    return launched;
}

bool LinuxBackend::focusHyprlandWindow(const DesktopEntry &entry) const
{
    if (!isHyprland()) return false;
    QString response;
    if (!sendHyprlandCommand(QStringLiteral("clients"), &response, true)) return false;
    const auto clients = QJsonDocument::fromJson(response.toUtf8()).array();
    QStringList classes{entry.startupWmClass, QFileInfo(entry.desktopFile).completeBaseName()};
    classes.removeAll(QString());
    for (const auto &value : clients) {
        const auto client = value.toObject();
        if (!client.value(QStringLiteral("mapped")).toBool(true)) continue;
        const auto currentClass = client.value(QStringLiteral("class")).toString();
        const auto initialClass = client.value(QStringLiteral("initialClass")).toString();
        bool matches = false;
        for (const auto &candidate : classes)
            matches |= candidate.compare(currentClass, Qt::CaseInsensitive) == 0 ||
                       candidate.compare(initialClass, Qt::CaseInsensitive) == 0;
        if (!matches) continue;
        const auto address = client.value(QStringLiteral("address")).toString();
        static const QRegularExpression validAddress(QStringLiteral("^0x[0-9a-fA-F]+$"));
        if (!validAddress.match(address).hasMatch()) continue;
        return sendHyprlandCommand(QStringLiteral("dispatch focuswindow address:%1").arg(address), &response)
            && response.trimmed() == QStringLiteral("ok");
    }
    // A generic focuswindow 'ok' does NOT mean a matching window exists.
    return false;
}

bool LinuxBackend::movePointer(const QPointF &position)
{
    if (!isHyprland()) return false;
    watchHyprlandReloads(); // Reconnects the event socket if it was lost.
    // Qt submitting a frame does not mean Hyprland has mapped that surface.
    // Defer until openlayer so the warp establishes pointer focus on the wheel.
    if (m_overlayWindow && m_overlayWindow->isVisible() && !m_overlayMapped) {
        m_pendingPointer = position;
        if (!m_hyprlandEvents || m_hyprlandEvents->state() != QLocalSocket::ConnectedState) {
            // Without the event socket openlayer never arrives. Warp after the
            // surface has certainly been mapped rather than never warping.
            QTimer::singleShot(150, this, [this] {
                if (!m_pendingPointer || !m_overlayWindow || !m_overlayWindow->isVisible()) return;
                const QPointF pending = *m_pendingPointer;
                m_pendingPointer.reset();
                m_overlayMapped = true;
                movePointer(pending);
            });
        }
        return true;
    }
    QString response;
    const int x = qRound(position.x());
    const int y = qRound(position.y());
    const QString lua = QStringLiteral("eval hl.dispatch(hl.dsp.cursor.move({x = %1, y = %2}))").arg(x).arg(y);
    if (sendHyprlandCommand(lua, &response) && response.trimmed() == QStringLiteral("ok"))
        return true;
    return sendHyprlandCommand(QStringLiteral("dispatch movecursor %1 %2").arg(x).arg(y), &response)
        && response.trimmed() == QStringLiteral("ok");
}

bool LinuxBackend::performSystemAction(const QString &id, QString *error)
{
    if (id == QStringLiteral("media:play-pause") || id == QStringLiteral("media:next")
        || id == QStringLiteral("media:previous")) {
        const QString action = id.mid(QStringLiteral("media:").size());
        const QString playerctl = QStandardPaths::findExecutable(QStringLiteral("playerctl"));
        if (!playerctl.isEmpty()) return launchDetached(playerctl, {action}, {}, error);

        QString method;
        if (action == QStringLiteral("play-pause")) method = QStringLiteral("PlayPause");
        else if (action == QStringLiteral("next")) method = QStringLiteral("Next");
        else method = QStringLiteral("Previous");
        const QDBusReply<QStringList> names = m_bus.interface()->registeredServiceNames();
        if (names.isValid()) {
            for (const QString &service : names.value()) {
                if (!service.startsWith(QStringLiteral("org.mpris.MediaPlayer2."))) continue;
                QDBusMessage message = QDBusMessage::createMethodCall(service,
                                                                      QStringLiteral("/org/mpris/MediaPlayer2"),
                                                                      QStringLiteral("org.mpris.MediaPlayer2.Player"),
                                                                      method);
                m_bus.asyncCall(message, 1000);
                return true;
            }
        }
        if (error) *error = QStringLiteral("No MPRIS media player is available. Install playerctl or start a compatible player.");
        return false;
    }

    if (id == QStringLiteral("screenshot")) {
        QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (pictures.isEmpty()) pictures = QDir::homePath() + QStringLiteral("/Pictures");
        if (!QDir().mkpath(pictures)) {
            if (error) *error = QStringLiteral("The Pictures directory could not be created.");
            return false;
        }
        const QString file = QDir(pictures).filePath(
            QStringLiteral("Arcade Wheel-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))));
        QString program = QStandardPaths::findExecutable(QStringLiteral("grim"));
        QStringList args;
        if (!program.isEmpty()) args = {file};
        else {
            program = QStandardPaths::findExecutable(QStringLiteral("gnome-screenshot"));
            if (!program.isEmpty()) args = {QStringLiteral("-f"), file};
            else {
                program = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
                if (!program.isEmpty()) args = {QStringLiteral("-b"), QStringLiteral("-o"), file};
            }
        }
        if (program.isEmpty()) {
            if (error) *error = QStringLiteral("No screenshot utility is installed. Install grim, gnome-screenshot, or spectacle.");
            return false;
        }
        return launchDetached(program, args, {}, error);
    }

    if (id.startsWith(QStringLiteral("desktop:"))) {
        if (!isHyprland()) {
            if (error) *error = QStringLiteral("Hyprland desktop actions are only available in a Hyprland session.");
            return false;
        }
        const QString action = id.mid(QStringLiteral("desktop:").size());
        static const QHash<QString, QString> allowed = {
            {QStringLiteral("togglefloating"), QStringLiteral("dispatch togglefloating")},
            {QStringLiteral("fullscreen"), QStringLiteral("dispatch fullscreen")},
            {QStringLiteral("pin"), QStringLiteral("dispatch pin")},
            {QStringLiteral("movefocus-left"), QStringLiteral("dispatch movefocus l")},
            {QStringLiteral("movefocus-right"), QStringLiteral("dispatch movefocus r")},
            {QStringLiteral("movefocus-up"), QStringLiteral("dispatch movefocus u")},
            {QStringLiteral("movefocus-down"), QStringLiteral("dispatch movefocus d")},
            {QStringLiteral("workspace-next"), QStringLiteral("dispatch workspace e+1")},
            {QStringLiteral("workspace-previous"), QStringLiteral("dispatch workspace e-1")}
        };
        if (!allowed.contains(action)) {
            if (error) *error = QStringLiteral("Unsupported Hyprland action. Allowed actions: togglefloating, fullscreen, pin, movefocus-left/right/up/down, workspace-next, workspace-previous.");
            return false;
        }
        QString response;
        if (!sendHyprlandCommand(allowed.value(action), &response)
            || !response.trimmed().startsWith(QStringLiteral("ok"), Qt::CaseInsensitive)) {
            if (error) *error = response.trimmed().isEmpty()
                ? QStringLiteral("Hyprland did not accept the desktop action.")
                : response.trimmed();
            return false;
        }
        return true;
    }

    if (id == QStringLiteral("lock")) {
        const QString loginctl = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
        if (!loginctl.isEmpty()) return launchDetached(loginctl, {QStringLiteral("lock-session")}, {}, error);
        for (const QString &locker : {QStringLiteral("hyprlock"), QStringLiteral("swaylock"), QStringLiteral("gtklock")}) {
            const QString path = QStandardPaths::findExecutable(locker);
            if (!path.isEmpty()) return launchDetached(path, {}, {}, error);
        }
        if (error) *error = QStringLiteral("No session locker is available. Install a system session manager or a screen locker.");
        return false;
    }

    if (id == QStringLiteral("suspend")) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (systemctl.isEmpty()) {
            if (error) *error = QStringLiteral("systemctl is unavailable; this desktop does not expose a supported suspend command.");
            return false;
        }
        return launchDetached(systemctl, {QStringLiteral("suspend")}, {}, error);
    }

    if (id == QStringLiteral("poweroff")) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (systemctl.isEmpty()) {
            if (error) *error = QStringLiteral("systemctl is unavailable; this desktop does not expose a supported poweroff command.");
            return false;
        }
        return launchDetached(systemctl, {QStringLiteral("poweroff")}, {}, error);
    }

    if (id == QStringLiteral("logout")) {
        if (isHyprland()) {
            QString response;
            if (sendHyprlandCommand(QStringLiteral("dispatch exit"), &response)
                && response.trimmed().startsWith(QStringLiteral("ok"), Qt::CaseInsensitive)) return true;
            if (error) *error = response.trimmed().isEmpty() ? QStringLiteral("Hyprland did not accept the logout request.") : response.trimmed();
            return false;
        }
        const QString gnomeQuit = QStandardPaths::findExecutable(QStringLiteral("gnome-session-quit"));
        if (!gnomeQuit.isEmpty()) return launchDetached(gnomeQuit, {QStringLiteral("--logout"), QStringLiteral("--no-prompt")}, {}, error);
        const QString sessionId = qEnvironmentVariable("XDG_SESSION_ID");
        const QString loginctl = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
        if (!sessionId.isEmpty() && !loginctl.isEmpty()) {
            return launchDetached(loginctl, {QStringLiteral("terminate-session"), sessionId}, {}, error);
        }
        if (error) *error = QStringLiteral("No supported logout method was found for this desktop session.");
        return false;
    }

    if (error) *error = QStringLiteral("Unsupported Linux system action: %1").arg(id);
    return false;
}

bool LinuxBackend::setStartOnLogin(bool enabled, QString *error)
{
    const QString path = autostartDesktopFile();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("The XDG config directory is unavailable.");
        return false;
    }
    QByteArray previous;
    const bool existed = QFile::exists(path);
    if (existed) {
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Could not read the existing Arcade Wheel autostart entry.");
            return false;
        }
        previous = current.readAll();
    }
    // Entries this app wrote need no backup; only a hand-edited entry does.
    // Backing up our own file left a new copy behind on every toggle.
    const bool ownEntry = previous.startsWith("[Desktop Entry]\nType=Application\nName=Arcade Wheel\n"
                                              "Comment=Start the Arcade Wheel background service\n");
    if (!enabled) {
        if (!existed) return true;
        const QString backup = path + QStringLiteral(".backup-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (!ownEntry && !QFile::copy(path, backup)) {
            if (error) *error = QStringLiteral("Could not back up the Arcade Wheel autostart entry; it was not removed.");
            return false;
        }
        if (!QFile::remove(path)) {
            if (error) *error = QStringLiteral("Could not remove the Arcade Wheel autostart entry.");
            return false;
        }
        return !QFile::exists(path);
    }

    const QString imagePath = qEnvironmentVariable("APPIMAGE");
    const QString executable = QFileInfo(imagePath.isEmpty()
        ? QCoreApplication::applicationFilePath() : imagePath).canonicalFilePath();
    if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) {
        if (error) *error = QStringLiteral("The running Arcade Wheel executable could not be located.");
        return false;
    }
    for (const QString &root : {QStringLiteral("/tmp"), QStringLiteral("/var/tmp"), QStringLiteral("/run")}) {
        if (executable == root || executable.startsWith(root + QLatin1Char('/'))) {
            if (error) *error = QStringLiteral("Move Arcade Wheel out of a temporary directory before enabling start on login.");
            return false;
        }
    }
    const QFileInfo target(path);
    if (!QDir().mkpath(target.absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the per-user autostart directory.");
        return false;
    }
    const QByteArray contents = QStringLiteral("[Desktop Entry]\n"
                                               "Type=Application\n"
                                               "Name=Arcade Wheel\n"
                                               "Comment=Start the Arcade Wheel background service\n"
                                               "Exec=%1 --background\n"
                                               "Terminal=false\n"
                                               "X-GNOME-Autostart-enabled=true\n")
                                    .arg(escapeDesktopExecPath(executable))
                                    .toUtf8();
    if (existed && previous == contents) return true;
    if (existed && !ownEntry) {
        const QString backup = path + QStringLiteral(".backup-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (!QFile::copy(path, backup)) {
            if (error) *error = QStringLiteral("Could not back up the existing Arcade Wheel autostart entry; it was not changed.");
            return false;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Could not write the Arcade Wheel autostart entry: %1").arg(file.errorString());
        return false;
    }
    if (file.write(contents) != contents.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Could not save the Arcade Wheel autostart entry: %1").arg(file.errorString());
        return false;
    }
    QFile verify(path);
    if (!verify.open(QIODevice::ReadOnly) || verify.readAll() != contents) {
        if (error) *error = QStringLiteral("The Arcade Wheel autostart entry could not be verified after writing.");
        return false;
    }
    return true;
}

bool LinuxBackend::startOnLogin() const
{
    return QFileInfo::exists(autostartDesktopFile());
}

QString LinuxBackend::autostartDesktopFile() const
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (config.isEmpty()) return {};
    return QDir(config).filePath(QStringLiteral("autostart/arcade-wheel.desktop"));
}

bool LinuxBackend::launchDetached(const QString &program,
                                 const QStringList &arguments,
                                 const QString &workingDirectory,
                                 QString *error) const
{
    QString executable = program;
    if (executable.contains(QLatin1Char('/'))) {
        const QFileInfo info(executable);
        if (!info.exists() || !info.isExecutable()) {
            if (error) *error = QStringLiteral("Executable not found or not executable: %1").arg(program);
            return false;
        }
    } else {
        executable = QStandardPaths::findExecutable(program);
        if (executable.isEmpty()) {
            if (error) *error = QStringLiteral("Executable not found in PATH: %1").arg(program);
            return false;
        }
    }
    if (!workingDirectory.isEmpty() && !QFileInfo(workingDirectory).isDir()) {
        if (error) *error = QStringLiteral("Application working directory does not exist: %1").arg(workingDirectory);
        return false;
    }
    if (!QProcess::startDetached(executable, arguments, workingDirectory)) {
        if (error) *error = QStringLiteral("Could not start %1.").arg(program);
        return false;
    }
    return true;
}

bool LinuxBackend::isHyprland() const
{
    return !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty()
        && !hyprlandSocketPath().isEmpty();
}

QString LinuxBackend::hyprlandSocketPath() const
{
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    static const QRegularExpression safeSignature(QStringLiteral("^[A-Za-z0-9_-]+$"));
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty() || !safeSignature.match(signature).hasMatch()) return {};
    return QDir(runtime).filePath(QStringLiteral("hypr/%1/.socket.sock").arg(signature));
}

bool LinuxBackend::sendHyprlandCommand(const QString &command, QString *reply, bool json) const
{
    const QString socketPath = hyprlandSocketPath();
    if (socketPath.isEmpty() || command.contains(QLatin1Char('\n')) || command.contains(QLatin1Char('\0'))) return false;
    QLocalSocket socket;
    socket.connectToServer(socketPath, QIODevice::ReadWrite);
    if (!socket.waitForConnected(60)) return false;
    const QByteArray request = command.toUtf8();
    const QByteArray wireCommand = (json ? QByteArrayLiteral("j/") : QByteArrayLiteral("/")) + request;
    if (socket.write(wireCommand) != wireCommand.size() || (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(60))) {
        socket.abort();
        return false;
    }

    QByteArray response;
    QElapsedTimer deadline;
    deadline.start();
    while (socket.state() == QLocalSocket::ConnectedState && deadline.elapsed() < 120) {
        if (socket.bytesAvailable() || socket.waitForReadyRead(30)) response += socket.readAll();
    }
    response += socket.readAll();
    socket.disconnectFromServer();
    if (reply) *reply = QString::fromUtf8(response).trimmed();
    return !response.isEmpty();
}

namespace {
int shortcutMask(const QStringList &parts)
{
    int mask = 0;
    for (const auto &part : parts) {
        if (part == QStringLiteral("CTRL")) mask |= 4;
        if (part == QStringLiteral("ALT")) mask |= 8;
        if (part == QStringLiteral("SHIFT")) mask |= 1;
        if (part == QStringLiteral("LOGO")) mask |= 64;
        if (part == QStringLiteral("NUM")) mask |= 16;
    }
    return mask;
}
QString modifierText(int mask)
{
    QStringList parts;
    if (mask & 1) parts << QStringLiteral("SHIFT");
    if (mask & 2) parts << QStringLiteral("CAPS");
    if (mask & 4) parts << QStringLiteral("CTRL");
    if (mask & 8) parts << QStringLiteral("ALT");
    if (mask & 16) parts << QStringLiteral("MOD2");
    if (mask & 32) parts << QStringLiteral("MOD3");
    if (mask & 64) parts << QStringLiteral("SUPER");
    if (mask & 128) parts << QStringLiteral("MOD5");
    return parts.join(QLatin1Char(' '));
}
bool matchesChord(const QJsonObject &bind, const QString &key, int mask)
{
    return bind.value(QStringLiteral("key")).toString().compare(key, Qt::CaseInsensitive) == 0
        && bind.value(QStringLiteral("modmask")).toInt() == mask;
}

QString luaStringLiteral(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

QString normalizedBindDisplayKey(QString key)
{
    key.remove(QLatin1Char(' '));
    return key.toLower();
}

QString bindDisplayKey(const QJsonObject &bind)
{
    QStringList modifiers;
    const int mask = bind.value(QStringLiteral("modmask")).toInt();
    if (mask & 4) modifiers << QStringLiteral("CTRL");
    if (mask & 8) modifiers << QStringLiteral("ALT");
    if (mask & 1) modifiers << QStringLiteral("SHIFT");
    if (mask & 16) modifiers << QStringLiteral("MOD2");
    if (mask & 32) modifiers << QStringLiteral("MOD3");
    if (mask & 64) modifiers << QStringLiteral("SUPER");
    if (mask & 128) modifiers << QStringLiteral("MOD5");
    const QString key = bind.value(QStringLiteral("key")).toString();
    if (key.isEmpty()) return {};
    modifiers << key;
    return modifiers.join(QStringLiteral(" + "));
}

QString luaRestoreBindCommand(const QJsonObject &bind)
{
    if (bind.value(QStringLiteral("dispatcher")).toString() != QStringLiteral("__lua")) return {};
    const QString callbackId = bind.value(QStringLiteral("arg")).toString();
    static const QRegularExpression validCallbackId(QStringLiteral("^[1-9][0-9]*$"));
    const QString displayKey = bindDisplayKey(bind);
    if (!validCallbackId.match(callbackId).hasMatch() || displayKey.isEmpty()) return {};

    QStringList options;
    const std::pair<const char *, const char *> boolOptions[] = {
        {"locked", "locked"}, {"release", "release"}, {"repeat", "repeating"},
        {"non_consuming", "non_consuming"}, {"auto_consuming", "auto_consuming"},
        {"transparent", "transparent"}, {"ignore_mods", "ignore_mods"},
        {"longPress", "long_press"}, {"dont_inhibit", "dont_inhibit"},
        {"submap_universal", "submap_universal"}, {"click", "click"},
        {"drag", "drag"}, {"allow_input_capture", "allow_input_capture"}
    };
    for (const auto &[jsonField, luaField] : boolOptions) {
        if (bind.value(QLatin1String(jsonField)).toBool())
            options << QString::fromLatin1(luaField) + QStringLiteral(" = true");
    }
    if (bind.value(QStringLiteral("has_description")).toBool())
        options << QStringLiteral("description = ") + luaStringLiteral(bind.value(QStringLiteral("description")).toString());

    const QString submap = bind.value(QStringLiteral("submap")).toString();
    QString bindExpression = QStringLiteral("hl.bind(%1, callback, {%2})")
                                 .arg(luaStringLiteral(displayKey), options.join(QStringLiteral(", ")));
    if (!submap.isEmpty())
        bindExpression = QStringLiteral("hl.define_submap(%1, function() %2 end)")
                             .arg(luaStringLiteral(submap), bindExpression);
    return QStringLiteral("eval local callback = debug.getregistry()[%1]; if type(callback) ~= 'function' then error('original shortcut callback is unavailable') end; %2")
        .arg(callbackId, bindExpression);
}
}

QString LinuxBackend::shortcutValidationError(const QString &shortcut) const
{
    QString error;
    preferredTriggerFromShortcut(shortcut, &error);
    return error;
}

bool LinuxBackend::replayTriggerTap(QString *error) const
{
    if (m_boundKey.isEmpty()) {
        if (error) *error = QStringLiteral("the active trigger binding is unavailable");
        return false;
    }

    const QString command = QStringLiteral(
        "eval hl.dispatch(hl.dsp.send_shortcut({mods = %1, key = %2, window = \"activewindow\"}))")
        .arg(luaStringLiteral(modifierText(m_boundModifiers)), luaStringLiteral(m_boundKey));
    QString response;
    if (!sendHyprlandCommand(command, &response) || response.trimmed() != QStringLiteral("ok")) {
        if (error) *error = response.isEmpty() ? QStringLiteral("Hyprland did not accept the key replay.") : response;
        return false;
    }
    return true;
}

QString LinuxBackend::shortcutConflict(const QString &shortcut) const
{
    QString error;
    const auto preferred = preferredTriggerFromShortcut(shortcut, &error);
    if (preferred.isEmpty()) return error;
    if (!isHyprland()) return {};
    QStringList parts = preferred.split(QLatin1Char('+'));
    const QString key = parts.takeLast();
    const int mask = shortcutMask(parts);
    QString response;
    if (!sendHyprlandCommand(QStringLiteral("binds"), &response, true))
        return QStringLiteral("Could not check desktop shortcuts. Try again when Hyprland is available.");
    QStringList conflicts;
    auto bindings = QJsonDocument::fromJson(response.toUtf8()).array();
    for (const auto &displaced : m_displacedBindings) bindings.append(displaced);
    for (const auto &value : bindings) {
        const auto bind = value.toObject();
        if (!matchesChord(bind,key,mask) || bind.value(QStringLiteral("description")).toString() == QStringLiteral("Arcade Wheel trigger")) continue;
        QString name = bind.value(QStringLiteral("description")).toString();
        if (name.isEmpty()) name = bind.value(QStringLiteral("dispatcher")).toString() + QStringLiteral(" ") + bind.value(QStringLiteral("arg")).toString();
        conflicts << name.trimmed();
    }
    conflicts.removeDuplicates();
    return conflicts.isEmpty() ? QString() : QStringLiteral("%1 is already used by: %2").arg(shortcut, conflicts.join(QStringLiteral("; ")));
}

void LinuxBackend::setShortcutRecording(bool recording)
{
    if (m_recordingShortcut == recording) return;
    m_recordingShortcut = recording;
    if (!isHyprland()) return;
    if (recording) {
        // A scoped submap lets focused Settings receive keys normally consumed
        // by global binds. The compositor-side timeout also survives app failure.
        sendHyprlandCommand(QStringLiteral(R"(eval if _G.arcadeWheelCaptureTimer then _G.arcadeWheelCaptureTimer:set_enabled(false) end; _G.arcadeWheelCapturePrevious = hl.get_current_submap(); _G.arcadeWheelCaptureRestore = function() if hl.get_current_submap() == '__arcade_wheel_capture' then local previous = _G.arcadeWheelCapturePrevious; hl.dispatch(hl.dsp.submap((previous and previous ~= '') and previous or 'reset')) end; if _G.arcadeWheelCaptureGuard then _G.arcadeWheelCaptureGuard:remove(); _G.arcadeWheelCaptureGuard = nil end end; hl.define_submap('__arcade_wheel_capture', function() _G.arcadeWheelCaptureGuard = hl.bind('F35', hl.dsp.no_op()) end); _G.arcadeWheelCaptureTimer = hl.timer(_G.arcadeWheelCaptureRestore, {timeout=15000, type='oneshot'}); hl.dispatch(hl.dsp.submap('__arcade_wheel_capture')) )"));
    } else {
        sendHyprlandCommand(QStringLiteral("eval if _G.arcadeWheelCaptureTimer then _G.arcadeWheelCaptureTimer:set_enabled(false) end; if _G.arcadeWheelCaptureRestore then _G.arcadeWheelCaptureRestore() end"));
    }
}

void LinuxBackend::watchHyprlandReloads()
{
    if (!isHyprland()) return;
    const QString eventSocket = QFileInfo(hyprlandSocketPath()).dir().filePath(QStringLiteral(".socket2.sock"));
    if (m_hyprlandEvents) {
        // A dropped event socket must not silently disable reload handling
        // and openlayer-gated pointer warps for the rest of the session.
        if (m_hyprlandEvents->state() == QLocalSocket::UnconnectedState) {
            m_hyprlandEventBuffer.clear();
            m_hyprlandEvents->connectToServer(eventSocket);
        }
        return;
    }
    m_hyprlandEvents = new QLocalSocket(this);
    connect(m_hyprlandEvents, &QLocalSocket::readyRead, this, [this] {
        m_hyprlandEventBuffer += m_hyprlandEvents->readAll();
        qsizetype end;
        while ((end = m_hyprlandEventBuffer.indexOf('\n')) >= 0) {
            const QByteArray event = m_hyprlandEventBuffer.left(end);
            m_hyprlandEventBuffer.remove(0, end + 1);
            if (event == "openlayer>>arcade-wheel" && m_overlayWindow && m_overlayWindow->isVisible()) {
                m_overlayMapped = true;
                if (m_pendingPointer) {
                    const QPointF position = *m_pendingPointer;
                    m_pendingPointer.reset();
                    movePointer(position);
                }
            }
            if (event == "closelayer>>arcade-wheel") m_overlayMapped = false;
            if (!event.startsWith("configreloaded>>")) continue;
            // Reload recreates Lua state and restores the user's configured
            // binds. Never replay callback IDs from the previous Lua state.
            m_displacedBindings = {};
            m_boundKey.clear();
            m_bindingInstalled = m_luaBinding = m_displacedWithLua = false;
            m_overlayRuleInstalled = false;
            if (m_triggerDown) { m_triggerDown = false; m_triggerClock.invalidate(); emit triggerCancelled(); }
            if (!m_sessionHandle.isEmpty() && m_triggerSupported) {
                QString error;
                if (installHyprlandBinding(&error))
                    emit triggerStatusChanged(triggerReadyStatus());
                else emit triggerStatusChanged(error);
            }
        }
        if (m_hyprlandEventBuffer.size() > 65536) m_hyprlandEventBuffer.clear();
    });
    m_hyprlandEvents->connectToServer(eventSocket);
}

bool LinuxBackend::installHyprlandBinding(QString *error)
{
    if (!isHyprland() || m_sessionHandle.isEmpty()) return false;
    removeHyprlandBinding();
    QStringList parts = m_preferredTrigger.split(QLatin1Char('+'));
    const QString key = parts.takeLast();
    const int mask = shortcutMask(parts);
    QString response;
    // The portal may attribute a terminal-launched process to the terminal's
    // desktop ID. Bind the published ID, never an assumed application ID.
    QString target;
    if (sendHyprlandCommand(QStringLiteral("globalshortcuts"), &response, true)) {
        QStringList candidates;
        for (const auto &value : QJsonDocument::fromJson(response.toUtf8()).array()) {
            const auto entry = value.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            if (name.endsWith(QLatin1Char(':') + QString::fromLatin1(kShortcutId))) candidates << name;
        }
        candidates.removeDuplicates();
        const QString expected = m_appId + QLatin1Char(':') + QString::fromLatin1(kShortcutId);
        if (candidates.contains(expected)) target = expected;
        else if (candidates.size() == 1) target = candidates.first();
    }
    static const QRegularExpression safeTarget(QStringLiteral("^[A-Za-z0-9_.:-]+$"));
    if (!safeTarget.match(target).hasMatch()) {
        if (error) *error = QStringLiteral("The desktop has not published an unambiguous Arcade Wheel shortcut. Reapply the shortcut to reconnect.");
        return false;
    }
    if (!sendHyprlandCommand(QStringLiteral("binds"), &response, true)) {
        if (error) *error = QStringLiteral("Could not read Hyprland shortcuts.");
        return false;
    }
    QJsonArray displaced;
    for (const auto &value : QJsonDocument::fromJson(response.toUtf8()).array()) {
        const auto bind = value.toObject();
        if (!matchesChord(bind,key,mask)) continue;
        if (bind.value(QStringLiteral("description")).toString() != QStringLiteral("Arcade Wheel trigger")) displaced.append(bind);
    }
    if (!displaced.isEmpty() && !m_triggerConfiguration.value(QStringLiteral("overrideConflict")).toBool()) {
        if (error) *error = shortcutConflict(m_shortcutSetting) + QStringLiteral(" Open Trigger settings to override or choose another key.");
        return false;
    }
    m_boundKey = key;
    m_boundModifiers = mask;
    if (!displaced.isEmpty()) {
        const bool luaBindings = std::all_of(displaced.cbegin(), displaced.cend(), [](const QJsonValue &value) {
            const auto bind = value.toObject();
            return bind.value(QStringLiteral("dispatcher")).toString() == QStringLiteral("__lua")
                && !luaRestoreBindCommand(bind).isEmpty();
        });
        QSet<QString> removed;
        for (const auto &value : displaced) {
            const auto bind = value.toObject();
            const QString displayKey = bindDisplayKey(bind);
            const QString normalizedKey = normalizedBindDisplayKey(displayKey);
            if (displayKey.isEmpty() || removed.contains(normalizedKey)) continue;

            const QString command = luaBindings
                ? QStringLiteral("eval hl.unbind(%1)").arg(luaStringLiteral(displayKey))
                : QStringLiteral("keyword unbind %1, %2").arg(modifierText(mask),bind.value(QStringLiteral("key")).toString());
            if (!sendHyprlandCommand(command, &response) || response.trimmed() != QStringLiteral("ok")) {
                removeHyprlandBinding();
                if (error) *error = QStringLiteral("Could not override the desktop shortcut: %1").arg(response);
                return false;
            }
            removed.insert(normalizedKey);
            if (luaBindings) m_displacedWithLua = true;
            for (const auto &entry : displaced) {
                if (normalizedBindDisplayKey(bindDisplayKey(entry.toObject())) == normalizedKey)
                    m_displacedBindings.append(entry);
            }
        }
    }
    // Hyprland 0.55+ Lua configs reject `keyword bind`. Its Lua dispatcher
    // wrapper also requests the matching release edge for the portal shortcut.
    parts.replaceInStrings(QStringLiteral("LOGO"), QStringLiteral("SUPER"));
    parts.replaceInStrings(QStringLiteral("NUM"), QStringLiteral("MOD2"));
    const QString chord = parts.isEmpty() ? key : parts.join(QStringLiteral(" + ")) + QStringLiteral(" + ") + key;
    const QString luaBinding = QStringLiteral(
        "eval if _G.arcadeWheelTrigger then _G.arcadeWheelTrigger:remove() end; "
        "_G.arcadeWheelTrigger = hl.bind(\"%1\", hl.dsp.global(\"%2\"), {description=\"Arcade Wheel trigger\"})")
                                    .arg(chord, target);
    m_boundKey = key;
    m_boundModifiers = mask;
    if (sendHyprlandCommand(luaBinding, &response) && response.trimmed() == QStringLiteral("ok")) {
        m_luaBinding = true;
        m_bindingInstalled = true;
        return true;
    }

    // Retain support for Hyprland's legacy hyprlang config parser.
    const QString binding = QStringLiteral("keyword bindd %1, %2, Arcade Wheel trigger, global, %3")
                                .arg(modifierText(mask), key, target);
    QString legacyResponse;
    if (sendHyprlandCommand(binding, &legacyResponse) && legacyResponse.trimmed() == QStringLiteral("ok")) {
        m_luaBinding = false;
        m_bindingInstalled = true;
        return true;
    }
    removeHyprlandBinding();
    const QString cause = legacyResponse.contains(QStringLiteral("non-legacy")) ? response : legacyResponse;
    if (error) *error = QStringLiteral("Hyprland could not bind %1: %2").arg(m_shortcutSetting, cause);
    return false;
}

void LinuxBackend::removeHyprlandBinding()
{
    if (m_boundKey.isEmpty()) return;
    QString response;
    bool owned = false;
    bool foreign = false;
    QJsonArray current;
    if (sendHyprlandCommand(QStringLiteral("binds"), &response, true)) {
        current = QJsonDocument::fromJson(response.toUtf8()).array();
        for (const auto &value : current) {
            const auto bind = value.toObject();
            if (!matchesChord(bind,m_boundKey,m_boundModifiers)) continue;
            if (bind.value(QStringLiteral("description")).toString() == QStringLiteral("Arcade Wheel trigger")) owned = true;
            else foreign = true;
        }
    }
    if (m_luaBinding) {
        sendHyprlandCommand(QStringLiteral("eval if _G.arcadeWheelTrigger then _G.arcadeWheelTrigger:remove(); _G.arcadeWheelTrigger = nil end"));
    } else if (owned && !foreign) {
        sendHyprlandCommand(QStringLiteral("keyword unbind %1, %2").arg(modifierText(m_boundModifiers),m_boundKey));
    }
    // A compositor reload already restores configured bindings. Do not replay
    // stale Lua callback IDs over those restored bindings.
    if ((!foreign && (owned || m_luaBinding)) || !m_bindingInstalled) {
        for (const auto &value : std::as_const(m_displacedBindings)) {
            const auto bind = value.toObject();
            if (m_displacedWithLua) {
                const QString restore = luaRestoreBindCommand(bind);
                if (!restore.isEmpty()) sendHyprlandCommand(restore);
                continue;
            }
            QString flags;
            const std::pair<const char *, char> fields[] = {
                {"locked",'l'}, {"release",'r'}, {"repeat",'e'}, {"mouse",'m'},
                {"non_consuming",'n'}, {"transparent",'t'}, {"ignore_mods",'i'},
                {"longPress",'o'}, {"dont_inhibit",'p'}, {"submap_universal",'u'},
                {"auto_consuming",'a'}, {"allow_input_capture",'x'}, {"click",'c'}, {"drag",'g'}
            };
            for (const auto &[field,flag] : fields) if (bind.value(QLatin1String(field)).toBool()) flags += QLatin1Char(flag);
            const QString submap = bind.value(QStringLiteral("submap")).toString();
            sendHyprlandCommand(QStringLiteral("keyword submap %1").arg(submap.isEmpty() ? QStringLiteral("reset") : submap));
            QString args = modifierText(bind.value(QStringLiteral("modmask")).toInt()) + QStringLiteral(", ") + bind.value(QStringLiteral("key")).toString() + QStringLiteral(", ");
            if (bind.value(QStringLiteral("has_description")).toBool()) {
                flags += QLatin1Char('d');
                QString description = bind.value(QStringLiteral("description")).toString();
                description.replace(QLatin1Char(','),QLatin1Char(' '));
                args += description + QStringLiteral(", ");
            }
            args += bind.value(QStringLiteral("dispatcher")).toString() + QStringLiteral(", ") + bind.value(QStringLiteral("arg")).toString();
            sendHyprlandCommand(QStringLiteral("keyword bind%1 %2").arg(flags,args));
        }
        if (!m_displacedWithLua && !m_displacedBindings.isEmpty())
            sendHyprlandCommand(QStringLiteral("keyword submap reset"));
    }
    m_displacedBindings = {};
    m_displacedWithLua = false;
    m_bindingInstalled = false;
    m_luaBinding = false;
    m_boundKey.clear();
}
