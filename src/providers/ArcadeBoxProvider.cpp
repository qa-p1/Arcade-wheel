#include "providers/ArcadeBoxProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

ArcadeBoxProvider::ArcadeBoxProvider()
{
    refresh();
}

QString ArcadeBoxProvider::unavailableReason() const
{
    return available() ? QString() : QStringLiteral("Arcade Box is not installed");
}

void ArcadeBoxProvider::refresh()
{
    m_executable = QStandardPaths::findExecutable(QStringLiteral("arcade-box"));
    m_tools.clear();
    if (m_executable.isEmpty()) return;

    // Tiny, versionable CLI boundary. A provider failure never affects the wheel.
    QProcess process;
    process.start(m_executable, {QStringLiteral("tools"), QStringLiteral("--json")});
    if (!process.waitForFinished(500) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return;
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
    const auto tools = document.isArray() ? document.array() : document.object().value(QStringLiteral("tools")).toArray();
    for (const auto &tool : tools) {
        const auto object = tool.toObject();
        if (object.value(QStringLiteral("id")).toString().isEmpty()) continue;
        m_tools.append(object.toVariantMap());
    }
}

bool ArcadeBoxProvider::execute(const QJsonObject &action, QString *error)
{
    if (!available()) {
        if (error) *error = unavailableReason();
        return false;
    }
    const auto payload = action.value(QStringLiteral("payload")).toObject();
    const auto toolId = payload.value(QStringLiteral("toolId")).toString();
    if (toolId.isEmpty()) {
        if (error) *error = QStringLiteral("Choose an Arcade Box tool");
        return false;
    }
    QStringList arguments{QStringLiteral("run"), QStringLiteral("--tool"), toolId};
    const auto input = payload.value(QStringLiteral("input")).toString();
    const auto preset = payload.value(QStringLiteral("preset")).toString();
    if (!input.isEmpty()) arguments << QStringLiteral("--input") << input;
    if (!preset.isEmpty()) arguments << QStringLiteral("--preset") << preset;
    if (!QProcess::startDetached(m_executable, arguments)) {
        if (error) *error = QStringLiteral("Could not start Arcade Box");
        return false;
    }
    return true;
}
