#pragma once

#include "platform/PlatformBackend.h"
#include "providers/ArcadeBoxProvider.h"

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
    ArcadeBoxProvider &arcadeBox() { return *m_arcadeBox; }
    const ArcadeBoxProvider &arcadeBox() const { return *m_arcadeBox; }

private:
    PlatformBackend *m_backend;
    QVector<DiscoveredApplication> m_applications;
    std::vector<std::unique_ptr<ActionProvider>> m_providers;
    ArcadeBoxProvider *m_arcadeBox = nullptr;
    ActionProvider *provider(const QString &id) const;
};
