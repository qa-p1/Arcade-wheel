#include "config/ConfigStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace {
QJsonObject mergeDefaults(const QJsonObject &defaults, const QJsonObject &actual)
{
    QJsonObject result = defaults;
    for (auto it = actual.begin(); it != actual.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QJsonObject readJson(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return {};
    }
    return doc.object();
}
}

ConfigStore::ConfigStore(QString path)
    : m_path(path.isEmpty()
                 ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                       + QStringLiteral("/config.json")
                 : std::move(path))
{
}

QString ConfigStore::freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject ConfigStore::emptyAction()
{
    return {{QStringLiteral("id"), freshId()},
            {QStringLiteral("type"), QStringLiteral("none")},
            {QStringLiteral("name"), QStringLiteral("Choose action")},
            {QStringLiteral("icon"), QStringLiteral("applications-other")},
            {QStringLiteral("payload"), QJsonObject{}}};
}

QJsonObject ConfigStore::defaults(const QVector<DiscoveredApplication> &applications)
{
    QJsonArray actions;
    QSet<QString> chosen;
    const auto appendApp = [&](const DiscoveredApplication &app) {
        if (app.id.isEmpty() || app.name.isEmpty() || chosen.contains(app.id) || actions.size() == 6) return;
        chosen.insert(app.id);
        actions.append(QJsonObject{{QStringLiteral("id"), freshId()},
                                   {QStringLiteral("type"), QStringLiteral("application")},
                                   {QStringLiteral("name"), app.name},
                                   {QStringLiteral("icon"), app.icon},
                                   {QStringLiteral("payload"), QJsonObject{{QStringLiteral("desktopId"), app.id}}}});
    };
    // Pick familiar app roles from what is actually installed. No executable
    // name is assumed to exist, and the fallback still works on small systems.
    const QList<QStringList> roles = {
        {QStringLiteral("firefox"), QStringLiteral("zen browser"), QStringLiteral("chromium"),
         QStringLiteral("brave"), QStringLiteral("chrome"), QStringLiteral("web browser")},
        {QStringLiteral("kitty"), QStringLiteral("alacritty"), QStringLiteral("konsole"),
         QStringLiteral("wezterm"), QStringLiteral("foot"), QStringLiteral("terminal")},
        {QStringLiteral("dolphin"), QStringLiteral("nautilus"), QStringLiteral("thunar"),
         QStringLiteral("pcmanfm"), QStringLiteral("file manager"), QStringLiteral("files")},
        {QStringLiteral("code"), QStringLiteral("codium"), QStringLiteral("kate"),
         QStringLiteral("gedit"), QStringLiteral("editor")},
        {QStringLiteral("celluloid"), QStringLiteral("vlc"), QStringLiteral("mpv"),
         QStringLiteral("media player")},
        {QStringLiteral("screenshot"), QStringLiteral("spectacle"), QStringLiteral("flameshot"),
         QStringLiteral("calculator"), QStringLiteral("settings")}
    };
    for (const auto &hints : roles) {
        bool found = false;
        for (const auto &hint : hints) {
            for (const auto &app : applications) {
                const auto searchable = app.name + QLatin1Char(' ') + app.id + QLatin1Char(' ') + app.icon;
                if (!searchable.contains(hint, Qt::CaseInsensitive) || chosen.contains(app.id)) continue;
                appendApp(app);
                found = true;
                break;
            }
            if (found) break;
        }
    }
    for (const auto &app : applications) {
        if (actions.size() == 6) break;
        const auto searchable = app.name + QLatin1Char(' ') + app.id;
        if (searchable.contains(QStringLiteral("avahi"), Qt::CaseInsensitive) ||
            searchable.contains(QStringLiteral("server browser"), Qt::CaseInsensitive)) continue;
        appendApp(app);
    }
    while (actions.size() < 6) actions.append(emptyAction());

    return {{QStringLiteral("schemaVersion"), SchemaVersion},
            {QStringLiteral("general"), QJsonObject{{QStringLiteral("startOnLogin"), false},
                                                      {QStringLiteral("showNotifications"), true}}},
            {QStringLiteral("trigger"), QJsonObject{{QStringLiteral("shortcut"), QStringLiteral("F8")},
                                                      {QStringLiteral("holdThresholdMs"), 0}}},
            {QStringLiteral("appearance"), QJsonObject{{QStringLiteral("radius"), 148},
                                                         {QStringLiteral("scale"), 1.0},
                                                         {QStringLiteral("centerRadius"), 56},
                                                         {QStringLiteral("cardSize"), 96},
                                                         {QStringLiteral("gap"), 8},
                                                         {QStringLiteral("iconSize"), 40},
                                                         {QStringLiteral("labels"), false},
                                                         {QStringLiteral("opacity"), 0.94},
                                                         {QStringLiteral("dim"), 0.08},
                                                         {QStringLiteral("animationSpeed"), 1.0},
                                                         {QStringLiteral("selectedScale"), 1.06},
                                                         {QStringLiteral("reduceMotion"), false}}},
            {QStringLiteral("behaviour"), QJsonObject{{QStringLiteral("wrapDecks"), true},
                                                        {QStringLiteral("deadZone"), 64},
                                                        {QStringLiteral("selectionSensitivity"), 1.0},
                                                        {QStringLiteral("scrollCooldownMs"), 110},
                                                        {QStringLiteral("scrollReverse"), false},
                                                        {QStringLiteral("focusExisting"), true}}},
            {QStringLiteral("centerGestures"), QJsonObject{
                {QStringLiteral("clickIntervalMs"), 280},
                {QStringLiteral("longPressMs"), 500},
                {QStringLiteral("singleClick"), QJsonObject{{"name", "Quick launch"}, {"enabled", false}, {"actions", QJsonArray{}}}},
                {QStringLiteral("doubleClick"), QJsonObject{{"name", "Default workspace"}, {"enabled", false}, {"actions", QJsonArray{}}}},
                {QStringLiteral("tripleClick"), QJsonObject{{"name", "Alternate workspace"}, {"enabled", false}, {"actions", QJsonArray{}}}},
                {QStringLiteral("longPress"), QJsonObject{{"name", "Quick routine"}, {"enabled", false}, {"actions", QJsonArray{}}}}}},
            {QStringLiteral("decks"), QJsonArray{QJsonObject{{QStringLiteral("id"), freshId()},
                                                           {QStringLiteral("name"), QStringLiteral("Applications")},
                                                           {QStringLiteral("actions"), actions}}}}};
}

