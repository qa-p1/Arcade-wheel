#include "link/AppMetadata.h"
#include "providers/ArcadeLinkProvider.h"
#include "link/WheelInvoke.h"

#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMimeData>
#include <QTimer>
#include <QUuid>

using namespace ArcadeLink;

namespace {
// All filesystem and endpoint work in this class belongs to the discovery
// thread. Subscription sockets have no periodic timers; app.changed refreshes
// describe, and directory watches discover peers that start later.
class Discovery final : public QObject {
    Q_OBJECT
public:
    explicit Discovery(Locations locations) : loc(std::move(locations)) {}
    ~Discovery() override
    {
        for (auto &cancel : queries) cancel->store(true);
        for (auto *thread : queries.keys()) thread->wait();
    }
    void start()
    {
        registry = new Registry(loc, this);
        registry->watch();
        connect(registry, &Registry::changed, this, &Discovery::refresh);
        refresh();
    }
    void apply(const QJsonObject &settings)
    {
        enabled = settings.value("enabled").toBool(true);
        disabled = settings.value("disabledPeers").toArray();
        if (registry) refresh();
    }
    void refresh()
    {
        registry->refresh();
        const auto apps = registry->apps();
        QSet<QString> seen;
        for (const auto &app : apps) {
            const auto id = app.value("id").toString();
            if (id == Ids::Wheel) continue;
            seen.insert(id);
            Endpoint ep;
            const bool wanted = enabled && !disabled.contains(id)
                && app.value("settings").toObject().value("linkEnabled").toBool(true)
                && Endpoint::read(loc, id, &ep);
            if (peers.contains(id) && (!wanted || peers[id].token != ep.token)) remove(id);
            if (wanted && !peers.contains(id)) attach(id, ep);
        }
        for (const auto &id : peers.keys()) if (!seen.contains(id)) remove(id);
        refreshPipelines();
        publish();
    }
    void clipboard(const WheelClipboard &snapshot, quint64 generation);

signals:
    void snapshot(const QJsonArray &manifests, const QJsonObject &states, const QJsonObject &errors);
    void clipboardReady(quint64 generation, const QJsonArray &inputs, const QString &preview);

private:
    struct Peer {
        QLocalSocket *socket = nullptr;
        QTimer *deadline = nullptr;
        QString token;
        QByteArray buffer;
        bool authed = false;
        QJsonArray actions;
        bool described = false;
        bool pipelinesDirty = false;
        qint64 nextId = 3;
    };
    void remove(const QString &id)
    {
        if (!peers.contains(id)) return;
        const auto peer = peers.take(id);
        peer.deadline->stop();
        peer.deadline->disconnect(this);
        peer.socket->disconnect(this);
        peer.socket->abort();
        peer.socket->deleteLater();
    }
    void describe(const QString &id)
    {
        auto &peer = peers[id];
        peer.deadline->start(3000); // only a pending describe, never idle
        peer.socket->write(requestLine(++peer.nextId, "describe", {}));
    }
    void attach(const QString &id, const Endpoint &ep)
    {
        auto *socket = new QLocalSocket(this);
        auto *deadline = new QTimer(socket);
        deadline->setSingleShot(true);
        Peer peer;
        peer.socket = socket; peer.deadline = deadline; peer.token = ep.token;
        peer.pipelinesDirty = id == Ids::Box;
        peers.insert(id, peer);
        connect(deadline, &QTimer::timeout, this, [this, id] {
            errors.insert(id, standardMessage("timeout", AppMetadata::appName(id)));
            remove(id); publish();
        });
        connect(socket, &QLocalSocket::connected, this, [this, id] {
            auto &p = peers[id];
            p.socket->write(requestLine(1, "hello", {{"token", p.token}, {"protocol", QJsonArray{1}},
                         {"client", QJsonObject{{"id", Ids::Wheel}, {"version", "1"}}}}));
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, id] {
            if (!peers.contains(id)) return;
            errors.insert(id, standardMessage("not_running", AppMetadata::appName(id)));
            remove(id); publish();
        });
        connect(socket, &QLocalSocket::errorOccurred, this, [this, id](QLocalSocket::LocalSocketError) {
            if (!peers.contains(id)) return;
            errors.insert(id, standardMessage("not_running", AppMetadata::appName(id)));
            remove(id); publish();
        });
        connect(socket, &QLocalSocket::readyRead, this, [this, id] {
            if (!peers.contains(id)) return;
            auto &p = peers[id];
            p.buffer += p.socket->readAll();
            qsizetype nl;
            while ((nl = p.buffer.indexOf('\n')) >= 0) {
                const auto line = p.buffer.left(nl + 1); p.buffer.remove(0, nl + 1);
                QJsonObject message;
                if (classify(line, &message) == Kind::Invalid || message.contains("error")) {
                    errors.insert(id, message.contains("error") ? Error::fromJson(message.value("error").toObject()).userMessage(AppMetadata::appName(id))
                                                               : standardMessage("internal", AppMetadata::appName(id), "invalid response"));
                    remove(id); publish(); return;
                }
                const auto result = message.value("result").toObject();
                if (message.value("id").toInt() == 1) {
                    if (result.value("protocol").toInt() != 1) {
                        errors.insert(id, standardMessage("version_mismatch", AppMetadata::appName(id)));
                        remove(id); publish(); return;
                    }
                    p.authed = true; p.deadline->stop(); errors.remove(id);
                    p.socket->write(requestLine(2, "subscribe", {{"topics", QJsonArray{"app.changed"}}}));
                    describe(id); publish();
                } else if (result.contains("actions")) {
                    p.actions = result.value("actions").toArray(); p.described = true; p.deadline->stop();
                    const bool force = p.pipelinesDirty; p.pipelinesDirty = false;
                    if (id == Ids::Box) refreshPipelines(force);
                    publish();
                } else if (message.value("method").toString() == "app.changed") {
                    p.pipelinesDirty = true;
                    describe(id);
                }
            }
            if (p.buffer.size() > MaxLineBytes) {
                errors.insert(id, standardMessage("internal", AppMetadata::appName(id), "response exceeds the protocol limit"));
                remove(id); publish();
            }
        });
        deadline->start(HelloTimeoutMs);
        socket->connectToServer(ep.address);
    }
    void publish()
    {
        QJsonArray manifests;
        QJsonObject states;
        for (auto app : registry->apps()) {
            const auto id = app.value("id").toString();
            if (peers.contains(id) && peers[id].authed) {
                states.insert(id, "Running");
                if (peers[id].described) app.insert("actions", peers[id].actions);
            } else states.insert(id, "Installed");
            if (id == Ids::Box) {
                app.insert("pipelines", pipelines);
                app.insert("pipelinesLoaded", pipelinesLoaded);
            }
            manifests.append(app);
        }
        emit snapshot(manifests, states, errors);
    }
    void refreshPipelines(bool force = false)
    {
        QJsonObject manifest;
        for (const auto &app : registry->apps()) if (app.value("id").toString() == Ids::Box) manifest = app;
        if (peers.contains(Ids::Box) && peers[Ids::Box].described) manifest.insert("actions", peers[Ids::Box].actions);
        const auto list = WheelInvoke::actionFor(manifest, {{"action", "box.pipelines"}});
        const auto run = WheelInvoke::actionFor(manifest, {{"action", "box.pipeline.run"}});
        const bool wanted = enabled && !disabled.contains(Ids::Box)
            && !list.isEmpty() && !run.isEmpty() && actionUsable(manifest, list) && actionUsable(manifest, run);
        const auto signature = wanted ? QJsonDocument(manifest).toJson(QJsonDocument::Compact) : QByteArray();
        if (!force && signature == pipelineSignature) return;
        pipelineSignature = signature;
        const auto generation = ++pipelineGeneration;
        for (auto &cancel : queries) cancel->store(true);
        pipelines = {}; pipelinesLoaded = false;
        if (!wanted) return;
        const auto cancel = std::make_shared<std::atomic_bool>(false);
        auto *thread = QThread::create([this, manifest, generation, cancel, locations = loc] {
            Error error; QJsonObject result; QJsonArray values;
            const QJsonObject request{{"action", "box.pipelines"}, {"version", 1}, {"inputs", QJsonArray{}},
                {"options", QJsonObject{}}, {"context", QJsonObject{{"source", Ids::Wheel}, {"interactive", false}, {"reason", "discovery"}}}};
            // Never launch a resident UI just to fill an editor. Only a live
            // endpoint or Box's advertised headless entry point is used.
            Endpoint endpoint;
            if (!Endpoint::read(locations, Ids::Box, &endpoint)
                && !manifest.value("launch").toObject().value("invoke").isArray())
                error = Error::make("not_running", {});
            else WheelInvoke::run(locations, manifest, request, &result, &error, {}, cancel.get(), LaunchTimeoutMs);
            if (!error.isError()) {
                bool found = false;
                for (const auto &output : result.value("outputs").toArray()) {
                    const auto content = output.toObject();
                    if (content.value("type").toString() == "structured/pipelines" && content.value("data").isArray()) {
                        values = content.value("data").toArray(); found = true; break;
                    }
                }
                if (!found) error = Error::make("internal", {}, "invalid pipeline listing");
            }
            QMetaObject::invokeMethod(this, [this, generation, values, error] {
                if (generation != pipelineGeneration) return;
                pipelines = values; pipelinesLoaded = !error.isError();
                if (error.isError()) errors.insert(Ids::Box, error.userMessage(AppMetadata::appName(Ids::Box)));
                else errors.remove(Ids::Box);
                publish();
            });
        });
        thread->setParent(this);
        queries.insert(thread, cancel);
        connect(thread, &QThread::finished, this, [this, thread] { queries.remove(thread); thread->deleteLater(); });
        thread->start();
    }
    Locations loc;
    Registry *registry = nullptr;
    QHash<QString, Peer> peers;
    QJsonObject errors;
    QJsonArray pipelines;
    bool pipelinesLoaded = false;
    QByteArray pipelineSignature;
    quint64 pipelineGeneration = 0;
    QHash<QThread *, std::shared_ptr<std::atomic_bool>> queries;
    bool enabled = true;
    QJsonArray disabled;
};

QString handoffFile(const Locations &loc, const QString &name, const QByteArray &bytes, QStringList *owned)
{
    const QString dir = QDir(loc.handoff).filePath(QUuid::createUuid().toString(QUuid::Id128));
    if (!ensurePrivateDir(dir)) return {};
    owned->append(dir);
    QFile file(QDir(dir).filePath(name));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(bytes) != bytes.size()) return {};
    return file.fileName();
}

