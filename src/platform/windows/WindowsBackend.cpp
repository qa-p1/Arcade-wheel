#include "WindowsBackend.h"

#ifdef Q_OS_WIN

#include <QByteArray>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QQuickWindow>
#include <QRect>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

#include <powrprof.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <memory>
#include <utility>
#include <vector>

WindowsBackend *WindowsBackend::s_hookOwner = nullptr;

namespace {

constexpr wchar_t kStartMenuIdPrefix[] = L"win-startmenu:";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"ArcadeWheel";

void assignError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString win32Error(DWORD code = GetLastError())
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0,
                                        reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    QString result = length ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
                            : QStringLiteral("Windows error %1").arg(code);
    if (buffer)
        LocalFree(buffer);
    return result;
}

QString nativePath(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString expandEnvironmentVariables(const QString &path)
{
    const auto *input = reinterpret_cast<LPCWSTR>(path.utf16());
    const DWORD required = ExpandEnvironmentStringsW(input, nullptr, 0);
    if (!required)
        return path;
    std::vector<wchar_t> expanded(required);
    const DWORD written = ExpandEnvironmentStringsW(input, expanded.data(), required);
    return written && written <= required ? QString::fromWCharArray(expanded.data()) : path;
}

struct ShortcutInfo {
    QString target;
    QString arguments;
    QString workingDirectory;
    QString description;
};

bool readShortcut(const QString &shortcutPath, ShortcutInfo *info)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool mustUninitialize = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        return false;

    IShellLinkW *link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, reinterpret_cast<void **>(&link));
    if (FAILED(result)) {
        if (mustUninitialize)
            CoUninitialize();
        return false;
    }

    IPersistFile *persistFile = nullptr;
    result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persistFile));
    if (SUCCEEDED(result))
        result = persistFile->Load(reinterpret_cast<LPCOLESTR>(shortcutPath.utf16()), STGM_READ);

    if (SUCCEEDED(result) && info) {
        std::array<wchar_t, 32768> target{};
        std::array<wchar_t, 32768> arguments{};
        std::array<wchar_t, 32768> workingDirectory{};
        std::array<wchar_t, 32768> description{};
        WIN32_FIND_DATAW findData{};
        link->GetPath(target.data(), static_cast<int>(target.size()), &findData, SLGP_RAWPATH);
        link->GetArguments(arguments.data(), static_cast<int>(arguments.size()));
        link->GetWorkingDirectory(workingDirectory.data(), static_cast<int>(workingDirectory.size()));
        link->GetDescription(description.data(), static_cast<int>(description.size()));
        info->target = QString::fromWCharArray(target.data());
        info->arguments = QString::fromWCharArray(arguments.data());
        info->workingDirectory = QString::fromWCharArray(workingDirectory.data());
        info->description = QString::fromWCharArray(description.data());
    }

    if (persistFile)
        persistFile->Release();
    link->Release();
    if (mustUninitialize)
        CoUninitialize();
    return SUCCEEDED(result);
}

QString startMenuFolder(REFKNOWNFOLDERID folderId)
{
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path)) || !path)
        return {};
    const QString result = QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return result;
}

UINT modifierVirtualKey(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QLatin1String("ctrl") || normalized == QLatin1String("control"))
        return VK_CONTROL;
    if (normalized == QLatin1String("shift"))
        return VK_SHIFT;
    if (normalized == QLatin1String("alt") || normalized == QLatin1String("option"))
        return VK_MENU;
    if (normalized == QLatin1String("win") || normalized == QLatin1String("windows") ||
        normalized == QLatin1String("meta"))
        return VK_LWIN;
    return 0;
}