QJsonObject ConfigStore::normalize(QJsonObject config, QString *error)
{
    if (config.isEmpty()) {
        if (error) *error = QStringLiteral("Configuration is empty");
        return {};
    }
    const int version = config.value(QStringLiteral("schemaVersion")).toInt(0);
    if (version > SchemaVersion || version < 0) {
        if (error) *error = QStringLiteral("Unsupported configuration schema version %1").arg(version);
        return {};
    }
    if (version == 0) {
        if (!config.contains(QStringLiteral("decks")) && config.value(QStringLiteral("pages")).isArray())
            config.insert(QStringLiteral("decks"), config.value(QStringLiteral("pages")));
        config.remove(QStringLiteral("pages"));
    }
    if (version < 2) {
        auto appearance = config.value(QStringLiteral("appearance")).toObject();
        const QJsonObject oldStyle{{"radius",164},{"centerRadius",54},{"cardSize",82},
            {"gap",10},{"iconSize",30},{"opacity",0.88},{"selectedScale",1.10}};
        const auto newStyle = defaults().value(QStringLiteral("appearance")).toObject();
        for (auto it=oldStyle.begin(); it!=oldStyle.end(); ++it)
            if (appearance.value(it.key()) == it.value()) appearance.insert(it.key(),newStyle.value(it.key()));
        config.insert(QStringLiteral("appearance"),appearance);
    }
    const auto baseline = defaults();
    for (const auto &section : {"general", "trigger", "appearance", "behaviour"}) {
        const auto key = QString::fromLatin1(section);
        config.insert(key, mergeDefaults(baseline.value(key).toObject(), config.value(key).toObject()));
    }

    QJsonArray decks = config.value(QStringLiteral("decks")).toArray();
    if (decks.isEmpty()) decks = baseline.value(QStringLiteral("decks")).toArray();
    QJsonArray normalizedDecks;
    QSet<QString> ids;
    for (const auto &deckValue : decks) {
        if (!deckValue.isObject()) continue;
        QJsonObject deck = deckValue.toObject();
        QString deckId = deck.value(QStringLiteral("id")).toString();
        if (deckId.isEmpty() || ids.contains(deckId)) deckId = freshId();
        ids.insert(deckId);
        deck.insert(QStringLiteral("id"), deckId);
        if (deck.value(QStringLiteral("name")).toString().isEmpty()) deck.insert(QStringLiteral("name"), QStringLiteral("Deck"));
        QJsonArray actions = deck.value(QStringLiteral("actions")).toArray();
        if (actions.isEmpty() && deck.value(QStringLiteral("items")).isArray())
            actions = deck.value(QStringLiteral("items")).toArray();
        QJsonArray normalizedActions;
        for (const auto &value : actions) {
            if (!value.isObject()) continue;
            auto action = mergeDefaults(emptyAction(), value.toObject());
            QString actionId = action.value(QStringLiteral("id")).toString();
            if (actionId.isEmpty() || ids.contains(actionId)) actionId = freshId();
            ids.insert(actionId);
            action.insert(QStringLiteral("id"), actionId);
            if (!action.value(QStringLiteral("payload")).isObject()) action.insert(QStringLiteral("payload"), QJsonObject{});
            normalizedActions.append(action);
            if (normalizedActions.size() == 8) break;
        }
        while (normalizedActions.size() < 4) normalizedActions.append(emptyAction());
        deck.remove(QStringLiteral("items"));
        deck.insert(QStringLiteral("actions"), normalizedActions);
        normalizedDecks.append(deck);
    }
    if (normalizedDecks.isEmpty()) normalizedDecks = baseline.value(QStringLiteral("decks")).toArray();
    config.insert(QStringLiteral("decks"), normalizedDecks);
    auto gestures = mergeDefaults(baseline.value(QStringLiteral("centerGestures")).toObject(),
                                  config.value(QStringLiteral("centerGestures")).toObject());
    gestures.insert("clickIntervalMs", std::clamp(gestures.value("clickIntervalMs").toInt(280), 160, 500));
    gestures.insert("longPressMs", std::clamp(gestures.value("longPressMs").toInt(500), 250, 1500));
    for (const QString &key : {QStringLiteral("singleClick"), QStringLiteral("doubleClick"),
                               QStringLiteral("tripleClick"), QStringLiteral("longPress")}) {
        auto group = mergeDefaults(baseline.value("centerGestures").toObject().value(key).toObject(), gestures.value(key).toObject());
        QJsonArray actions;
        for (const auto &value : group.value("actions").toArray()) {
            if (!value.isObject()) continue;
            auto action = mergeDefaults(emptyAction(), value.toObject());
            QString id = action.value("id").toString();
            if (id.isEmpty() || ids.contains(id)) id = freshId();
            ids.insert(id);
            action.insert("id", id);
            if (!action.value("payload").isObject()) action.insert("payload", QJsonObject{});
            actions.append(action);
            if (actions.size() == 16) break;
        }
        group.insert("actions", actions);
        group.insert("enabled", group.value("enabled").toBool(false));
        gestures.insert(key, group);
    }
    config.insert("centerGestures", gestures);
    config.insert(QStringLiteral("schemaVersion"), SchemaVersion);
    return config;
}