QJsonArray clipboardInputs(const WheelClipboard &snapshot, const Locations &loc, QStringList *owned = nullptr)
{
    if (!snapshot.urls.isEmpty()) {
        QJsonArray paths;
        qint64 bytes = 0;
        for (const auto &url : snapshot.urls) {
            if (!url.isLocalFile()) continue;
            const auto path = url.toLocalFile();
            const QFileInfo info(path);
            if (!info.exists()) return {};
            paths.append(path); bytes += info.isFile() ? info.size() : 0;
        }
        if (paths.size() == 1) return {fileContent(paths.first().toString())};
        if (!paths.isEmpty()) return {QJsonObject{{"type", "file/any[]"}, {"paths", paths}, {"size", bytes}}};
    }
    if (!snapshot.image.isNull()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!snapshot.image.save(&buffer, "PNG")) return {};
        QJsonObject content{{"type", "file/image"}, {"size", png.size()}};
        if (owned) {
            const auto path = handoffFile(loc, "clipboard.png", png, owned);
            if (path.isEmpty()) return {};
            content.insert("path", path); content.insert("owner", Ids::Wheel);
        }
        return {content};
    }
    if (snapshot.text.isEmpty() && snapshot.html.isEmpty()) return {};
    const QUrl url(snapshot.text.trimmed());
    auto content = textContent(!snapshot.html.isEmpty() ? "text/rich" : url.isValid() && !url.scheme().isEmpty() ? "text/url" : "text/plain", snapshot.text);
    if (!snapshot.html.isEmpty()) content.insert("html", snapshot.html);
    const auto bytes = snapshot.text.toUtf8();
    content.insert("size", bytes.size() + snapshot.html.toUtf8().size());
    if (owned && bytes.size() + snapshot.html.toUtf8().size() > 256 * 1024) {
        const auto path = handoffFile(loc, "clipboard.txt", bytes, owned);
        if (path.isEmpty()) return {};
        content.remove("text"); content.insert("path", path); content.insert("owner", Ids::Wheel);
        if (!snapshot.html.isEmpty()) {
            const auto htmlPath = handoffFile(loc, "clipboard.html", snapshot.html.toUtf8(), owned);
            if (htmlPath.isEmpty()) return {};
            content.remove("html"); content.insert("htmlPath", htmlPath);
        }
    }
    return {content};
}

