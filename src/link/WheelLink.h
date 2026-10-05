#pragma once

#include "link/ArcadeLink.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <memory>
#include <optional>

class WheelController;

// Arcade Wheel's presence among the other Arcade apps: its manifest and its
// Link endpoint, kept in step with the "Connect with other Arcade apps"
// setting. Lives on the GUI thread; the server is event-driven (no extra
// threads, no timers while idle).
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

private:
    void invoke(const QJsonObject &request, const ArcadeLink::Responder &responder);
    QString m_version;
    QPointer<WheelController> m_controller;
    std::optional<ArcadeLink::Responder> m_pendingAdd;
    ArcadeLink::Locations m_locations;
    std::unique_ptr<ArcadeLink::Server> m_server;
    QString m_lastError;
};