QJsonObject ConfigStore::load(const QVector<DiscoveredApplication> &applications, QString *error) const
{
    m_writeBlocked = false;
    if (!QFileInfo::exists(m_path)) {
        auto config = defaults(applications);
        save(config, error);
        return config;
    }
    QString readError;
    auto config = readJson(m_path, &readError);
    if (config.isEmpty() && readError.isEmpty()) readError = QStringLiteral("Configuration is empty");
    if (!config.isEmpty()) config = normalize(config, &readError);
    if (config.isEmpty()) {
        const auto backup = m_path + QStringLiteral(".invalid-") + freshId() + QStringLiteral(".json");
        if (!QFile::copy(m_path, backup)) {
            m_writeBlocked = true;
            if (error) *error = QStringLiteral("%1. The original configuration could not be backed up; changes will not be saved.").arg(readError);
            return defaults(applications);
        }
        QString saveError;
        const auto replacement = defaults(applications);
        if (!writeJson(replacement, m_path, &saveError)) {
            m_writeBlocked = true;
            if (error) *error = QStringLiteral("%1. Original preserved at %2. Could not write defaults: %3").arg(readError, backup, saveError);
            return replacement;
        }
        if (error) *error = QStringLiteral("%1. Original preserved at %2; defaults loaded.").arg(readError, backup);
        return replacement;
    }
    return config;
}

bool ConfigStore::writeJson(const QJsonObject &config, const QString &path, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("Could not create configuration directory");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const auto bytes = QJsonDocument(config).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool ConfigStore::save(const QJsonObject &config, QString *error) const
{
    if (m_writeBlocked) {
        if (error) *error = QStringLiteral("Configuration writes are disabled until the original file can be backed up.");
        return false;
    }
    return writeJson(config, m_path, error);
}

bool ConfigStore::exportTo(const QJsonObject &config, const QString &path, QString *error) const
{
    return writeJson(config, path, error);
}

bool ConfigStore::importFrom(const QString &path, QJsonObject *config, QString *error) const
{
    QString readError;
    const auto imported = normalize(readJson(path, &readError), &readError);
    if (imported.isEmpty()) {
        if (error) *error = readError;
        return false;
    }
    *config = imported;
    return true;
}