UINT keyVirtualKey(const QString &name)
{
    const QString key = name.trimmed();
    const QString lower = key.toLower();
    static const QHash<QString, UINT> namedKeys = {
        {QStringLiteral("space"), VK_SPACE}, {QStringLiteral("tab"), VK_TAB},
        {QStringLiteral("escape"), VK_ESCAPE}, {QStringLiteral("esc"), VK_ESCAPE},
        {QStringLiteral("enter"), VK_RETURN}, {QStringLiteral("return"), VK_RETURN},
        {QStringLiteral("backspace"), VK_BACK}, {QStringLiteral("delete"), VK_DELETE},
        {QStringLiteral("del"), VK_DELETE}, {QStringLiteral("insert"), VK_INSERT},
        {QStringLiteral("home"), VK_HOME}, {QStringLiteral("end"), VK_END},
        {QStringLiteral("pageup"), VK_PRIOR}, {QStringLiteral("pgup"), VK_PRIOR},
        {QStringLiteral("pagedown"), VK_NEXT}, {QStringLiteral("pgdn"), VK_NEXT},
        {QStringLiteral("up"), VK_UP}, {QStringLiteral("down"), VK_DOWN},
        {QStringLiteral("left"), VK_LEFT}, {QStringLiteral("right"), VK_RIGHT},
        {QStringLiteral("capslock"), VK_CAPITAL}, {QStringLiteral("caps lock"), VK_CAPITAL},
        {QStringLiteral("printscreen"), VK_SNAPSHOT}, {QStringLiteral("prtsc"), VK_SNAPSHOT},
        {QStringLiteral("pause"), VK_PAUSE}, {QStringLiteral("plus"), VK_OEM_PLUS},
        {QStringLiteral("minus"), VK_OEM_MINUS}, {QStringLiteral("comma"), VK_OEM_COMMA},
        {QStringLiteral("period"), VK_OEM_PERIOD}, {QStringLiteral("slash"), VK_OEM_2},
        {QStringLiteral("backslash"), VK_OEM_5}, {QStringLiteral("semicolon"), VK_OEM_1},
        {QStringLiteral("quote"), VK_OEM_7}, {QStringLiteral("leftbracket"), VK_OEM_4},
        {QStringLiteral("rightbracket"), VK_OEM_6}, {QStringLiteral("backtick"), VK_OEM_3},
        {QStringLiteral("numpadenter"), VK_RETURN},
    };
    const auto it = namedKeys.constFind(lower);
    if (it != namedKeys.cend())
        return it.value();
    if (const UINT modifier = modifierVirtualKey(key))
        return modifier;

    if (lower.size() >= 2 && lower.at(0) == QLatin1Char('f')) {
        bool ok = false;
        const int functionNumber = lower.mid(1).toInt(&ok);
        if (ok && functionNumber >= 1 && functionNumber <= 24)
            return VK_F1 + static_cast<UINT>(functionNumber - 1);
    }
    if (key.size() == 1) {
        const SHORT translated = VkKeyScanW(key.at(0).toUpper().unicode());
        if (translated != -1)
            return static_cast<UINT>(translated & 0xff);
    }
    return 0;
}

struct ParsedTrigger {
    UINT key = 0;
    QVector<UINT> modifiers;
    QString error;
};

void addModifier(QVector<UINT> *modifiers, UINT key)
{
    if (key && !modifiers->contains(key))
        modifiers->append(key);
}

