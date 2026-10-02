#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

class ActionProvider {
public:
    virtual ~ActionProvider() = default;
    virtual QString id() const = 0;
    virtual bool available() const = 0;
    virtual QString unavailableReason() const = 0;
    virtual QVariantList tools() const = 0;
    virtual bool execute(const QJsonObject &action, QString *error) = 0;
};
