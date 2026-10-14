#include "install/AppInstaller.h"
#include "config/ConfigStore.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>
#include <QVersionNumber>

using namespace ArcadeLink;
namespace {
using Engine = AppInstaller::Engine;
// The nested event loop keeps dialogs responsive. Only the worker touches
// receipts, app files, registry discovery and installers.
template <typename F> auto onWorker(F work)
{
    using Result = decltype(work());
    Result result{};
    QEventLoop loop;
    auto *worker = QThread::create([&] { result = work(); });
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    loop.exec();
    worker->wait();
    delete worker;
    return result;
}
QString configDirectory()
{
    return QFileInfo(ConfigStore().path()).absolutePath();
}
QJsonObject identity(const QString &path)
{
    const QFileInfo file(path);
    if (!file.exists()) return {};
    return {{"path", file.canonicalFilePath()}, {"size", file.size()},
            {"modified", file.lastModified().toMSecsSinceEpoch()}};
}
QJsonArray dismissals(const QString &directory)
{
    QFile file(QDir(directory).filePath(QStringLiteral("install-dismissed.json")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}
QString launchPath(const QJsonObject &receipt)
{
    const QString path = receipt.value("path").toString();
#ifdef Q_OS_MACOS
    return QDir(path).filePath(QStringLiteral("Contents/MacOS/arcade-wheel"));
#else
    return path;
#endif
}
bool toolsAvailable(const Engine &engine)
{
    Registry registry(engine.environment().locations);
    registry.refresh();
    const auto tools = registry.app(Ids::Tools);
    return !tools.isEmpty() && tools.value("settings").toObject().value("linkEnabled").toBool(true);
}
bool handoff(const Engine &engine, const QString &version, QString *message)
{
    Registry registry(engine.environment().locations);
    registry.refresh();
    const auto tools = registry.app(Ids::Tools);
    if (tools.isEmpty()) { *message = QStringLiteral("Arcade Tools isn't installed."); return false; }
    QJsonObject action;
    for (const auto &value : tools.value("actions").toArray()) {
        const auto candidate = value.toObject();
        if (candidate.value("id").toString().section('#', 0, 0) == u"tools.install"
            && candidate.value("available").toBool(true)) action = candidate;
    }
    if (action.isEmpty()) { *message = QStringLiteral("This version of Arcade Tools cannot install Wheel."); return false; }
    const QJsonObject request{{"action", action.value("id").toString().section('#', 0, 0)},
        {"version", action.value("version").toInt(1)},
        {"inputs", QJsonArray{textContent(QStringLiteral("text/plain"), Ids::Wheel)}},
        {"options", QJsonObject{{"app", Ids::Wheel}}},
        {"context", QJsonObject{{"source", Ids::Wheel}}}};
    Error error;
    const bool ok = invokeAction(engine.environment().locations, Ids::Wheel, version, tools, request, nullptr, &error);
    if (!ok) *message = error.userMessage(QStringLiteral("Arcade Tools"));
    return ok;
}
bool quitResident(const Engine &engine, const QString &version, QString *message)
{
    Endpoint endpoint;
    if (!Endpoint::read(engine.environment().locations, Ids::Wheel, &endpoint)) return true;
    Error error;
    auto client = Client::connect(engine.environment().locations, Ids::Wheel, Ids::Tools, version, 1000, &error);
    if (!client) {
        // A stale endpoint does not make uninstalling an offline app fail.
        QLocalSocket probe; probe.connectToServer(endpoint.address);
        if (!probe.waitForConnected(150)) return true;
        *message = error.userMessage(QStringLiteral("Arcade Wheel")); return false;
    }
    const auto result = client->call("app.quit", {}, &error, 3000).toObject();
    if (error.isError() || !result.value("quitting").toBool()) { *message = error.userMessage(QStringLiteral("Arcade Wheel")); return false; }
    QElapsedTimer deadline; deadline.start();
    QJsonObject notification;
    while (deadline.elapsed() < 3000) {
        if (!client->nextNotification(&notification, &error, int(3000 - deadline.elapsed()))) {
            if (error.message == u"connection closed") return true;
            break;
        }
    }
    *message = QStringLiteral("Arcade Wheel did not finish quitting; the installation was kept.");
    return false;
}
QString format(const QString &text, const QString &version, const QString &from = {}, const QString &to = {})
{
    return Engine::formatString(text, QStringLiteral("Arcade Wheel"), version, from, to);
}
struct Choice { int button = 1; AppInstaller::Options options; };
Choice ask(const Engine::Detection &d, const QString &version, bool managed)
{
    const auto text = AppInstaller::dialogText(d, version);
    QDialog dialog;
    dialog.setWindowTitle(text.value("title").toString());
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(text.value("title").toString());
    title->setWordWrap(true);
    layout->addWidget(title);
    if (text.contains("body")) {
        auto *body = new QLabel(text.value("body").toString());
        body->setWordWrap(true);
        layout->addWidget(body);
    }
    if (managed) {
        auto *note = new QLabel(format(Engine::strings().value("managedByTools").toString(), version));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    Choice choice;
    choice.options.startAtLogin = !d.receipt.value("integration").toObject().value("autostart").toString().isEmpty();
    QCheckBox *login = nullptr, *download = nullptr;
    if (AppInstaller::promptKind(d, version) == u"notInstalled") {
        const auto labels = Engine::strings().value("notInstalled").toObject().value("options").toArray();
        login = new QCheckBox(labels.at(0).toString());
        download = new QCheckBox(labels.at(1).toString());
        download->setChecked(true);
        layout->addWidget(login);
        layout->addWidget(download);
    }
    auto *buttons = new QDialogButtonBox;
    const auto labels = text.value("buttons").toArray();
    for (qsizetype i = 0; i < labels.size(); ++i) {
        auto *button = buttons->addButton(labels.at(i).toString(), i == 0 ? QDialogButtonBox::AcceptRole : QDialogButtonBox::RejectRole);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, i] {
            choice.button = int(i);
            if (login) choice.options.startAtLogin = login->isChecked();
            if (download) choice.options.removeDownload = download->isChecked();
            dialog.accept();
        });
    }
    layout->addWidget(buttons);
    dialog.exec();
    return choice;
}
bool launchInstalled(const QString &path, const QStringList &args, QString *error)
{
    return Engine::reexec(path, args, error);
}
#ifdef Q_OS_MACOS
bool macReceipt(const Engine &engine, const QString &bundle, const QString &version,
                const QString &channel, const QJsonObject &old, QJsonObject *out, QString *error)
{
    QString cli;
    const auto status = engine.integrationStatus(Ids::Wheel, qEnvironmentVariable("SHELL"), error);
    if (!error->isEmpty()) return false;
    if (status.value("binOnPath").toBool()
        && !engine.installCli(QStringLiteral("arcade-wheel"), QDir(bundle).filePath("Contents/MacOS/arcade-wheel"), &cli, error)) return false;
    const QString now = nowRfc3339();
    QJsonObject receipt{{"schema", 1}, {"id", Ids::Wheel}, {"version", version}, {"channel", channel},
        {"method", "macos-bundle"}, {"managedBy", old.value("managedBy").toString("self")}, {"path", bundle},
        {"integration", QJsonObject{{"desktopEntry", QJsonValue::Null}, {"icons", QJsonArray{}},
            {"cli", cli.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(cli)},
            {"autostart", QJsonValue::Null}, {"uninstaller", QJsonValue::Null}}},
        {"installedAt", old.value("installedAt").toString(now)}, {"updatedAt", now}};
    if (old.contains("previous")) receipt.insert("previous", old.value("previous"));
    if (!engine.environment().receipts().write(receipt, error)) return false;
    *out = receipt;
    return true;
}
#endif
}

Engine::AppInfo AppInstaller::appInfo()
{
    return {Ids::Wheel, QStringLiteral("Arcade Wheel"), QStringLiteral("Arcade-Wheel.AppImage"),
            QStringLiteral("com.arcadewheel.ArcadeWheel"), QStringLiteral("arcade-wheel"),
            {QStringLiteral("--background")}, {QStringLiteral("arcade-wheel")}, QStringLiteral("arcade-wheel")};
}
QString AppInstaller::promptKind(const Engine::Detection &d, const QString &version)
{
    if (d.state == u"devBuild" || d.state == u"installedHere") return {};
    if (d.state == u"installedElsewhere") {
        const auto oldVersion = QVersionNumber::fromString(d.receipt.value("version").toString());
        const auto newVersion = QVersionNumber::fromString(version);
        return !oldVersion.isNull() && !newVersion.isNull() && oldVersion < newVersion
            ? QStringLiteral("older") : QStringLiteral("sameOrNewer");
    }
#ifdef Q_OS_MACOS
    if (d.runningFrom.endsWith(u".app")) return QStringLiteral("macos");
#endif
    return QStringLiteral("notInstalled");
}
QJsonObject AppInstaller::dialogText(const Engine::Detection &d, const QString &version)
{
    const auto kind = promptKind(d, version);
    auto text = Engine::strings().value(kind).toObject();
    const auto old = d.receipt.value("version").toString();
    for (const auto &key : {QStringLiteral("title"), QStringLiteral("body")})
        if (text.contains(key)) text.insert(key, format(text.value(key).toString(), kind == u"sameOrNewer" ? old : version, old, version));
    return text;
}
bool AppInstaller::dismissed(const QString &path, const QString &directory)
{
    const auto id = identity(path);
    return !id.isEmpty() && dismissals(directory).contains(id);
}
bool AppInstaller::dismiss(const QString &path, const QString &directory, QString *error)
{
    const auto id = identity(path);
    if (id.isEmpty()) { if (error) *error = QStringLiteral("The downloaded file no longer exists."); return false; }
    QJsonArray list;
    for (const auto &value : dismissals(directory))
        if (value.toObject().value("path") != id.value("path")) list.append(value);
    if (list.size() >= 128) list.removeAt(0);
    list.append(id);
    if (!ensurePrivateDir(directory)) { if (error) *error = QStringLiteral("Could not create the settings directory."); return false; }
    QSaveFile file(QDir(directory).filePath("install-dismissed.json"));
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
#ifdef Q_OS_UNIX
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) { if (error) *error = file.errorString(); return false; }
#endif
    const auto bytes = QJsonDocument(list).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) { if (error) *error = file.errorString(); return false; }
    return true;
}
QString AppInstaller::installMethod()
{
    const auto runtime = Engine::Runtime::discover();
    if (runtime.devBuild || Engine::isDevBuild(runtime.runningFrom())) return QStringLiteral("dev");
    Engine::Environment environment;
    QJsonObject receipt;
    if (Engine::Environment::discover(&environment)
        && environment.receipts().read(Ids::Wheel, &receipt) && !receipt.isEmpty())
        return receipt.value("method").toString(QStringLiteral("manual"));
    return QStringLiteral("manual");
}
bool AppInstaller::installCopy(const Engine &engine, const Engine::Runtime &runtime,
                               const Options &options, const QString &version,
                               QJsonObject *receipt, QString *error)
{
    Engine::Detection d;
    if (!engine.detect(Ids::Wheel, runtime, &d, error)) return false;
    if (d.state == u"devBuild") { *error = Engine::strings().value("errors").toObject().value("devBuild").toString(); return false; }
#ifdef Q_OS_LINUX
    if (!runtime.isAppImage()) { *error = QStringLiteral("Self-install requires an AppImage. Run the downloaded AppImage with --install."); return false; }
    Engine::InstallOptions opts;
    opts.app = appInfo(); opts.runtime = runtime; opts.version = version;
    opts.channel = options.channel;
    opts.managedBy = d.receipt.value("managedBy").toString(QStringLiteral("self"));
    opts.startAtLogin = options.startAtLogin;
    opts.removeDownload = options.removeDownload;
    opts.icons = {{QStringLiteral(":/assets/arcade-wheel.svg"), 0}};
    QTemporaryDir icons;
    const QImage image(QStringLiteral(":/assets/arcade-wheel.png"));
    if (!icons.isValid() || image.isNull()) { *error = QStringLiteral("Could not prepare Wheel's launcher icons."); return false; }
    for (const int pixels : {16, 32, 48, 64, 128, 256, 512}) {
        const QString path = icons.filePath(QString::number(pixels) + QStringLiteral(".png"));
        if (!image.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(path)) { *error = QStringLiteral("Could not prepare Wheel's launcher icon."); return false; }
        opts.icons.append({path, pixels});
    }
    return engine.install(opts, receipt, error);
#elif defined(Q_OS_MACOS)
    QString bundle = Engine::bundleFor(runtime.executable);
    if (bundle.isEmpty()) { *error = QStringLiteral("Self-install requires a macOS app bundle."); return false; }
    if (!engine.inApplications(bundle)) {
        const QString destination = qEnvironmentVariableIsEmpty("ARCADE_HOME") && QFileInfo(QStringLiteral("/Applications")).isWritable()
            ? QStringLiteral("/Applications") : QDir(engine.environment().home).filePath("Applications");
        const QString target = QDir(destination).filePath(QFileInfo(bundle).fileName());
        QString previous;
        if (QFileInfo::exists(target)) {
            if (d.receipt.value("path").toString() != target) { *error = QStringLiteral("Applications already contains a copy without Wheel's receipt."); return false; }
            previous = QDir(destination).filePath(".previous/" + QFileInfo(bundle).fileName());
            if (QFileInfo::exists(previous)) {
                if (d.receipt.value("previous").toObject().value("path").toString() != previous) { *error = QStringLiteral("The rollback location is already occupied."); return false; }
                if (!QDir(previous).removeRecursively()) { *error = QStringLiteral("Could not replace the previous bundle."); return false; }
            }
            if (!QDir().mkpath(QFileInfo(previous).absolutePath()) || !QDir().rename(target, previous)) { *error = QStringLiteral("Could not preserve the installed bundle."); return false; }
        }
        QString installed;
        if (!engine.moveToApplications(bundle, destination, &installed, error)) {
            if (!previous.isEmpty()) QDir().rename(previous, target);
            return false;
        }
        bundle = installed;
        if (!previous.isEmpty()) d.receipt.insert("previous", QJsonObject{{"version", d.receipt.value("version")}, {"path", previous}});
    }
    return macReceipt(engine, bundle, version, options.channel, d.receipt, receipt, error);
#else
    Q_UNUSED(options); Q_UNUSED(version); Q_UNUSED(receipt);
    *error = QStringLiteral("Use the per-user Windows setup installer to install Arcade Wheel.");
    return false;
#endif
}
bool AppInstaller::setStartOnLogin(const Engine &engine, bool enabled, QString *error)
{
    if (QThread::currentThread() == QCoreApplication::instance()->thread())
        return onWorker([&] { return setStartOnLogin(engine, enabled, error); });
#ifdef Q_OS_LINUX
    Engine::Detection d;
    if (!engine.detect(Ids::Wheel, Engine::Runtime::discover(), &d, error)) return false;
    if (d.receipt.isEmpty()) { *error = QStringLiteral("Install Arcade Wheel before enabling start at login."); return false; }
    const QString path = d.receipt.value("path").toString();
    Engine::InstallOptions opts;
    opts.app = appInfo(); opts.runtime.executable = path;
    opts.version = d.receipt.value("version").toString();
    opts.channel = d.receipt.value("channel").toString();
    opts.managedBy = d.receipt.value("managedBy").toString();
    opts.startAtLogin = enabled; opts.refreshCaches = false;
    QJsonObject installed;
    if (!engine.install(opts, &installed, error)) return false;
    // The pinned installer rebuilds receipts. Keep fields added by Tools or
    // newer Link versions when changing only the login preference.
    auto receipt = d.receipt;
    auto integration = receipt.value("integration").toObject();
    const auto updated = installed.value("integration").toObject();
    for (auto it = updated.begin(); it != updated.end(); ++it) integration.insert(it.key(), it.value());
    if (!enabled) {
        const QString autostart = integration.value("autostart").toString();
        if (!autostart.isEmpty() && QFileInfo::exists(autostart) && !QFile::remove(autostart)) {
            *error = QStringLiteral("Could not remove Wheel's login entry."); return false;
        }
        integration.insert("autostart", QJsonValue::Null);
    }
    receipt.insert("integration", integration);
    receipt.insert("updatedAt", nowRfc3339());
    return engine.environment().receipts().write(receipt, error);
#else
    Q_UNUSED(engine); Q_UNUSED(enabled); Q_UNUSED(error); return false;
#endif
}
bool AppInstaller::repairDesktop(const Engine &engine, QString *error)
{
    return onWorker([&] { return engine.repair(appInfo(), {}, nullptr, error); });
}
int AppInstaller::command(const QStringList &arguments, const QString &version)
{
    if (arguments.isEmpty()) return -1;
    const QString cmd = arguments.first();
    if (cmd != u"--install" && cmd != u"--uninstall" && cmd != u"--repair" && cmd != u"--integration-status") return -1;
    Options options;
    options.removeDownload = false; // explicit CLI keeps the source unless the dialog chooses otherwise
    bool silent = false, noLaunch = false, removeData = false;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const auto arg = arguments.at(i);
        if (arg == u"--silent" && (cmd == u"--install" || cmd == u"--uninstall")) silent = true;
        else if (arg == u"--no-launch" && cmd == u"--install") noLaunch = true;
        else if (arg == u"--remove-data" && cmd == u"--uninstall") removeData = true;
        else if (arg == u"--channel" && cmd == u"--install" && i + 1 < arguments.size()
                 && (arguments.at(i+1) == u"stable" || arguments.at(i+1) == u"nightly")) options.channel = arguments.at(++i);
        else { QTextStream(stderr) << "Invalid options for " << cmd << ". See arcade-wheel --help.\n"; return 2; }
    }
    Engine::Environment environment;
    QString error;
    if (!Engine::Environment::discover(&environment, &error)) { QTextStream(stderr) << error << '\n'; return 1; }
    Engine engine(environment);
    const auto runtime = Engine::Runtime::discover();
    Engine::Detection detection;
    if (!onWorker([&] { return engine.detect(Ids::Wheel, runtime, &detection, &error); })) { QTextStream(stderr) << error << '\n'; return 1; }
    options.startAtLogin = !detection.receipt.value("integration").toObject().value("autostart").toString().isEmpty();
    if (cmd == u"--install" && !silent && detection.state != u"devBuild") {
        auto dialogDetection = detection;
        if (dialogDetection.state == u"installedHere") dialogDetection.state = QStringLiteral("notInstalled");
        const bool managed = onWorker([&] { return detection.receipt.value("managedBy").toString() == u"tools" && toolsAvailable(engine); });
        const auto choice = ask(dialogDetection, version, managed);
        if (choice.button != 0) {
            if (choice.button == 2 && !onWorker([&] { return dismiss(detection.runningFrom, configDirectory(), &error); })) { QTextStream(stderr) << error << '\n'; return 1; }
            return 0;
        }
        const QString channel = options.channel;
        options = choice.options; options.channel = channel;
    }
    if (cmd == u"--uninstall" && !silent
        && QMessageBox::question(nullptr, QStringLiteral("Uninstall Arcade Wheel?"),
            removeData ? QStringLiteral("Remove Arcade Wheel and its settings and data?") : QStringLiteral("Remove Arcade Wheel? Your settings and data will be kept.")) != QMessageBox::Yes) return 0;
    QJsonObject receipt;
    const bool ok = onWorker([&] {
        if (cmd == u"--integration-status") { receipt = engine.integrationStatus(Ids::Wheel, qEnvironmentVariable("SHELL"), &error); return error.isEmpty(); }
        if (cmd == u"--install") {
            if (!silent && detection.receipt.value("managedBy").toString() == u"tools" && toolsAvailable(engine)) return handoff(engine, version, &error);
            return installCopy(engine, runtime, options, version, &receipt, &error);
        }
        if (cmd == u"--repair") {
#ifdef Q_OS_LINUX
            const QString moved = detection.state != u"devBuild" && runtime.isAppImage() && detection.installedPath.isEmpty() ? detection.runningFrom : QString();
            return engine.repair(appInfo(), moved, &receipt, &error);
#elif defined(Q_OS_MACOS)
            const QString bundle = detection.installedPath.isEmpty() ? Engine::bundleFor(runtime.executable) : detection.installedPath;
            if (detection.receipt.isEmpty() || !engine.inApplications(bundle)) { error = QStringLiteral("No installed bundle receipt was found."); return false; }
            return macReceipt(engine, bundle, detection.receipt.value("version").toString(), detection.receipt.value("channel").toString(), detection.receipt, &receipt, &error);
#else
            if (detection.receipt.isEmpty()) { error = QStringLiteral("No installation receipt was found."); return false; }
            receipt = detection.receipt;
            QString shim;
            if (!validateNativeReceiptPaths(receipt, &error) || !engine.writeWindowsShim("arcade-wheel", receipt.value("path").toString(), &shim, &error)) return false;
            auto integration = receipt.value("integration").toObject(); integration.insert("cli", shim); receipt.insert("integration", integration);
            return environment.receipts().write(receipt, &error);
#endif
        }
        Engine::UninstallOptions opts;
        if (!quitResident(engine, version, &error)) return false;
        opts.removeData = removeData;
        opts.dataFolders = {configDirectory(), QStandardPaths::writableLocation(QStandardPaths::CacheLocation), QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)};
#ifdef Q_OS_WIN
        if (detection.receipt.isEmpty()) return true;
        if (!validateNativeReceiptPaths(detection.receipt, &error)) return false;
        const auto uninstaller = detection.receipt.value("integration").toObject().value("uninstaller").toString();
        if (uninstaller.isEmpty()) { error = QStringLiteral("Use Apps & features to uninstall this copy."); return false; }
        QProcess process;
        process.start(uninstaller, {"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"});
        if (!process.waitForFinished(-1) || process.exitCode() != 0) { error = QStringLiteral("The Windows uninstaller failed."); return false; }
        if (removeData) for (const auto &path : opts.dataFolders) if (!path.isEmpty() && !QDir(path).removeRecursively()) { error = QStringLiteral("Could not remove Wheel data."); return false; }
        return true;
#else
#ifdef Q_OS_MACOS
        // The shared receipt names the .app directory, while Wheel's CLI
        // symlink targets the executable inside it. Remove only that owned
        // link; preserve replacements made outside Wheel.
        if (!detection.receipt.isEmpty() && !validateNativeReceiptPaths(detection.receipt, &error)) return false;
        const QString cli = detection.receipt.value("integration").toObject().value("cli").toString();
        const QFileInfo link(cli);
        if (!cli.isEmpty() && link.isSymLink() && link.symLinkTarget() == launchPath(detection.receipt)
            && !QFile::remove(cli)) { error = QStringLiteral("Could not remove Wheel's CLI symlink."); return false; }
#endif
        return engine.uninstall(Ids::Wheel, opts, nullptr, &error);
#endif
    });
    if (!ok) { QTextStream(stderr) << error << '\n'; return 1; }
    if (!receipt.isEmpty()) QTextStream(stdout) << QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    if (cmd == u"--install" && !noLaunch && !receipt.isEmpty() && !launchInstalled(launchPath(receipt), {}, &error)) { QTextStream(stderr) << error << '\n'; return 1; }
    return 0;
}
bool AppInstaller::firstRun(const QStringList &arguments, const QString &version, bool noPrompt)
{
    if (noPrompt || qEnvironmentVariable("ARCADE_NO_INSTALL_PROMPT") == u"1") return true;
    for (const auto &arg : arguments) if (arg.startsWith("--") && arg != u"--settings") return true;
    const auto runtime = Engine::Runtime::discover();
#ifdef Q_OS_LINUX
    if (!runtime.isAppImage()) return true;
#elif defined(Q_OS_WIN)
    Q_UNUSED(version); return true;
#endif
    Engine::Environment environment;
    QString error;
    if (!Engine::Environment::discover(&environment, &error)) { QTextStream(stderr) << error << '\n'; return true; }
    Engine engine(environment);
    Engine::Detection detection;
    bool managed = false, prompt = false;
    const bool detected = onWorker([&] {
        if (!engine.detect(Ids::Wheel, runtime, &detection, &error)) return false;
#ifdef Q_OS_MACOS
        const QString bundle = Engine::bundleFor(runtime.executable);
        if (bundle.isEmpty() || detection.state == u"devBuild") return true;
        if (engine.inApplications(bundle)) {
            if (detection.receipt.isEmpty()) { QJsonObject receipt; return macReceipt(engine, bundle, version, "stable", {}, &receipt, &error); }
            return true;
        }
        const bool move = Engine::isTranslocated(bundle) || bundle.startsWith(QDir(environment.home).filePath("Downloads/"))
            || bundle.startsWith("/Volumes/") || !QFileInfo(bundle).isWritable();
        if (!move) return true;
#endif
        prompt = Engine::shouldPrompt(detection, arguments, false, dismissed(detection.runningFrom, configDirectory()));
        managed = detection.receipt.value("managedBy").toString() == u"tools" && toolsAvailable(engine);
        return true;
    });
    if (!detected) { QTextStream(stderr) << error << '\n'; return true; }
    if (!prompt) return true;
    const auto choice = ask(detection, version, managed);
    if (choice.button != 0) {
        if (choice.button == 2 && !onWorker([&] { return dismiss(detection.runningFrom, configDirectory(), &error); })) QMessageBox::warning(nullptr, "Arcade Wheel", error);
        return true;
    }
    QJsonObject receipt;
    if (promptKind(detection, version) == u"sameOrNewer") receipt = detection.receipt;
    else {
        const bool ok = onWorker([&] { return managed ? handoff(engine, version, &error) : installCopy(engine, runtime, choice.options, version, &receipt, &error); });
        if (!ok) { QMessageBox::warning(nullptr, "Arcade Wheel", format(Engine::strings().value("errors").toObject().value("install").toString(), version, error)); return true; }
        if (managed) return false;
        const auto status = onWorker([&] { return engine.integrationStatus(Ids::Wheel, qEnvironmentVariable("SHELL"), &error); });
        if (status.value("pathHint").isString()) {
            QString hint = format(Engine::strings().value("pathHint").toString(), version, {}, status.value("pathHint").toString());
#ifdef Q_OS_MACOS
            hint += "\n\n" + engine.cliSymlinkOffer("arcade-wheel", launchPath(receipt));
#endif
            QMessageBox::information(nullptr, "Arcade Wheel", hint);
        }
    }
    if (!launchInstalled(launchPath(receipt), arguments, &error)) { QMessageBox::warning(nullptr, "Arcade Wheel", error); return true; }
    return false;
}