ParsedTrigger parseTrigger(const QJsonObject &trigger)
{
    ParsedTrigger parsed;
    const QString shortcut = trigger.value(QStringLiteral("shortcut")).toString().trimmed();
    const QJsonValue virtualKeyValue = trigger.value(QStringLiteral("virtualKey"));
    const QJsonValue keyCodeValue = trigger.value(QStringLiteral("keyCode"));
    if (shortcut.isEmpty() && (virtualKeyValue.isDouble() || keyCodeValue.isDouble())) {
        const int code = (virtualKeyValue.isDouble() ? virtualKeyValue : keyCodeValue).toInt();
        if (code > 0 && code <= 0xff)
            parsed.key = static_cast<UINT>(code);
    }

    const QString keyName = trigger.value(QStringLiteral("key")).toString().trimmed();
    if (!shortcut.isEmpty()) {
        const QStringList tokens = shortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            const UINT modifier = modifierVirtualKey(token);
            if (modifier) {
                addModifier(&parsed.modifiers, modifier);
            } else if (!parsed.key) {
                if (token.trimmed().startsWith(QLatin1String("mouse"), Qt::CaseInsensitive)) {
                    parsed.error = QStringLiteral("Windows currently supports keyboard triggers; mouse buttons are unavailable.");
                    return parsed;
                }
                parsed.key = keyVirtualKey(token);
                if (!parsed.key) {
                    parsed.error = QStringLiteral("Unsupported trigger key: %1").arg(token.trimmed());
                    return parsed;
                }
            } else {
                parsed.error = QStringLiteral("Use one trigger key with optional modifiers.");
                return parsed;
            }
        }
    } else if (!parsed.key && !keyName.isEmpty()) {
        parsed.key = keyVirtualKey(keyName);
        if (!parsed.key) {
            parsed.error = QStringLiteral("Unsupported trigger key: %1").arg(keyName);
            return parsed;
        }
    }

    const QJsonValue modifiersValue = trigger.value(QStringLiteral("modifiers"));
    if (modifiersValue.isArray()) {
        for (const QJsonValue &value : modifiersValue.toArray()) {
            const UINT modifier = modifierVirtualKey(value.toString());
            if (!modifier) {
                parsed.error = QStringLiteral("Unsupported trigger modifier: %1").arg(value.toString());
                return parsed;
            }
            addModifier(&parsed.modifiers, modifier);
        }
    } else if (modifiersValue.isString() && !modifiersValue.toString().isEmpty()) {
        const UINT modifier = modifierVirtualKey(modifiersValue.toString());
        if (!modifier) {
            parsed.error = QStringLiteral("Unsupported trigger modifier: %1").arg(modifiersValue.toString());
            return parsed;
        }
        addModifier(&parsed.modifiers, modifier);
    }

    if (!parsed.key && !parsed.modifiers.isEmpty()) {
        // A single modifier can itself be a trigger key (for example, holding Ctrl).
        parsed.key = parsed.modifiers.takeLast();
    }
    if (!parsed.key) {
        parsed.error = QStringLiteral("Set a keyboard key or a shortcut such as Ctrl+Alt+Space.");
        return parsed;
    }
    parsed.modifiers.removeAll(parsed.key);
    return parsed;
}

bool keyIsDown(UINT virtualKey)
{
    return (GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
}

bool modifierIsDown(UINT modifier)
{
    switch (modifier) {
    case VK_CONTROL: return keyIsDown(VK_LCONTROL) || keyIsDown(VK_RCONTROL);
    case VK_SHIFT: return keyIsDown(VK_LSHIFT) || keyIsDown(VK_RSHIFT);
    case VK_MENU: return keyIsDown(VK_LMENU) || keyIsDown(VK_RMENU);
    case VK_LWIN: return keyIsDown(VK_LWIN) || keyIsDown(VK_RWIN);
    default: return keyIsDown(modifier);
    }
}

QString normalizedDisplayName(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('\\'));
    const QString prefix = QStringLiteral("\\\\.\\");
    if (name.startsWith(prefix, Qt::CaseInsensitive))
        name.remove(0, prefix.size());
    return name;
}

bool matchesConfiguredKey(UINT configuredKey, UINT eventKey)
{
    if (configuredKey == eventKey)
        return true;
    if (configuredKey == VK_CONTROL)
        return eventKey == VK_LCONTROL || eventKey == VK_RCONTROL;
    if (configuredKey == VK_SHIFT)
        return eventKey == VK_LSHIFT || eventKey == VK_RSHIFT;
    if (configuredKey == VK_MENU)
        return eventKey == VK_LMENU || eventKey == VK_RMENU;
    if (configuredKey == VK_LWIN)
        return eventKey == VK_LWIN || eventKey == VK_RWIN;
    return false;
}

struct ExistingWindowSearch {
    QString executable;
    HWND window = nullptr;
};