void Discovery::clipboard(const WheelClipboard &value, quint64 generation)
{
    const auto inputs = clipboardInputs(value, loc);
    QString preview;
    if (!inputs.isEmpty()) {
        const auto content = inputs.first().toObject();
        preview = content.value("text").toString().left(120);
        if (preview.isEmpty()) preview = content.value("path").toString();
        if (preview.isEmpty()) preview = content.value("type").toString();
    }
    emit clipboardReady(generation, inputs, preview);
}

bool outbound(const QJsonObject &action)
{
    const auto effects = action.value("effects").toArray();
    return effects.contains("sends-to-device") || effects.contains("uploads-content") || effects.contains("network")
        || action.value("privacy").toString() == "cloud" || action.value("privacy").toString() == "network";
}

bool selectionCompatible(const QJsonArray &accepts, const QString &produces)
{
    if (acceptsType(accepts, produces)) return true;
    // A file-manager resolver advertises all file kinds. The selected kind is
    // checked on the worker after resolveOnly returns the actual files.
    if (produces == "file/*[]" || produces == "file/any[]")
        for (const auto &type : accepts) if (type.toString().startsWith("file/")) return true;
    return false;
}

QJsonArray selectionInputs(const QJsonArray &accepts, const QJsonArray &outputs)
{
    QJsonArray inputs;
    QMap<QString, QJsonArray> batches;
    QJsonArray values;
    for (const auto &value : outputs) {
        const auto content = value.toObject();
        if (content.value("paths").isArray()) {
            for (const auto &path : content.value("paths").toArray()) values.append(fileContent(path.toString()));
        } else values.append(content);
    }
    for (const auto &value : values) {
        const auto content = value.toObject();
        const auto type = content.value("type").toString();
        if (type.startsWith("file/") && content.contains("path") && acceptsType(accepts, type + "[]"))
            batches[type].append(content.value("path"));
        else if (acceptsContent(accepts, content)) inputs.append(content);
    }
    for (auto it = batches.cbegin(); it != batches.cend(); ++it)
        inputs.append(QJsonObject{{"type", it.key() + "[]"}, {"paths", it.value()}});
    return inputs;
}
}

