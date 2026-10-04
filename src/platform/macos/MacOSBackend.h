#pragma once

#include "../PlatformBackend.h"
#include <memory>

class MacOSBackend final : public PlatformBackend {
    Q_OBJECT
public:
    explicit MacOSBackend(QObject *parent = nullptr);
    ~MacOSBackend() override;
    QString name() const override;
    void configureTrigger(const QJsonObject &trigger) override;
    void setShortcutRecording(bool recording) override;
    QString shortcutValidationError(const QString &shortcut) const override;
    bool prepareOverlay(QQuickWindow *window, QString *error) override;
    QPointF cursorPosition() const override;
    bool movePointer(const QPointF &position) override;
    QVector<DiscoveredApplication> applications() const override;
    bool launchApplication(const QString &id, bool focusExisting, QString *error) override;
    bool performSystemAction(const QString &id, QString *error) override;
    bool setStartOnLogin(bool enabled, QString *error) override;
    bool startOnLogin() const override;

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};