BOOL CALLBACK findExistingWindow(HWND window, LPARAM parameter)
{
    if (!IsWindowVisible(window))
        return TRUE;

    auto *search = reinterpret_cast<ExistingWindowSearch *>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId)
        return TRUE;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return TRUE;

    std::array<wchar_t, 32768> processPath{};
    DWORD pathLength = static_cast<DWORD>(processPath.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, processPath.data(), &pathLength);
    CloseHandle(process);
    if (!queried)
        return TRUE;

    if (QString::fromWCharArray(processPath.data(), static_cast<int>(pathLength))
            .compare(search->executable, Qt::CaseInsensitive) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

bool raiseWindow(HWND window)
{
    if (!window)
        return false;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    else
        ShowWindow(window, SW_SHOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    return true;
}

class ShutdownPrivilege {
public:
    ShutdownPrivilege()
    {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &m_token))
            return;
        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &luid))
            return;

        TOKEN_PRIVILEGES enabled{};
        enabled.PrivilegeCount = 1;
        enabled.Privileges[0].Luid = luid;
        enabled.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        DWORD previousSize = sizeof(m_previous);
        if (!AdjustTokenPrivileges(m_token, FALSE, &enabled, sizeof(m_previous), &m_previous,
                                   &previousSize) || GetLastError() == ERROR_NOT_ALL_ASSIGNED)
            return;
        m_enabled = true;
    }

    ~ShutdownPrivilege()
    {
        if (m_enabled)
            AdjustTokenPrivileges(m_token, FALSE, &m_previous, 0, nullptr, nullptr);
        if (m_token)
            CloseHandle(m_token);
    }

    bool enabled() const { return m_enabled; }

private:
    HANDLE m_token = nullptr;
    TOKEN_PRIVILEGES m_previous{};
    bool m_enabled = false;
};

bool sendKeyboardChord(const QVector<UINT> &keys)
{
    QVector<INPUT> inputs;
    inputs.reserve(keys.size() * 2);
    for (UINT key : keys) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(key);
        inputs.append(input);
    }
    for (auto it = keys.crbegin(); it != keys.crend(); ++it) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(*it);
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.append(input);
    }
    return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) ==
           static_cast<UINT>(inputs.size());
}

QString shellIconCachePath(const QString &path)
{
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty())
        return {};

    const QFileInfo shortcutInfo(path);
    QByteArray fingerprint = shortcutInfo.absoluteFilePath().toCaseFolded().toUtf8();
    fingerprint += QByteArray::number(shortcutInfo.lastModified().toMSecsSinceEpoch());
    fingerprint += QByteArray::number(shortcutInfo.size());
    const QString directoryPath = QDir(cacheRoot).filePath(QStringLiteral("windows-icons"));
    QDir directory;
    if (!directory.mkpath(directoryPath))
        return {};
    const QString filename = QString::fromLatin1(
        QCryptographicHash::hash(fingerprint, QCryptographicHash::Sha256).toHex()) +
                             QStringLiteral(".png");
    return QDir(directoryPath).filePath(filename);
}

QString shellIconCacheFile(const QString &path)
{
    const QString cachePath = shellIconCachePath(path);
    if (cachePath.isEmpty())
        return {};
    if (QFileInfo::exists(cachePath))
        return cachePath;

    SHFILEINFOW fileInfo{};
    if (!SHGetFileInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, &fileInfo,
                        sizeof(fileInfo), SHGFI_ICON | SHGFI_LARGEICON) || !fileInfo.hIcon)
        return {};

    constexpr int iconSize = 64;
    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = iconSize;
    bitmapInfo.bmiHeader.biHeight = -iconSize;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = memoryDc ? CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS,
                                                &pixels, nullptr, 0) : nullptr;
    HGDIOBJ oldBitmap = bitmap ? SelectObject(memoryDc, bitmap) : nullptr;
    if (bitmap && pixels) {
        std::memset(pixels, 0, iconSize * iconSize * 4);
        DrawIconEx(memoryDc, 0, 0, fileInfo.hIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    QImage iconImage;
    if (bitmap && pixels) {
        QImage image(static_cast<const uchar *>(pixels), iconSize, iconSize, iconSize * 4,
                     QImage::Format_ARGB32);
        iconImage = image.copy();
    }

    if (oldBitmap)
        SelectObject(memoryDc, oldBitmap);
    if (bitmap)
        DeleteObject(bitmap);
    if (memoryDc)
        DeleteDC(memoryDc);
    if (screenDc)
        ReleaseDC(nullptr, screenDc);
    DestroyIcon(fileInfo.hIcon);
    return iconImage.save(cachePath, "PNG") ? cachePath : QString();
}

QString appRunCommand()
{
    const QString executable = nativePath(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\" --background").arg(executable);
}

} // namespace

