#pragma once

#include "link/ArcadeLink.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <optional>

class WheelController;

// Arcade Wheel's presence among the other Arcade apps: its manifest and its
// Link endpoint, kept in step with the "Connect with other Arcade apps"
// setting. Its manifest and event-driven server share the registry worker;
// controller state and cached diagnostics remain on the GUI thread.
class WheelLink final : public QObject {
    Q_OBJECT
public:
    WheelLink(QString version, QObject *parent = nullptr);
    ~WheelLink() override;

    // The manifest for this configuration (also printed by --arcade-manifest).
    static QJsonObject manifest(const QJsonObject &config, const QString &version);
    static QJsonArray actions();

    // The controller that shows the wheel and opens Settings for wheel.add_action.
    void setController(WheelController *controller);
    // The Wheel action a Link value would become (empty, with `error`, if none).
    static QVariantMap draftFor(const QJsonObject &content, QString *error);

    // Writes the manifest and starts or stops listening to match `config`.
    void apply(const QJsonObject &config);
    void stop();
    bool listening() const;
    QString lastError() const { return m_lastError; }
    const ArcadeLink::Locations &locations() const { return m_locations; }

signals:
    void diagnosticsChanged(const QString &endpoint, const QString &error);

private:
    void invoke(const QJsonObject &request, const ArcadeLink::Responder &responder, const QVariantMap &draft, const QString &draftError);
    QString m_version;
    QPointer<WheelController> m_controller;
    std::optional<ArcadeLink::Responder> m_pendingAdd;
    ArcadeLink::Locations m_locations;
    struct PresenceState;
    std::shared_ptr<PresenceState> m_presence;
    QPointer<QObject> m_ioWorker;
    std::atomic_bool m_listening{false};
    QTimer m_addCancelTimer;
    void respond(const ArcadeLink::Responder &r, std::function<void(const ArcadeLink::Responder &)> reply);
    QString m_lastError;
};