ArcadeLinkProvider::ArcadeLinkProvider(QObject *parent, int invokeTimeoutMs)
    : QObject(parent), m_locations(Locations::discover()), m_timeoutMs(invokeTimeoutMs)
{
}

ArcadeLinkProvider::~ArcadeLinkProvider()
{
    for (auto &job : m_jobs) job.cancel->store(true);
    for (auto &job : m_jobs) job.thread->wait();
    m_discoveryThread.quit();
    m_discoveryThread.wait();
}

void ArcadeLinkProvider::startDiscovery()
{
    if (m_started) return;
    m_started = true;
    auto *worker = new Discovery(m_locations);
    m_worker = worker;
    worker->moveToThread(&m_discoveryThread);
    connect(&m_discoveryThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(&m_discoveryThread, &QThread::started, worker, [worker, link = m_link] { worker->apply(link); worker->start(); });
    connect(worker, &Discovery::snapshot, this, [this](const QJsonArray &manifests, const QJsonObject &states, const QJsonObject &errors) {
        m_manifests = manifests; m_states = states; m_errors = errors; emit changed();
    });
    connect(worker, &Discovery::clipboardReady, this, [this](quint64 generation, const QJsonArray &inputs, const QString &preview) {
        if (generation != m_clipboardGeneration) return;
        m_clipboardInputs = inputs; m_clipboardPreview = preview; emit changed();
    });
    m_discoveryThread.start();
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ArcadeLinkProvider::refreshClipboard);
        refreshClipboard();
    }
}