WindowsBackend::WindowsBackend(QObject *parent)
    : PlatformBackend(parent)
{
    // Qt normally establishes process DPI awareness from its Windows manifest. This also
    // covers builds without that manifest, and safely does nothing if awareness is set already.
    using SetDpiContext = BOOL(WINAPI *)(HANDLE);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto setDpiContext = user32 ? reinterpret_cast<SetDpiContext>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;
    if (setDpiContext)
        setDpiContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
}

WindowsBackend::~WindowsBackend()
{
    uninstallKeyboardHook();
}

QString WindowsBackend::name() const
{
    return QStringLiteral("Windows");
}

void WindowsBackend::configureTrigger(const QJsonObject &trigger)
{
    if (m_triggerDown)
        emit triggerReleased();
    uninstallKeyboardHook();
    m_triggerDown = false;

    const ParsedTrigger parsed = parseTrigger(trigger);
    if (!parsed.error.isEmpty()) {
        emit triggerStatusChanged(parsed.error);
        return;
    }

    m_triggerVirtualKey = parsed.key;
    m_requiredModifiers = parsed.modifiers;
    s_hookOwner = this;
    // This is a passive low-level hook: every event is passed to Windows unchanged.
    // Consequently a trigger such as Caps Lock still toggles Caps Lock. The configured
    // hold threshold can choose when the wheel opens, but Windows tap behavior cannot be
    // deferred and replayed without suppressing the original key event.
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc,
                                       GetModuleHandleW(nullptr), 0);
    if (!m_keyboardHook) {
        s_hookOwner = nullptr;
        emit triggerStatusChanged(QStringLiteral("Could not register the global keyboard trigger: %1")
                                      .arg(win32Error()));
        return;
    }

    emit triggerStatusChanged(QString());
}

bool WindowsBackend::prepareOverlay(QQuickWindow *window, QString *error)
{
    if (!window) {
        assignError(error, QStringLiteral("The overlay window is unavailable."));
        return false;
    }

    window->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool |
                     Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    window->setColor(Qt::transparent);
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    if (!handle) {
        assignError(error, QStringLiteral("Windows did not create the overlay window."));
        return false;
    }

    const LONG_PTR oldStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR newStyle = oldStyle | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    const LONG_PTR previousStyle = SetWindowLongPtrW(handle, GWL_EXSTYLE, newStyle);
    if (!previousStyle && GetLastError() != ERROR_SUCCESS) {
        assignError(error, QStringLiteral("Could not set overlay window styles: %1").arg(win32Error()));
        return false;
    }
    if (!SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
        assignError(error, QStringLiteral("Could not keep the overlay above desktop windows: %1")
                               .arg(win32Error()));
        return false;
    }
    if (error)
        error->clear();
    return true;
}

QPointF WindowsBackend::cursorPosition() const
{
    POINT point{};
    if (!GetCursorPos(&point))
        return {};

    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (monitor && GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&monitorInfo))) {
        const QString deviceName = QString::fromWCharArray(monitorInfo.szDevice);
        for (QScreen *screen : QGuiApplication::screens()) {
            if (normalizedDisplayName(screen->name()).compare(normalizedDisplayName(deviceName),
                                                               Qt::CaseInsensitive) != 0)
                continue;
            const qreal ratio = screen->devicePixelRatio();
            const QRect geometry = screen->geometry();
            return QPointF(geometry.x() + (point.x - monitorInfo.rcMonitor.left) / ratio,
                           geometry.y() + (point.y - monitorInfo.rcMonitor.top) / ratio);
        }
    }

    // This fallback is useful on systems where Qt does not expose the native display name.
    return QPointF(point.x, point.y);
}

