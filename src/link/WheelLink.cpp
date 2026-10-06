#include "link/WheelLink.h"

#include "core/WheelController.h"

#include <QFileInfo>
#include <QThread>
#include <QJsonArray>
#include <QProcess>
#include <QUrl>

using namespace ArcadeLink;

struct WheelLink::PresenceState { std::unique_ptr<Server> server; };

WheelLink::WheelLink(QString version, QObject *parent)
    : QObject(parent), m_version(std::move(version)), m_locations(Locations::discover()), m_presence(std::make_shared<PresenceState>())
{
}

WheelLink::~WheelLink()
{
    stop();
    if (m_ioWorker && m_ioWorker->thread()->isRunning())
        QMetaObject::invokeMethod(m_ioWorker, [] {}, Qt::BlockingQueuedConnection);
}

QJsonArray WheelLink::actions()
{
    const QJsonArray all{QStringLiteral("linux"), QStringLiteral("windows"), QStringLiteral("macos")};
    return {
        QJsonObject{{QStringLiteral("id"), QStringLiteral("wheel.add_action")}, {QStringLiteral("version"), 1},
                    {QStringLiteral("title"), QStringLiteral("Add to Wheel")}, {QStringLiteral("verb"), QStringLiteral("add")},
                    {QStringLiteral("accepts"), QJsonArray{QStringLiteral("text/url"), QStringLiteral("text/plain;hint=command"),
                                                           QStringLiteral("file/*"), QStringLiteral("folder/reference"), QStringLiteral("structured/arcade-action")}},
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
    if (type == u"structured/arcade-action") {
        const auto data = content.value(QStringLiteral("data")).toObject();
        const auto app = data.value(QStringLiteral("app")).toString();
        const auto action = data.value(QStringLiteral("action")).toString();
        const auto input = data.value(QStringLiteral("input")).toString(QStringLiteral("none"));
        const QStringList inputs{QStringLiteral("none"), QStringLiteral("clipboard"), QStringLiteral("lens-selection"), QStringLiteral("file-selection")};
        if (!Ids::apps().contains(app) || action.isEmpty() || data.value(QStringLiteral("version")).toInt(0) < 1
            || data.value(QStringLiteral("title")).toString().isEmpty() || !inputs.contains(input)
            || (data.contains(QStringLiteral("options")) && !data.value(QStringLiteral("options")).isObject())) {
            if (error) *error = QStringLiteral("The Arcade action reference is incomplete or invalid");
            return {};
        }
        QJsonObject payload{{QStringLiteral("app"), app}, {QStringLiteral("action"), action},
            {QStringLiteral("version"), data.value(QStringLiteral("version"))}, {QStringLiteral("input"), input},
            {QStringLiteral("options"), data.value(QStringLiteral("options")).toObject()}};
        if (data.contains(QStringLiteral("preset"))) payload.insert(QStringLiteral("preset"), data.value(QStringLiteral("preset")));
        return {{QStringLiteral("type"), QStringLiteral("arcade")}, {QStringLiteral("name"), data.value(QStringLiteral("title")).toString()},
                {QStringLiteral("icon"), QStringLiteral("qrc:/assets/arcade/%1.svg").arg(app)},
                {QStringLiteral("payload"), payload.toVariantMap()}};
    }
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
    m_ioWorker = controller->arcadeWorker();
    m_addCancelTimer.setInterval(100);
    connect(&m_addCancelTimer, &QTimer::timeout, this, [this] {
        if (m_pendingAdd && m_pendingAdd->cancelled() && m_controller) m_controller->finishLinkAction(false);
    });
    connect(controller, &WheelController::linkActionFinished, this, [this](bool saved, const QString &where) {
        if (!m_pendingAdd) return;
        const Responder r = *m_pendingAdd;
        m_pendingAdd.reset();
        m_addCancelTimer.stop();
        if (saved)
            respond(r, [where](const Responder &r) { r.finish({{QStringLiteral("message"), where.isEmpty() ? QStringLiteral("Added to the Wheel") : QStringLiteral("Added to %1").arg(where)}}); });
        else
            respond(r, [](const Responder &r) { r.finishError(Error::make(QStringLiteral("denied"), QStringLiteral("cancelled in Arcade Wheel"), QStringLiteral("user_cancelled"))); });
    });
}

void WheelLink::invoke(const QJsonObject &request, const Responder &responder, const QVariantMap &draft, const QString &draftError)
{
    if (m_controller && !m_controller->config().value("link").toMap().value("enabled", true).toBool()) {
        respond(responder, [](const Responder &r) { r.fail(Error::make("denied", {}, "disabled")); });
        return;
    }
    if (request.value("version").toInt(1) != 1) {
        respond(responder, [](const Responder &r) { r.fail(Error::make("version_mismatch", {})); });
        return;
    }
    const QString action = request.value(QStringLiteral("action")).toString();
    if (!m_controller) {
        respond(responder, [](const Responder &r) { r.fail(Error::make(QStringLiteral("not_running"), QStringLiteral("Arcade Wheel isn't ready"))); });
        return;
    }
    if (action == u"wheel.show") {
        m_controller->showWheel();
        respond(responder, [](const Responder &r) { r.done({{QStringLiteral("message"), QStringLiteral("The Wheel is open")}}); });
        return;
    }
    if (action != u"wheel.add_action") {
        respond(responder, [action](const Responder &r) { r.fail(Error::make(QStringLiteral("unavailable"), QStringLiteral("no such action"), QStringLiteral("Arcade Wheel has no action %1").arg(action))); });
        return;
    }
    const QJsonArray inputs = request.value(QStringLiteral("inputs")).toArray();
    if (inputs.isEmpty()) {
        respond(responder, [](const Responder &r) { r.fail(Error::make(QStringLiteral("unsupported_input"), QStringLiteral("nothing to add"))); });
        return;
    }
    const auto error = draftError;
    if (draft.isEmpty()) {
        respond(responder, [error](const Responder &r) { r.fail(Error::make(QStringLiteral("unsupported_input"), error)); });
        return;
    }
    const QJsonObject context = request.value(QStringLiteral("context")).toObject();
    const QString source = appName(context.value(QStringLiteral("source")).toString(responder.peerId()));
    // Nothing is saved until the user picks a slot and confirms in Settings.
    if (!m_controller->beginLinkAction(draft, source)) {
        respond(responder, [](const Responder &r) { r.fail(Error::make(QStringLiteral("busy"), QStringLiteral("another action is waiting to be placed"))); });
        return;
    }
    respond(responder, [](const Responder &r) { r.startJob(); });
    m_pendingAdd = responder;
    m_addCancelTimer.start();
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

void WheelLink::respond(const Responder &r, std::function<void(const Responder &)> reply)
{
    if (m_ioWorker) QMetaObject::invokeMethod(m_ioWorker, [r, reply = std::move(reply)] { reply(r); });
}

void WheelLink::apply(const QJsonObject &config)
{
    if (!m_ioWorker) return;
    const auto manifest = WheelLink::manifest(config, m_version);
    const auto state = m_presence;
    const auto loc = m_locations;
    const auto version = m_version;
    QMetaObject::invokeMethod(m_ioWorker, [this, state, loc, version, manifest] {
        QString error;
        const bool changed = writeManifest(loc, manifest, &error);
        const bool enabled = manifest.value("settings").toObject().value("linkEnabled").toBool();
        if (enabled && !state->server) {
            state->server = std::make_unique<Server>(Ids::Wheel, version, loc);
            state->server->describe = [] { return WheelLink::actions(); };
            state->server->invoke = [this](const QJsonObject &request, const Responder &r) {
                QString error;
                QVariantMap draft;
                if (request.value("action").toString() == "wheel.add_action") {
                    const auto inputs = request.value("inputs").toArray();
                    if (!inputs.isEmpty()) draft = draftFor(inputs.first().toObject(), &error);
                }
                QMetaObject::invokeMethod(this, [this, request, r, draft, error] { invoke(request, r, draft, error); });
            };
            state->server->activate = [this] {
                QMetaObject::invokeMethod(this, [this] { if (m_controller) m_controller->requestSettings(); });
                return true;
            };
            state->server->quit = [this] {
                QMetaObject::invokeMethod(this, [this] { if (m_controller) m_controller->requestQuit(); });
                return true;
            };
            if (!state->server->start(&error)) state->server.reset();
        } else if (!enabled) state->server.reset();
        else if (changed) state->server->notifyChanged();
        const bool live = state->server && state->server->listening();
        QMetaObject::invokeMethod(this, [this, live, error] { m_listening.store(live); m_lastError = error; emit diagnosticsChanged(live ? QStringLiteral("Listening") : QStringLiteral("Not listening"), error); });
    });
    if (!config.value("link").toObject().value("enabled").toBool(true) && m_controller)
        m_controller->finishLinkAction(false);
}

void WheelLink::stop()
{
    m_addCancelTimer.stop();
    m_listening.store(false);
    if (m_ioWorker) QMetaObject::invokeMethod(m_ioWorker, [state = m_presence] { state->server.reset(); });
}

bool WheelLink::listening() const { return m_listening.load(); }
