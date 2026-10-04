#pragma once

#include "link/ArcadeLink.h"

#include <QJsonObject>
#include <QObject>
#include <memory>

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

    // Writes the manifest and starts or stops listening to match `config`.
    void apply(const QJsonObject &config);
    void stop();
    bool listening() const;
    QString lastError() const { return m_lastError; }
    const ArcadeLink::Locations &locations() const { return m_locations; }

private:
    QString m_version;
    ArcadeLink::Locations m_locations;
    std::unique_ptr<ArcadeLink::Server> m_server;
    QString m_lastError;
};