void ArcadeLinkProvider::apply(const QJsonObject &link)
{
    if (m_started && m_link == link) return;
    m_link = link;
    m_enabled = link.value("enabled").toBool(true);
    m_disabledPeers = link.value("disabledPeers").toArray();
    for (auto &job : m_jobs) if (!m_enabled || m_disabledPeers.contains(job.app)) job.cancel->store(true);
    if (!m_started) startDiscovery();
    else QMetaObject::invokeMethod(m_worker, [worker = static_cast<Discovery *>(m_worker), link] { worker->apply(link); });
    emit changed();
}

void ArcadeLinkProvider::refresh()
{
    if (!m_started) startDiscovery();
    else QMetaObject::invokeMethod(m_worker, [worker = static_cast<Discovery *>(m_worker)] { worker->refresh(); });
}

void ArcadeLinkProvider::refreshClipboard()
{
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    m_clipboard = {};
    if (mime) {
        m_clipboard.text = mime->text(); m_clipboard.html = mime->html();
        m_clipboard.urls = mime->urls();
        if (mime->hasImage()) m_clipboard.image = qvariant_cast<QImage>(mime->imageData());
    }
    ++m_clipboardGeneration;
    // Do not display stale content while its metadata is being converted.
    m_clipboardInputs = {}; m_clipboardPreview.clear(); emit changed();
    if (m_worker) QMetaObject::invokeMethod(m_worker, [worker = static_cast<Discovery *>(m_worker), value = m_clipboard, generation = m_clipboardGeneration] {
        worker->clipboard(value, generation);
    });
}

QJsonObject ArcadeLinkProvider::manifestFor(const QString &id) const
{
    for (const auto &value : m_manifests) if (value.toObject().value("id").toString() == id) return value.toObject();
    return {};
}

QString ArcadeLinkProvider::referenceReason(const QString &id, const QJsonObject &payload) const
{
    const QString name = AppMetadata::appName(id);
    // Switched off here, not in the peer: say where to turn it back on.
    if (!m_enabled) return QStringLiteral("Connections to other Arcade apps are off in Connected apps.");
    if (m_disabledPeers.contains(id)) return QStringLiteral("%1 is turned off in Connected apps.").arg(name);
    const auto manifest = manifestFor(id);
    if (manifest.isEmpty()) return standardMessage("not_installed", name);
    if (!manifest.value("settings").toObject().value("linkEnabled").toBool(true)) return standardMessage("denied", name, "disabled");
    const auto action = WheelInvoke::actionFor(manifest, payload);
    if (action.isEmpty()) return standardMessage("unavailable", name, "the saved action is unavailable");
    if (action.value("version").toInt(1) != payload.value("version").toInt(1)) return standardMessage("version_mismatch", name);
    if (!actionUsable(manifest, action)) return standardMessage("unavailable", name, action.value("reason").toString());
    return {};
}

QString ArcadeLinkProvider::unavailableReason() const
{
    return m_enabled ? QStringLiteral("No Arcade actions are available") : standardMessage("denied", AppMetadata::appName(Ids::Wheel), "disabled");
}

