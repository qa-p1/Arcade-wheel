#include "actions/ActionDispatcher.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

ActionDispatcher::ActionDispatcher(PlatformBackend *backend) : m_backend(backend)
{
    auto box = std::make_unique<ArcadeBoxProvider>();
    m_arcadeBox = box.get();
    registerProvider(std::move(box));
}

void ActionDispatcher::registerProvider(std::unique_ptr<ActionProvider> provider)
{
    if (provider) m_providers.push_back(std::move(provider));
}

ActionProvider *ActionDispatcher::provider(const QString &id) const
{
    for (const auto &candidate : m_providers)
        if (candidate->id() == id) return candidate.get();
    return nullptr;
}

void ActionDispatcher::setApplications(QVector<DiscoveredApplication> applications)
{
    m_applications = std::move(applications);
}

QString ActionDispatcher::unavailableReason(const QJsonObject &action) const
{
    const auto type = action.value(QStringLiteral("type")).toString();
    const auto payload = action.value(QStringLiteral("payload")).toObject();
    if (type == QStringLiteral("none")) return QStringLiteral("Choose an action");
    if (type == QStringLiteral("application")) {
        const auto id = payload.value(QStringLiteral("desktopId")).toString();
        for (const auto &app : m_applications) if (app.id == id) return {};
        return QStringLiteral("Application is not installed");
    }
    if (type == QStringLiteral("command")) {
        const auto command = payload.value(QStringLiteral("command")).toString();
        const auto parts = QProcess::splitCommand(command);
        if (parts.isEmpty()) return QStringLiteral("Command is empty");
        if (QStandardPaths::findExecutable(parts.first()).isEmpty()) return QStringLiteral("Executable is unavailable");
        return {};
    }
    if (type == QStringLiteral("url"))
        return payload.value(QStringLiteral("url")).toString().isEmpty() ? QStringLiteral("URL is empty") : QString();
    if (type == QStringLiteral("file"))
        return QFileInfo::exists(payload.value(QStringLiteral("path")).toString()) ? QString() : QStringLiteral("File or folder is missing");
    if (type == QStringLiteral("system") || type == QStringLiteral("media") || type == QStringLiteral("desktop"))
        return payload.value(QStringLiteral("id")).toString().isEmpty() ? QStringLiteral("Action is incomplete") : QString();
    if (type == QStringLiteral("arcade_box") || type == QStringLiteral("plugin")) {
        const auto id = type == QStringLiteral("arcade_box") ? QStringLiteral("arcade_box")
                                                              : payload.value(QStringLiteral("providerId")).toString();
        const auto *resolved = provider(id);
        if (!resolved) return QStringLiteral("Provider is unavailable");
        if (!resolved->available()) return resolved->unavailableReason();
        if (payload.value(QStringLiteral("toolId")).toString().isEmpty()) return QStringLiteral("Choose a provider tool");
        return {};
    }
    return QStringLiteral("Unknown action type");
}

bool ActionDispatcher::execute(const QJsonObject &action, bool focusExisting, QString *error)
{
    const auto unavailable = unavailableReason(action);
    if (!unavailable.isEmpty()) {
        if (error) *error = unavailable;
        return false;
    }
    const auto type = action.value(QStringLiteral("type")).toString();
    const auto payload = action.value(QStringLiteral("payload")).toObject();
    if (type == QStringLiteral("application"))
        return m_backend->launchApplication(payload.value(QStringLiteral("desktopId")).toString(), focusExisting, error);
    if (type == QStringLiteral("command")) {
        auto parts = QProcess::splitCommand(payload.value(QStringLiteral("command")).toString());
        const auto program = parts.takeFirst();
        const auto workingDirectory = payload.value(QStringLiteral("workingDirectory")).toString();
        if (!QProcess::startDetached(program, parts, workingDirectory)) {
            if (error) *error = QStringLiteral("Could not start %1").arg(program);
            return false;
        }
        return true;
    }
    if (type == QStringLiteral("url")) {
        const auto url = QUrl::fromUserInput(payload.value(QStringLiteral("url")).toString());
        if (QDesktopServices::openUrl(url)) return true;
        if (error) *error = QStringLiteral("Could not open URL");
        return false;
    }
    if (type == QStringLiteral("file")) {
        const auto url = QUrl::fromLocalFile(payload.value(QStringLiteral("path")).toString());
        if (QDesktopServices::openUrl(url)) return true;
        if (error) *error = QStringLiteral("Could not open file or folder");
        return false;
    }
    if (type == QStringLiteral("system"))
        return m_backend->performSystemAction(payload.value(QStringLiteral("id")).toString(), error);
    if (type == QStringLiteral("media"))
        return m_backend->performSystemAction(QStringLiteral("media:") + payload.value(QStringLiteral("id")).toString(), error);
    if (type == QStringLiteral("desktop"))
        return m_backend->performSystemAction(QStringLiteral("desktop:") + payload.value(QStringLiteral("id")).toString(), error);
    if (type == QStringLiteral("arcade_box") || type == QStringLiteral("plugin")) {
        const auto id = type == QStringLiteral("arcade_box") ? QStringLiteral("arcade_box")
                                                              : payload.value(QStringLiteral("providerId")).toString();
        return provider(id)->execute(action, error);
    }
    if (error) *error = QStringLiteral("Provider is unavailable");
    return false;
}
