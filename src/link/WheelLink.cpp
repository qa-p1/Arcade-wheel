#include "link/WheelLink.h"

#include <QJsonArray>

using namespace ArcadeLink;

WheelLink::WheelLink(QString version, QObject *parent)
    : QObject(parent), m_version(std::move(version)), m_locations(Locations::discover())
{
}

WheelLink::~WheelLink() { stop(); }

QJsonArray WheelLink::actions() { return {}; }

QJsonObject WheelLink::manifest(const QJsonObject &config, const QString &version)
{
    const QJsonObject link = config.value(QStringLiteral("link")).toObject();
    const bool enabled = link.value(QStringLiteral("enabled")).toBool(true);
    QJsonArray shortcuts;
    const QString trigger = config.value(QStringLiteral("trigger")).toObject().value(QStringLiteral("shortcut")).toString();
    if (!trigger.isEmpty())
        shortcuts.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("trigger")}, {QStringLiteral("accelerator"), trigger}});
    QJsonArray protocol;
    for (int p : supportedProtocols()) protocol.append(p);
    return {{QStringLiteral("schema"), 1},
            {QStringLiteral("id"), Ids::Wheel},
            {QStringLiteral("name"), appName(Ids::Wheel)},
            {QStringLiteral("version"), version},
            {QStringLiteral("link"), QJsonObject{{QStringLiteral("protocol"), protocol}}},
            {QStringLiteral("executable"), executablePath()},
            {QStringLiteral("launch"), QJsonObject{{QStringLiteral("background"), QJsonArray{QStringLiteral("--background")}}}},
            {QStringLiteral("shortcuts"), shortcuts},
            {QStringLiteral("settings"), QJsonObject{{QStringLiteral("linkEnabled"), enabled}}},
            {QStringLiteral("actions"), enabled ? actions() : QJsonArray()}};
}

void WheelLink::apply(const QJsonObject &config)
{
    const QJsonObject m = manifest(config, m_version);
    QString error;
    const bool changed = writeManifest(m_locations, m, &error);
    if (!error.isEmpty()) m_lastError = QStringLiteral("could not write the manifest: %1").arg(error);
    const bool enabled = m.value(QStringLiteral("settings")).toObject().value(QStringLiteral("linkEnabled")).toBool();
    if (enabled && !m_server) {
        m_server = std::make_unique<Server>(Ids::Wheel, m_version, m_locations);
        m_server->describe = [] { return WheelLink::actions(); };
        if (!m_server->start(&error)) {
            m_lastError = QStringLiteral("could not listen: %1").arg(error);
            m_server.reset();
        } else {
            m_lastError.clear();
        }
    } else if (!enabled) {
        m_server.reset();
    } else if (changed) {
        m_server->notifyChanged();
    }
}

void WheelLink::stop() { m_server.reset(); }

bool WheelLink::listening() const { return m_server && m_server->listening(); }