QString ArcadeLinkProvider::unavailableReason(const QJsonObject &slot) const
{
    const auto payload = slot.value("payload").toObject();
    const auto id = payload.value("app").toString();
    const auto name = AppMetadata::appName(id);
    const auto reason = referenceReason(id, payload);
    if (!reason.isEmpty()) return reason;
    const auto action = WheelInvoke::actionFor(manifestFor(id), payload);
    const auto accepts = action.value("accepts").toArray();
    const auto mode = payload.value("input").toString("none");
    if (mode == "none")
        return accepts.isEmpty() || accepts.contains("*") ? QString() : standardMessage("unsupported_input", name);
    if (mode == "clipboard") {
        if (m_clipboardInputs.isEmpty()) return standardMessage("unsupported_input", name);
        qint64 size = 0;
        for (const auto &value : m_clipboardInputs) {
            const auto content = value.toObject();
            if (!acceptsContent(accepts, content)) return standardMessage("unsupported_input", name);
            size += content.value("size").toInteger();
        }
        const auto limit = action.value("maxBytes").toInteger(-1);
        return limit >= 0 && size > limit ? standardMessage("too_large", name, {}, limit) : QString();
    }
    const QString resolverApp = mode == "lens-selection" ? Ids::Lens : Ids::Look;
    const QString resolverAction = mode == "lens-selection" ? "lens.capture" : "look.preview_selection";
    const QJsonObject resolverPayload{{"action", resolverAction}, {"version", 1}};
    const auto resolverReason = referenceReason(resolverApp, resolverPayload);
    if (!resolverReason.isEmpty()) return resolverReason;
    const auto resolver = WheelInvoke::actionFor(manifestFor(resolverApp), resolverPayload);
    if (mode != "lens-selection" && mode != "file-selection") return standardMessage("unsupported_input", name);
    for (const auto &type : resolver.value("produces").toArray())
        if (selectionCompatible(accepts, type.toString())) return {};
    return standardMessage("unavailable", AppMetadata::appName(resolverApp), "the selection resolver doesn't return content for this action");
}

QStringList ArcadeLinkProvider::inputModes(const QJsonObject &action) const
{
    QStringList modes;
    const auto accepts = action.value("accepts").toArray();
    if (accepts.isEmpty() || accepts.contains("*")) modes << "none";
    if (!accepts.isEmpty()) modes << "clipboard";
    for (const auto &pair : {QPair{Ids::Lens, QStringLiteral("lens.capture")}, QPair{Ids::Look, QStringLiteral("look.preview_selection")}}) {
        const QJsonObject payload{{"action", pair.second}, {"version", 1}};
        if (!referenceReason(pair.first, payload).isEmpty()) continue;
        const auto resolver = WheelInvoke::actionFor(manifestFor(pair.first), payload);
        for (const auto &type : resolver.value("produces").toArray()) {
            if (!selectionCompatible(accepts, type.toString())) continue;
            modes << (pair.first == Ids::Lens ? "lens-selection" : "file-selection"); break;
        }
    }
    return modes;
}

QVariantList ArcadeLinkProvider::tools() const
{
    QVariantList rows;
    if (!m_enabled) return rows;
    for (const auto &value : m_manifests) {
        const auto manifest = value.toObject();
        const auto id = manifest.value("id").toString();
        if (id == Ids::Wheel || id == Ids::Tools || m_disabledPeers.contains(id)) continue;
        for (const auto &value : manifest.value("actions").toArray()) {
            auto action = value.toObject();
            if (id == Ids::Box && (action.value("id").toString() == "box.pipeline.run" || action.value("id").toString() == "box.pipelines")) continue;
            if (!actionUsable(manifest, action)) continue;
            const auto modes = inputModes(action);
            if (modes.isEmpty()) continue;
            action.insert("app", id); action.insert("appName", AppMetadata::appName(id));
            action.insert("version", action.value("version").toInt(1));
            action.insert("inputModes", QJsonArray::fromStringList(modes));
            action.insert("input", modes.first());
            action.insert("outbound", outbound(action));
            action.insert("glyph", QStringLiteral("qrc:/assets/arcade/%1.svg").arg(id));
            rows.append(action.toVariantMap());
        }
        if (id == Ids::Box) for (const auto &pipeline : manifest.value("pipelines").toArray()) {
            const auto entry = pipeline.toObject();
            if (entry.value("id").toString().isEmpty() || entry.value("name").toString().isEmpty()) continue;
            auto action = WheelInvoke::actionFor(manifest, {{"action", "box.pipeline.run"},
                {"options", QJsonObject{{"pipeline", entry.value("id")}}}});
            if (action.isEmpty() || !actionUsable(manifest, action)) continue;
            const auto modes = inputModes(action);
            if (modes.isEmpty()) continue;
            action.insert("app", id); action.insert("appName", AppMetadata::appName(id));
            action.insert("version", action.value("version").toInt(1));
            action.insert("inputModes", QJsonArray::fromStringList(modes)); action.insert("input", modes.first());
            action.insert("outbound", outbound(action));
            action.insert("glyph", QStringLiteral("qrc:/assets/arcade/%1.svg").arg(id));
            rows.append(action.toVariantMap());
        }
    }
    return rows;
}

