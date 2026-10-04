#include "MacOSBackend.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>

#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QGuiApplication>
#include <QKeySequence>
#include <QProcess>
#include <QQuickWindow>
#include <QSet>
#include <algorithm>

namespace {
void assignError(QString *error, const QString &message)
{
    if (error) *error = message;
}

int nativeKey(Qt::Key key)
{
    static const QHash<int, int> keys = {
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3},
        {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6},
        {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9},
        {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
        {Qt::Key_F13, kVK_F13}, {Qt::Key_F14, kVK_F14}, {Qt::Key_F15, kVK_F15},
        {Qt::Key_F16, kVK_F16}, {Qt::Key_F17, kVK_F17}, {Qt::Key_F18, kVK_F18},
        {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20},
        {Qt::Key_Space, kVK_Space}, {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Return, kVK_Return}, {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
        {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Backspace, kVK_Delete},
        {Qt::Key_Delete, kVK_ForwardDelete}, {Qt::Key_Home, kVK_Home},
        {Qt::Key_End, kVK_End}, {Qt::Key_PageUp, kVK_PageUp},
        {Qt::Key_PageDown, kVK_PageDown}, {Qt::Key_Left, kVK_LeftArrow},
        {Qt::Key_Right, kVK_RightArrow}, {Qt::Key_Up, kVK_UpArrow},
        {Qt::Key_Down, kVK_DownArrow}
    };
    if (keys.contains(key)) return keys.value(key);
    // Translate printable keys using the active keyboard layout.
    if (key < Qt::Key_Space || key > Qt::Key_ydiaeresis) return -1;
    const auto source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    if (!source) return -1;
    const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    int result = -1;
    if (data) {
        const auto *layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        for (UInt16 code = 0; code < 128; ++code) {
            UInt32 deadKey = 0;
            UniChar chars[4];
            UniCharCount count = 0;
            if (UCKeyTranslate(layout, code, kUCKeyActionDown, 0, LMGetKbdType(),
                    kUCKeyTranslateNoDeadKeysBit, &deadKey, 4, &count, chars) == noErr
                && count == 1 && QChar(chars[0]).toUpper().unicode() == key) {
                result = code;
                break;
            }
        }
    }
    CFRelease(source);
    return result;
}
}

struct MacOSBackend::Native {
    EventHotKeyRef hotKey = nullptr;
    EventHandlerRef handler = nullptr;
    QJsonObject trigger;
    bool down = false;
    bool recording = false;

    static OSStatus handle(EventHandlerCallRef, EventRef event, void *context)
    {
        auto *backend = static_cast<MacOSBackend *>(context);
        auto &state = *backend->m_native;
        EventHotKeyID id{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                nullptr, sizeof(id), nullptr, &id) != noErr || id.signature != 0x4157434c || id.id != 1)
            return eventNotHandledErr;
        const bool pressed = GetEventKind(event) == kEventHotKeyPressed;
        if (pressed != state.down) {
            state.down = pressed;
            if (pressed) emit backend->triggerPressed();
            else emit backend->triggerReleased();
        }
        return noErr;
    }
};

MacOSBackend::MacOSBackend(QObject *parent)
    : PlatformBackend(parent), m_native(std::make_unique<Native>()) {}

MacOSBackend::~MacOSBackend()
{
    if (m_native->hotKey) UnregisterEventHotKey(m_native->hotKey);
    if (m_native->handler) RemoveEventHandler(m_native->handler);
}

QString MacOSBackend::name() const { return QStringLiteral("macOS"); }

QString MacOSBackend::shortcutValidationError(const QString &shortcut) const
{
    const QKeySequence sequence(shortcut, QKeySequence::PortableText);
    if (sequence.count() != 1 || nativeKey(sequence[0].key()) < 0)
        return QStringLiteral("Use one keyboard shortcut such as F8 or Command+Space. Mouse, media and modifier-only triggers are unavailable on macOS.");
    return {};
}

void MacOSBackend::configureTrigger(const QJsonObject &trigger)
{
    m_native->trigger = trigger;
    if (m_native->hotKey) UnregisterEventHotKey(m_native->hotKey);
    m_native->hotKey = nullptr;
    if (m_native->down) {
        m_native->down = false;
        emit triggerCancelled();
    }
    if (m_native->recording) return;
    if (qEnvironmentVariableIsSet("ARCADE_WHEEL_DISABLE_GLOBAL_SHORTCUT")) {
        emit triggerStatusChanged(QStringLiteral("Global shortcut disabled for this session"));
        return;
    }
    const QString shortcut = trigger.value(QStringLiteral("shortcut")).toString();
    const QString error = shortcutValidationError(shortcut);
    if (!error.isEmpty()) {
        emit triggerStatusChanged(error);
        return;
    }
    const QKeySequence sequence(shortcut, QKeySequence::PortableText);
    const auto modifiers = sequence[0].keyboardModifiers();
    UInt32 nativeModifiers = 0;
    // Qt swaps Control and Meta on macOS to preserve portable shortcut semantics.
    if (modifiers & Qt::ControlModifier) nativeModifiers |= cmdKey;
    if (modifiers & Qt::MetaModifier) nativeModifiers |= controlKey;
    if (modifiers & Qt::AltModifier) nativeModifiers |= optionKey;
    if (modifiers & Qt::ShiftModifier) nativeModifiers |= shiftKey;
    OSStatus status = noErr;
    if (!m_native->handler) {
        const EventTypeSpec events[] = {{kEventClassKeyboard, kEventHotKeyPressed},
                                        {kEventClassKeyboard, kEventHotKeyReleased}};
        status = InstallEventHandler(GetApplicationEventTarget(), Native::handle,
                                     2, events, this, &m_native->handler);
    }
    if (status == noErr)
        status = RegisterEventHotKey(nativeKey(sequence[0].key()), nativeModifiers,
            EventHotKeyID{0x4157434c, 1}, GetApplicationEventTarget(), 0, &m_native->hotKey);
    emit triggerStatusChanged(status == noErr
        ? QStringLiteral("Ready — hold %1").arg(sequence.toString(QKeySequence::NativeText))
        : QStringLiteral("Could not register shortcut (macOS error %1). Choose a shortcut not used by another app or macOS.").arg(status));
}

