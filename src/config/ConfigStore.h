#pragma once

#include "platform/PlatformBackend.h"

#include <QJsonObject>
#include <QString>

class ConfigStore {
public:
    static constexpr int SchemaVersion = 3;

    explicit ConfigStore(QString path = {});
    QString path() const { return m_path; }
    QJsonObject load(const QVector<DiscoveredApplication> &applications, QString *error = nullptr) const;
    bool save(const QJsonObject &config, QString *error = nullptr) const;
    bool exportTo(const QJsonObject &config, const QString &path, QString *error = nullptr) const;
    bool importFrom(const QString &path, QJsonObject *config, QString *error = nullptr) const;

    static QJsonObject defaults(const QVector<DiscoveredApplication> &applications = {});
    static QJsonObject normalize(QJsonObject config, QString *error = nullptr);
    static QJsonObject emptyAction();
    static QString freshId();

private:
    QString m_path;
    mutable bool m_writeBlocked = false;
    static bool writeJson(const QJsonObject &config, const QString &path, QString *error);
};