bool ArcadeLinkProvider::execute(const QJsonObject &slot, QString *error)
{
    const auto reason = unavailableReason(slot);
    if (!reason.isEmpty()) { if (error) *error = reason; return false; }
    const auto payload = slot.value("payload").toObject();
    const auto id = payload.value("app").toString();
    const auto manifest = manifestFor(id);
    const auto action = WheelInvoke::actionFor(manifest, payload);
    const auto jobId = QUuid::createUuid().toString(QUuid::Id128);
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    const auto mode = payload.value("input").toString("none");
    const QString preview = mode == "clipboard" ? m_clipboardPreview : mode == "lens-selection" ? "Region you select in Arcade Lens"
        : mode == "file-selection" ? "File manager selection" : "No input";
    const bool sends = outbound(action);
    emit jobStarted(jobId, id, slot.value("name").toString(action.value("title").toString()), preview, sends);
    const auto loc = m_locations;
    const auto snapshot = m_clipboard;
    const auto resolverApp = mode == "lens-selection" ? Ids::Lens : Ids::Look;
    const auto resolverManifest = manifestFor(resolverApp);
    auto *thread = QThread::create([this, loc, payload, manifest, resolverManifest, resolverApp, snapshot, cancel, jobId, mode, timeout = m_timeoutMs] {
        QStringList owned;
        Error failure;
        QJsonObject result;
        QJsonArray inputs;
        QString failingApp = manifest.value("id").toString();
        const auto progress = [this, jobId](double fraction, const QString &message) {
            QMetaObject::invokeMethod(this, [this, jobId, fraction, message] { emit jobProgress(jobId, fraction, message); });
        };
        if (mode == "clipboard") {
            inputs = clipboardInputs(snapshot, loc, &owned);
            if (inputs.isEmpty()) failure = Error::make("unsupported_input", {});
        } else if (mode == "lens-selection" || mode == "file-selection") {
            const QJsonObject request{{"action", mode == "lens-selection" ? "lens.capture" : "look.preview_selection"}, {"version", 1},
                {"inputs", QJsonArray{}}, {"options", mode == "file-selection" ? QJsonObject{{"resolveOnly", true}} : QJsonObject{}},
                {"context", QJsonObject{{"source", Ids::Wheel}, {"interactive", true}, {"reason", "user-click"}}}};
            failingApp = resolverApp;
            WheelInvoke::run(loc, resolverManifest, request, &result, &failure, progress, cancel.get(), timeout);
            if (!failure.isError()) {
                const auto target = WheelInvoke::actionFor(manifest, payload);
                inputs = selectionInputs(target.value("accepts").toArray(), result.value("outputs").toArray());
                failingApp = manifest.value("id").toString();
                if (inputs.isEmpty()) failure = Error::make("unsupported_input", {});
            }
        }
        if (!failure.isError()) {
            QJsonObject request{{"action", payload.value("action")}, {"version", payload.value("version").toInt(1)},
                {"inputs", inputs}, {"options", payload.value("options").toObject()},
                {"context", QJsonObject{{"source", Ids::Wheel}, {"interactive", true}, {"reason", "user-click"}}}};
            if (payload.contains("preset")) request.insert("preset", payload.value("preset"));
            WheelInvoke::run(loc, manifest, request, &result, &failure, progress, cancel.get(), timeout);
        }
        for (const auto &dir : owned) QDir(dir).removeRecursively();
        const auto message = failure.isError() ? failure.userMessage(AppMetadata::appName(failingApp)) : QString();
        QMetaObject::invokeMethod(this, [this, jobId, result, message] {
            emit jobFinished(jobId, result, message);
            refresh();
        });
    });
    thread->setParent(this);
    m_jobs.insert(jobId, {id, cancel, thread});
    connect(thread, &QThread::finished, this, [this, jobId, thread] { m_jobs.remove(jobId); thread->deleteLater(); });
    thread->start();
    return true;
}

