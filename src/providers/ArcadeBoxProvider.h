#pragma once

#include "providers/ActionProvider.h"

class ArcadeBoxProvider final : public ActionProvider {
public:
    ArcadeBoxProvider();
    QString id() const override { return QStringLiteral("arcade_box"); }
    bool available() const override { return !m_executable.isEmpty(); }
    QString unavailableReason() const override;
    QVariantList tools() const override { return m_tools; }
    bool execute(const QJsonObject &action, QString *error) override;
    void refresh();

private:
    QString m_executable;
    QVariantList m_tools;
};
