#pragma once

#include "platform/PlatformBackend.h"

#include <QJsonObject>
#include <QString>

class ConfigStore {
public:
    static constexpr int SchemaVersion = 4;
    // Limits applied before anything is parsed or stored (WH-3). An import
    // above the byte limit is refused unread; longer text is refused on
    // import and cut to the limit when an existing file is loaded.
    static constexpr qint64 MaxImportBytes = 2 * 1024 * 1024;
    static constexpr qint64 MaxSettingsBytes = 64 * 1024 * 1024;
    static constexpr int MaxNameLength = 120;
    static constexpr int MaxTextLength = 4096;
    static constexpr int MaxDecks = 64;
    static constexpr int MaxDeckActions = 8;
    static constexpr int MaxGestureActions = 16;
    // Imports and corrupt-file copies kept next to settings.json.
    static constexpr int KeptBackups = 5;

    explicit ConfigStore(QString path = {});
    QString path() const { return m_path; }
    QJsonObject load(const QVector<DiscoveredApplication> &applications, QString *error = nullptr) const;
    bool save(const QJsonObject &config, QString *error = nullptr) const;
    bool exportTo(const QJsonObject &config, const QString &path, QString *error = nullptr) const;
    bool importFrom(const QString &path, QJsonObject *config, QString *error = nullptr) const;
    bool backupCurrent(QString *backupPath = nullptr, QString *error = nullptr) const;

    static QJsonObject defaults(const QVector<DiscoveredApplication> &applications = {});
    static QJsonObject normalize(QJsonObject config, QString *error = nullptr);
    // Whether a parsed import has the shape of a settings file. Nothing is
    // repaired here: a file that fails is refused whole and changes nothing.
    static bool validateImport(const QJsonObject &raw, QString *error);
    static QJsonObject portable(const QJsonObject &config);
    static QJsonObject emptyAction();
    static QString freshId();

private:
    QString m_path;
    // A pre-0.5 config.json to start from while settings.json doesn't exist yet.
    QString m_legacyPath;
    mutable bool m_writeBlocked = false;
    static void pruneCopies(const QString &settingsPath, const QString &suffixPattern, const QString &keep);
    static bool writeJson(const QJsonObject &config, const QString &path, QString *error, bool privateFile = false);
};