bool WindowsBackend::movePointer(const QPointF &position)
{
    // The wheel opens at the screen center. Move the pointer there as well so
    // selection starts in the cancel zone instead of at the old cursor position.
    QScreen *screen = QGuiApplication::screenAt(position.toPoint());
    if (!screen)
        return false;
    QCursor::setPos(screen, position.toPoint());
    return true;
}

QVector<DiscoveredApplication> WindowsBackend::applications() const
{
    QVector<DiscoveredApplication> result;
    QStringList folders;
    const QString userStartMenu = startMenuFolder(FOLDERID_Programs);
    const QString commonStartMenu = startMenuFolder(FOLDERID_CommonPrograms);
    if (!userStartMenu.isEmpty())
        folders.append(userStartMenu);
    if (!commonStartMenu.isEmpty() && commonStartMenu != userStartMenu)
        folders.append(commonStartMenu);

    QSet<QString> visited;
    for (const QString &folder : folders) {
        QDirIterator iterator(folder, QStringList{QStringLiteral("*.lnk")}, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = QDir::cleanPath(iterator.next());
            const QString normalizedPath = path.toCaseFolded();
            if (visited.contains(normalizedPath))
                continue;
            visited.insert(normalizedPath);

            ShortcutInfo shortcut;
            readShortcut(path, &shortcut);
            const QFileInfo fileInfo(path);
            DiscoveredApplication app;
            app.id = QStringLiteral("win-startmenu:") + path;
            app.name = fileInfo.completeBaseName();
            app.description = shortcut.description.isEmpty()
                                  ? QFileInfo(shortcut.target).fileName()
                                  : shortcut.description;
            app.icon = shellIconCacheFile(path);
            app.launchTarget = path;
            result.append(std::move(app));
        }
    }

    std::sort(result.begin(), result.end(), [](const DiscoveredApplication &left,
                                               const DiscoveredApplication &right) {
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool WindowsBackend::launchApplication(const QString &id, bool focusExisting, QString *error)
{
    if (!id.startsWith(QString::fromWCharArray(kStartMenuIdPrefix), Qt::CaseInsensitive)) {
        assignError(error, QStringLiteral("This application does not have a Windows Start Menu entry."));
        return false;
    }

    const QString shortcutPath = id.mid(static_cast<int>(std::wcslen(kStartMenuIdPrefix)));
    if (!QFileInfo::exists(shortcutPath)) {
        assignError(error, QStringLiteral("The Start Menu shortcut is no longer available."));
        return false;
    }

    ShortcutInfo shortcut;
    if (focusExisting && readShortcut(shortcutPath, &shortcut) && !shortcut.target.isEmpty()) {
        ExistingWindowSearch search{nativePath(expandEnvironmentVariables(shortcut.target)), nullptr};
        EnumWindows(findExistingWindow, reinterpret_cast<LPARAM>(&search));
        if (search.window) {
            raiseWindow(search.window);
            if (error)
                error->clear();
            return true;
        }
    }

    const QString nativeShortcut = nativePath(shortcutPath);
    const HINSTANCE result = ShellExecuteW(nullptr, L"open",
                                           reinterpret_cast<LPCWSTR>(nativeShortcut.utf16()),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        assignError(error, QStringLiteral("Could not launch %1 (Windows shell error %2).")
                               .arg(QFileInfo(shortcutPath).completeBaseName())
                               .arg(reinterpret_cast<INT_PTR>(result)));
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool WindowsBackend::performSystemAction(const QString &id, QString *error)
{
    bool succeeded = false;
    if (id == QLatin1String("lock")) {
        succeeded = LockWorkStation();
    } else if (id == QLatin1String("suspend")) {
        succeeded = SetSuspendState(FALSE, FALSE, FALSE);
    } else if (id == QLatin1String("logout")) {
        succeeded = ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_APPLICATION |
                                               SHTDN_REASON_FLAG_USER_DEFINED);
    } else if (id == QLatin1String("poweroff")) {
        ShutdownPrivilege privilege;
        if (!privilege.enabled()) {
            assignError(error, QStringLiteral("Windows did not grant this process shutdown permission."));
            return false;
        }
        succeeded = ExitWindowsEx(EWX_POWEROFF | EWX_SHUTDOWN,
                                  SHTDN_REASON_MAJOR_APPLICATION |
                                      SHTDN_REASON_FLAG_USER_DEFINED);
    } else if (id == QLatin1String("screenshot")) {
        succeeded = sendKeyboardChord({VK_LWIN, VK_SHIFT, 'S'});
    } else if (id == QLatin1String("media:play-pause")) {
        succeeded = sendKeyboardChord({VK_MEDIA_PLAY_PAUSE});
    } else if (id == QLatin1String("media:next")) {
        succeeded = sendKeyboardChord({VK_MEDIA_NEXT_TRACK});
    } else if (id == QLatin1String("media:previous")) {
        succeeded = sendKeyboardChord({VK_MEDIA_PREV_TRACK});
    } else if (id.startsWith(QLatin1String("desktop:"))) {
        assignError(error, QStringLiteral("Windows does not provide a stable public API for this desktop action."));
        return false;
    } else {
        assignError(error, QStringLiteral("System action '%1' is not supported by the Windows backend.").arg(id));
        return false;
    }

    if (!succeeded) {
        assignError(error, QStringLiteral("Windows could not perform '%1': %2").arg(id, win32Error()));
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool WindowsBackend::setStartOnLogin(bool enabled, QString *error)
{
    if (enabled) {
        const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
        const QString temporary = QFileInfo(QDir::tempPath()).canonicalFilePath();
        if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) {
            assignError(error, QStringLiteral("The Arcade Wheel executable could not be located."));
            return false;
        }
        if (!temporary.isEmpty() && (executable.compare(temporary, Qt::CaseInsensitive) == 0 ||
            executable.startsWith(temporary + QDir::separator(), Qt::CaseInsensitive))) {
            assignError(error, QStringLiteral("Move Arcade Wheel out of the temporary directory before enabling start on login."));
            return false;
        }
    }
    HKEY key = nullptr;
    const LSTATUS openResult = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                                               REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                                               &key, nullptr);
    if (openResult != ERROR_SUCCESS) {
        assignError(error, QStringLiteral("Could not open the Windows login settings: %1")
                               .arg(win32Error(static_cast<DWORD>(openResult))));
        return false;
    }

    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const QString command = appRunCommand();
        const DWORD byteCount = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(command.utf16()), byteCount);
    } else {
        result = RegDeleteValueW(key, kRunValue);
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        assignError(error, QStringLiteral("Could not update Windows start-on-login: %1")
                               .arg(win32Error(static_cast<DWORD>(result))));
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool WindowsBackend::startOnLogin() const
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    const LSTATUS result = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void WindowsBackend::uninstallKeyboardHook()
{
    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }
    if (s_hookOwner == this)
        s_hookOwner = nullptr;
}

bool WindowsBackend::hasRequiredModifiers() const
{
    for (UINT modifier : m_requiredModifiers) {
        if (!modifierIsDown(modifier))
            return false;
    }
    return true;
}

LRESULT CALLBACK WindowsBackend::keyboardHookProc(int code, WPARAM message, LPARAM data)
{
    if (code >= 0 && s_hookOwner && data) {
        const auto *keyEvent = reinterpret_cast<const KBDLLHOOKSTRUCT *>(data);
        if (!(keyEvent->flags & LLKHF_INJECTED)) {
            const bool keyDown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            const bool keyUp = message == WM_KEYUP || message == WM_SYSKEYUP;
            WindowsBackend *owner = s_hookOwner;
            if (keyDown && matchesConfiguredKey(owner->m_triggerVirtualKey, keyEvent->vkCode) &&
                !owner->m_triggerDown && owner->hasRequiredModifiers()) {
                owner->m_triggerDown = true;
                emit owner->triggerPressed();
            } else if (keyUp && matchesConfiguredKey(owner->m_triggerVirtualKey, keyEvent->vkCode) &&
                       owner->m_triggerDown) {
                owner->m_triggerDown = false;
                emit owner->triggerReleased();
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

#endif // Q_OS_WIN