void MacOSBackend::setShortcutRecording(bool recording)
{
    m_native->recording = recording;
    configureTrigger(m_native->trigger);
}

bool MacOSBackend::prepareOverlay(QQuickWindow *window, QString *error)
{
    if (!window) { assignError(error, QStringLiteral("Overlay window is missing")); return false; }
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return true;
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nativeWindow = [view window];
    [nativeWindow setLevel:NSPopUpMenuWindowLevel];
    [nativeWindow setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                        NSWindowCollectionBehaviorFullScreenAuxiliary];
    return true;
}

QPointF MacOSBackend::cursorPosition() const { return QCursor::pos(); }
bool MacOSBackend::movePointer(const QPointF &position)
{
    QCursor::setPos(position.toPoint());
    return true;
}

QVector<DiscoveredApplication> MacOSBackend::applications() const
{
    QVector<DiscoveredApplication> result;
    QSet<QString> seen;
    @autoreleasepool {
        for (const QString &root : {QStringLiteral("/Applications"), QStringLiteral("/System/Applications"),
                                   QDir::homePath() + QStringLiteral("/Applications")}) {
            NSDirectoryEnumerator *items = [[NSFileManager defaultManager]
                enumeratorAtURL:[NSURL fileURLWithPath:root.toNSString()]
                includingPropertiesForKeys:nil
                options:NSDirectoryEnumerationSkipsHiddenFiles | NSDirectoryEnumerationSkipsPackageDescendants
                errorHandler:nil];
            for (NSURL *url in items) {
                if (![[url pathExtension] isEqualToString:@"app"]) continue;
                NSBundle *bundle = [NSBundle bundleWithURL:url];
                NSString *identifier = [bundle bundleIdentifier];
                if (!identifier || [identifier isEqualToString:@"com.arcadewheel.ArcadeWheel"]) continue;
                const QString id = QString::fromNSString(identifier);
                if (seen.contains(id)) continue;
                seen.insert(id);
                NSString *title = [bundle objectForInfoDictionaryKey:@"CFBundleDisplayName"];
                if (!title) title = [bundle objectForInfoDictionaryKey:@"CFBundleName"];
                if (!title) title = [[url lastPathComponent] stringByDeletingPathExtension];
                NSString *icon = [bundle objectForInfoDictionaryKey:@"CFBundleIconFile"];
                if (icon && [[icon pathExtension] length] == 0) icon = [icon stringByAppendingPathExtension:@"icns"];
                NSString *iconPath = icon ? [[bundle resourcePath] stringByAppendingPathComponent:icon] : nil;
                result.append({id, QString::fromNSString(title), QString::fromNSString(iconPath),
                               {}, QString::fromNSString([url path])});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}

bool MacOSBackend::launchApplication(const QString &id, bool focusExisting, QString *error)
{
    // open uses Launch Services and normally activates the existing instance.
    QStringList args;
    if (!focusExisting) args << QStringLiteral("-n");
    args << QStringLiteral("-b") << id;
    if (QProcess::execute(QStringLiteral("/usr/bin/open"), args) == 0) return true;
    assignError(error, QStringLiteral("Could not open application %1").arg(id));
    return false;
}

bool MacOSBackend::performSystemAction(const QString &id, QString *error)
{
    bool started = false;
    if (id == QStringLiteral("screenshot"))
        started = QProcess::startDetached(QStringLiteral("/usr/sbin/screencapture"), {QStringLiteral("-i"), QStringLiteral("-c")});
    else if (id == QStringLiteral("suspend"))
        started = QProcess::startDetached(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("sleepnow")});
    else {
        assignError(error, QStringLiteral("This system or media action is not available on macOS: %1").arg(id));
        return false;
    }
    if (!started) assignError(error, QStringLiteral("Could not start macOS action %1").arg(id));
    return started;
}

bool MacOSBackend::setStartOnLogin(bool enabled, QString *error)
{
    @autoreleasepool {
        const QString bundle = QFileInfo(QString::fromNSString([[NSBundle mainBundle] bundlePath])).canonicalFilePath();
        if (enabled && !bundle.startsWith(QStringLiteral("/Applications/"))
            && !bundle.startsWith(QDir::homePath() + QStringLiteral("/Applications/"))) {
            assignError(error, QStringLiteral("Move Arcade Wheel to Applications before enabling start on login."));
            return false;
        }
        if (@available(macOS 13.0, *)) {
            NSError *nativeError = nil;
            SMAppService *service = [SMAppService mainAppService];
            const BOOL ok = enabled ? [service registerAndReturnError:&nativeError]
                                    : [service unregisterAndReturnError:&nativeError];
            if (!ok) assignError(error, QString::fromNSString([nativeError localizedDescription]));
            else if (enabled && service.status == SMAppServiceStatusRequiresApproval) {
                assignError(error, QStringLiteral("Allow Arcade Wheel in System Settings → General → Login Items."));
                return false;
            }
            return ok;
        }
    }
    assignError(error, QStringLiteral("Start on login requires macOS 13 or later."));
    return false;
}

bool MacOSBackend::startOnLogin() const
{
    if (@available(macOS 13.0, *))
        return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
    return false;
}
