#pragma once

#include "providers/ActionProvider.h"
#include "link/ArcadeLink.h"

#include <QImage>
#include <QObject>
#include <QThread>
#include <QUrl>

struct WheelClipboard {
    QString text;
    QString html;
    QImage image;
    QList<QUrl> urls;
};

// Registry and IPC workers publish immutable snapshots. Menus, slot state and
// shortcut recording use only that cache, with no disk access or IPC.
class ArcadeLinkProvider final : public QObject, public ActionProvider {
    Q_OBJECT
public:
    explicit ArcadeLinkProvider(QObject *parent = nullptr, int invokeTimeoutMs = 120000);
    ~ArcadeLinkProvider() override;
    QString id() const override { return QStringLiteral("arcade"); }
    bool available() const override { return !tools().isEmpty(); }
    QString unavailableReason() const override;
    QString unavailableReason(const QJsonObject &action) const;
    QVariantList tools() const override;
    bool execute(const QJsonObject &action, QString *error) override;
    void apply(const QJsonObject &link);
    void refresh();
    void refreshClipboard();
    QObject *ioWorker() const { return m_worker; }
    void cancel(const QString &job);
    QVariantList connectedApps() const;
    QString shortcutOwner(const QString &accelerator) const;
    QJsonObject manifestFor(const QString &app) const;
    const ArcadeLink::Locations &locations() const { return m_locations; }

signals:
    void changed();
    void jobStarted(const QString &job, const QString &app, const QString &title, const QString &preview, bool outbound);
    void jobProgress(const QString &job, double fraction, const QString &message);
    void jobFinished(const QString &job, const QJsonObject &result, const QString &error);

private:
    QString referenceReason(const QString &app, const QJsonObject &payload) const;
    QStringList inputModes(const QJsonObject &action) const;
    void startDiscovery();
    ArcadeLink::Locations m_locations;
    QThread m_discoveryThread;
    QObject *m_worker = nullptr;
    QJsonArray m_manifests;
    QJsonObject m_states;
    QJsonObject m_errors;
    QJsonObject m_link;
    WheelClipboard m_clipboard;
    QJsonArray m_clipboardInputs;
    QString m_clipboardPreview;
    quint64 m_clipboardGeneration = 0;
    bool m_started = false;
    bool m_enabled = true;
    QJsonArray m_disabledPeers;
    int m_timeoutMs;
    struct ActiveJob { QString app; std::shared_ptr<std::atomic_bool> cancel; QThread *thread; };
    QHash<QString, ActiveJob> m_jobs;
};
