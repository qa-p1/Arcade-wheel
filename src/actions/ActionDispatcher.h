#pragma once

#include "platform/PlatformBackend.h"
#include "providers/ArcadeLinkProvider.h"

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

class ActionDispatcher {
public:
    explicit ActionDispatcher(PlatformBackend *backend);
    void setApplications(QVector<DiscoveredApplication> applications);
    void registerProvider(std::unique_ptr<ActionProvider> provider);
    QString unavailableReason(const QJsonObject &action) const;
    bool execute(const QJsonObject &action, bool focusExisting, QString *error);
    ArcadeLinkProvider &arcade() { return *m_arcade; }
    const ArcadeLinkProvider &arcade() const { return *m_arcade; }

private:
    PlatformBackend *m_backend;
    QVector<DiscoveredApplication> m_applications;
    std::vector<std::unique_ptr<ActionProvider>> m_providers;
    ArcadeLinkProvider *m_arcade = nullptr;
    ActionProvider *provider(const QString &id) const;
};
