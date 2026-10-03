#pragma once

#include "../PlatformBackend.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#endif

class WindowsBackend final : public PlatformBackend {
    Q_OBJECT
public:
    explicit WindowsBackend(QObject *parent = nullptr);
    ~WindowsBackend() override;

    QString name() const override;
    void configureTrigger(const QJsonObject &trigger) override;
    bool prepareOverlay(QQuickWindow *window, QString *error) override;
    QPointF cursorPosition() const override;
    bool movePointer(const QPointF &position) override;
    QString shortcutValidationError(const QString &shortcut) const override;
    QVector<DiscoveredApplication> applications() const override;
    bool launchApplication(const QString &id, bool focusExisting, QString *error) override;
    bool performSystemAction(const QString &id, QString *error) override;
    bool setStartOnLogin(bool enabled, QString *error) override;
    bool startOnLogin() const override;

private:
#ifdef Q_OS_WIN
    static LRESULT CALLBACK keyboardHookProc(int code, WPARAM message, LPARAM data);
    bool hasRequiredModifiers() const;
    void uninstallKeyboardHook();

    HHOOK m_keyboardHook = nullptr;
    UINT m_triggerVirtualKey = 0;
    QVector<UINT> m_requiredModifiers;
    bool m_triggerDown = false;
    static WindowsBackend *s_hookOwner;
#endif
};