void ArcadeLinkProvider::cancel(const QString &job)
{
    if (m_jobs.contains(job)) m_jobs[job].cancel->store(true);
}

bool ArcadeLinkProvider::requestInstall(const QString &app)
{
    const auto manifest = manifestFor(Ids::Tools);
    if (!referenceReason(Ids::Tools, {{"action", "tools.install"}, {"version", 1}}).isEmpty()) return false;
    QMetaObject::invokeMethod(m_worker, [this, manifest, app, loc = m_locations] {
        Error error; QJsonObject result;
        const QJsonObject request{{"action", "tools.install"}, {"version", 1},
            {"inputs", QJsonArray{textContent("text/plain", app)}}, {"options", QJsonObject{{"app", app}}},
            {"context", QJsonObject{{"source", Ids::Wheel}, {"interactive", true}, {"reason", "user-click"}}}};
        WheelInvoke::run(loc, manifest, request, &result, &error, {}, nullptr, LaunchTimeoutMs);
        if (error.isError()) {
            const auto message = error.userMessage(AppMetadata::appName(Ids::Tools));
            QMetaObject::invokeMethod(this, [this, message] { emit openFailed(message); });
        }
    });
    return true;
}

QVariantList ArcadeLinkProvider::connectedApps() const
{
    QVariantList rows;
    auto ids = AppMetadata::apps();
    if (!ids.contains(Ids::Tools)) ids.append(Ids::Tools);
    for (const auto &id : ids) {
        if (id == Ids::Wheel) continue;
        const auto manifest = manifestFor(id);
        const bool installed = !manifest.isEmpty();
        if (id == Ids::Tools && installed) continue; // Its row only offers Get Arcade Tools.
        const auto state = m_states.value(id).toString();
        rows.append(QVariantMap{{"id", id}, {"name", AppMetadata::appName(id)}, {"pitch", AppMetadata::appPitch(id)},
            {"glyph", QStringLiteral("qrc:/assets/arcade/%1.svg").arg(id)},
            {"state", state == "Running" ? "Running · v" + manifest.value("version").toString() : installed ? "Installed" : "Not installed"},
            {"installed", installed}, {"enabled", !m_disabledPeers.contains(id)},
            {"endpoint", state == "Running" ? "Connected" : "No live endpoint"},
            {"lastError", m_errors.value(id).toString()}, {"registry", m_locations.registry}});
    }
    return rows;
}

QString ArcadeLinkProvider::shortcutOwner(const QString &accelerator) const
{
    const auto normalized = normalizeAccelerator(accelerator);
    if (normalized.isEmpty()) return {};
    for (const auto &value : m_manifests) {
        const auto manifest = value.toObject();
        if (manifest.value("id").toString() == Ids::Wheel) continue;
        for (const auto &shortcut : manifest.value("shortcuts").toArray())
            if (normalizeAccelerator(shortcut.toObject().value("accelerator").toString()) == normalized)
                return AppMetadata::appName(manifest.value("id").toString());
    }
    return {};
}

#include "ArcadeLinkProvider.moc"
