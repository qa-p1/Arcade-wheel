#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

class QQuickWindow;

struct DiscoveredApplication {
    QString id;
    QString name;
    QString icon;
    QString description;
    QString launchTarget;
};

// Only desktop integration lives behind this boundary. Configuration, selection,
// action routing and QML are shared by every platform.
class PlatformBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~PlatformBackend() override = default;

    virtual QString name() const = 0;
    virtual void configureTrigger(const QJsonObject &trigger) = 0;
    virtual void setTriggerHoldThreshold(int) {}
    virtual bool prepareOverlay(QQuickWindow *window, QString *error) = 0;
    virtual QPointF cursorPosition() const = 0;
    virtual bool movePointer(const QPointF &) { return false; }
    virtual QString shortcutValidationError(const QString &) const { return {}; }
    virtual QString shortcutConflict(const QString &) const { return {}; }
    virtual void setShortcutRecording(bool) {}
    virtual QVector<DiscoveredApplication> applications() const = 0;
    virtual bool launchApplication(const QString &id, bool focusExisting, QString *error) = 0;
    virtual bool performSystemAction(const QString &id, QString *error) = 0;
    virtual bool setStartOnLogin(bool enabled, QString *error) = 0;
    virtual bool startOnLogin() const = 0;

signals:
    void triggerPressed();
    void triggerReleased();
    void triggerStatusChanged(const QString &message);
    void actionFailed(const QString &message);
    void triggerCancelled();
};
