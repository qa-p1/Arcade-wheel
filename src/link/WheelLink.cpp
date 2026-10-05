#include "link/WheelLink.h"

#include "core/WheelController.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QUrl>

using namespace ArcadeLink;

WheelLink::WheelLink(QString version, QObject *parent)
    : QObject(parent), m_version(std::move(version)), m_locations(Locations::discover())
{
}

WheelLink::~WheelLink() { stop(); }

QJsonArray WheelLink::actions()
{
    const QJsonArray all{QStringLiteral("linux"), QStringLiteral("windows"), QStringLiteral("macos")};
    return {
        QJsonObject{{QStringLiteral("id"), QStringLiteral("wheel.add_action")}, {QStringLiteral("version"), 1},
                    {QStringLiteral("title"), QStringLiteral("Add to Wheel")}, {QStringLiteral("verb"), QStringLiteral("add")},
                    {QStringLiteral("accepts"), QJsonArray{QStringLiteral("text/url"), QStringLiteral("text/plain;hint=command"),
                                                           QStringLiteral("file/*"), QStringLiteral("folder/reference")}},
                    {QStringLiteral("produces"), QJsonArray{}},
                    {QStringLiteral("effects"), QJsonArray{QStringLiteral("persists"), QStringLiteral("opens-ui")}},
                    {QStringLiteral("interactive"), true}, {QStringLiteral("privacy"), QStringLiteral("local")},
                    {QStringLiteral("platforms"), all}, {QStringLiteral("available"), true}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("wheel.show")}, {QStringLiteral("version"), 1},
                    {QStringLiteral("title"), QStringLiteral("Show the Wheel")}, {QStringLiteral("verb"), QStringLiteral("show")},
                    {QStringLiteral("accepts"), QJsonArray{}}, {QStringLiteral("produces"), QJsonArray{}},
                    {QStringLiteral("effects"), QJsonArray{QStringLiteral("opens-ui")}},
                    {QStringLiteral("interactive"), true}, {QStringLiteral("privacy"), QStringLiteral("local")},
                    {QStringLiteral("platforms"), all}, {QStringLiteral("available"), true}},
    };
}

QVariantMap WheelLink::draftFor(const QJsonObject &content, QString *error)
{
    const QString type = content.value(QStringLiteral("type")).toString();
    const QString text = content.value(QStringLiteral("text")).toString().trimmed();
    const QString path = content.value(QStringLiteral("path")).toString();
    if (typeMatches(QStringLiteral("text/url"), type)) {
        const QUrl url = QUrl::fromUserInput(text);
        if (!url.isValid() || text.isEmpty()) {
            if (error) *error = QStringLiteral("The URL is empty or invalid");
            return {};
        }
        return {{QStringLiteral("type"), QStringLiteral("url")},
                {QStringLiteral("name"), url.host().isEmpty() ? QStringLiteral("Website") : url.host()},
                {QStringLiteral("icon"), QStringLiteral("internet-web-browser")},
                {QStringLiteral("payload"), QVariantMap{{QStringLiteral("url"), text}}}};
    }
    if (contentMatches(QStringLiteral("text/plain;hint=command"), content)) {
        // Wheel runs commands directly, never through a shell; the picker says so.
        const QStringList parts = QProcess::splitCommand(text);
        if (parts.isEmpty()) {
            if (error) *error = QStringLiteral("The command is empty");
            return {};
        }
        return {{QStringLiteral("type"), QStringLiteral("command")},
                {QStringLiteral("name"), QFileInfo(parts.first()).fileName()},
                {QStringLiteral("icon"), QStringLiteral("utilities-terminal")},
                {QStringLiteral("payload"), QVariantMap{{QStringLiteral("command"), text}}}};
    }
    if (type.startsWith(QStringLiteral("file/")) || type == u"folder/reference") {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            if (error) *error = QStringLiteral("The file or folder doesn't exist");
            return {};
        }
        return {{QStringLiteral("type"), QStringLiteral("file")},
                {QStringLiteral("name"), QFileInfo(path).fileName()},
                {QStringLiteral("icon"), QStringLiteral("folder")},
                {QStringLiteral("payload"), QVariantMap{{QStringLiteral("path"), path}}}};
    }
    if (error) *error = QStringLiteral("Arcade Wheel can't make an action from %1").arg(type);
    return {};
}

void WheelLink::setController(WheelController *controller)
{
    m_controller = controller;
    connect(controller, &WheelController::linkActionFinished, this, [this](bool saved, const QString &where) {
        if (!m_pendingAdd) return;
        const Responder r = *m_pendingAdd;
        m_pendingAdd.reset();
        if (saved)
            r.finish({{QStringLiteral("message"), where.isEmpty() ? QStringLiteral("Added to the Wheel") : QStringLiteral("Added to %1").arg(where)}});
        else
            r.finishError(Error::make(QStringLiteral("denied"), QStringLiteral("cancelled in Arcade Wheel"), QStringLiteral("user_cancelled")));
    });
}

void WheelLink::invoke(const QJsonObject &request, const Responder &responder)
{
    const QString action = request.value(QStringLiteral("action")).toString();
    if (!m_controller) {
        responder.fail(Error::make(QStringLiteral("not_running"), QStringLiteral("Arcade Wheel isn't ready")));
        return;
    }
    if (action == u"wheel.show") {
        m_controller->showWheel();
        responder.done({{QStringLiteral("message"), QStringLiteral("The Wheel is open")}});
        return;
    }
    if (action != u"wheel.add_action") {
        responder.fail(Error::make(QStringLiteral("unavailable"), QStringLiteral("no such action"), QStringLiteral("Arcade Wheel has no action %1").arg(action)));
        return;
    }
    const QJsonArray inputs = request.value(QStringLiteral("inputs")).toArray();
    if (inputs.isEmpty()) {
        responder.fail(Error::make(QStringLiteral("unsupported_input"), QStringLiteral("nothing to add")));
        return;
    }
    QString error;
    const QVariantMap draft = draftFor(inputs.first().toObject(), &error);
    if (draft.isEmpty()) {
        responder.fail(Error::make(QStringLiteral("unsupported_input"), error));
        return;
    }
    const QJsonObject context = request.value(QStringLiteral("context")).toObject();
    const QString source = appName(context.value(QStringLiteral("source")).toString(responder.peerId()));
    // Nothing is saved until the user picks a slot and confirms in Settings.
    if (!m_controller->beginLinkAction(draft, source)) {
        responder.fail(Error::make(QStringLiteral("busy"), QStringLiteral("another action is waiting to be placed")));
        return;
    }
    responder.startJob();
    m_pendingAdd = responder;
}

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
        m_server->invoke = [this](const QJsonObject &request, const Responder &r) { invoke(request, r); };
        m_server->activate = [this] {
            if (m_controller) m_controller->requestSettings();
            return bool(m_controller);
        };
        m_server->quit = [this] {
            if (m_controller) m_controller->requestQuit();
            return bool(m_controller);
        };
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
