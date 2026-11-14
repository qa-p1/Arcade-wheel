#include "config/ConfigStore.h"
#include "core/WheelPaths.h"

#include <QColor>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
QJsonObject mergeDefaults(const QJsonObject &defaults, const QJsonObject &actual)
{
    QJsonObject result = defaults;
    for (auto it = actual.begin(); it != actual.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

// Preserve the complete legacy payload, including its old free-form input.
// Only the new input mode gets normalized; the original remains repairable.
QJsonObject migrateAction(QJsonObject action)
{
    if (action.value("type").toString() != "arcade_box") return action;
    const auto legacy = action.value("payload").toObject();
    auto payload = legacy;
    payload.insert("app", "arcade.box");
    payload.insert("action", "box:" + legacy.value("toolId").toString());
    payload.insert("version", 1);
    const auto oldInput = legacy.value("input").toString();
    const QStringList modes{"none", "clipboard", "lens-selection", "file-selection"};
    payload.insert("input", modes.contains(oldInput) ? oldInput : oldInput.startsWith("clipboard") ? "clipboard"
                                                                                                   : "none");
    payload.insert("legacyPayload", legacy);
    action.insert("type", "arcade");
    action.insert("payload", payload);
    return action;
}

QJsonObject readJson(const QString &path, QString *error, qint64 maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    if (file.size() > maxBytes) {
        if (error) *error = QStringLiteral("The file is larger than %1 MiB, so it is not a settings file.").arg(maxBytes / (1024 * 1024));
        return {};
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return {};
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("This is not a settings file: the top level must be a JSON object.");
        return {};
    }
    return doc.object();
}

const QStringList &actionTypes()
{
    static const QStringList types{QStringLiteral("none"), QStringLiteral("application"), QStringLiteral("command"), QStringLiteral("url"),
                                   QStringLiteral("file"), QStringLiteral("system"), QStringLiteral("media"), QStringLiteral("desktop"),
                                   QStringLiteral("arcade"), QStringLiteral("plugin"), QStringLiteral("arcade_box")};
    return types;
}

bool hasLongText(const QJsonValue &value, int limit)
{
    if (value.isString()) return value.toString().size() > limit;
    if (value.isArray()) {
        for (const auto &entry : value.toArray())
            if (hasLongText(entry, limit)) return true;
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key().size() > limit || hasLongText(it.value(), limit)) return true;
    }
    return false;
}

QJsonValue cutText(const QJsonValue &value, int limit)
{
    if (value.isString()) return value.toString().left(limit);
    if (value.isArray()) {
        QJsonArray out;
        for (const auto &entry : value.toArray()) out.append(cutText(entry, limit));
        return out;
    }
    if (value.isObject()) {
        QJsonObject out;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) out.insert(it.key().left(limit), cutText(it.value(), limit));
        return out;
    }
    return value;
}

// An action must be an object with a known type and bounded text. `where`
// names it for the message ("Deck 2, action 3"), never quoting user text.
bool validateAction(const QJsonValue &value, const QString &where, QSet<QString> *ids, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error) *error = where + QStringLiteral(": ") + why;
        return false;
    };
    if (!value.isObject()) return fail(QStringLiteral("an action must be an object."));
    const auto action = value.toObject();
    const auto type = action.value(QStringLiteral("type"));
    if (type.isUndefined()) return fail(QStringLiteral("the action has no type."));
    if (!type.isString() || !actionTypes().contains(type.toString())) return fail(QStringLiteral("unknown action type."));
    for (const auto &key : {QStringLiteral("name"), QStringLiteral("icon"), QStringLiteral("id")}) {
        const auto field = action.value(key);
        if (field.isUndefined()) continue;
        if (!field.isString()) return fail(QStringLiteral("%1 must be text.").arg(key));
        if (field.toString().size() > (key == QStringLiteral("name") ? ConfigStore::MaxNameLength : ConfigStore::MaxTextLength))
            return fail(QStringLiteral("%1 is too long.").arg(key));
    }
    const QString id = action.value(QStringLiteral("id")).toString();
    if (!id.isEmpty() && ids) {
        if (ids->contains(id)) return fail(QStringLiteral("this id is already used by another deck or action."));
        ids->insert(id);
    }
    const auto payload = action.value(QStringLiteral("payload"));
    if (!payload.isUndefined() && !payload.isObject()) return fail(QStringLiteral("the payload must be an object."));
    if (hasLongText(payload, ConfigStore::MaxTextLength)) return fail(QStringLiteral("the payload has text longer than %1 characters.").arg(ConfigStore::MaxTextLength));
    return true;
}

bool validateActionList(const QJsonValue &list, int limit, const QString &where, QSet<QString> *ids, QString *error)
{
    if (!list.isArray()) {
        if (error) *error = where + QStringLiteral(": the actions must be a list.");
        return false;
    }
    const auto actions = list.toArray();
    if (actions.size() > limit) {
        if (error) *error = where + QStringLiteral(": %1 actions, but at most %2 are allowed.").arg(actions.size()).arg(limit);
        return false;
    }
    for (int i = 0; i < actions.size(); ++i)
        if (!validateAction(actions.at(i), where + QStringLiteral(", action %1").arg(i + 1), ids, error)) return false;
    return true;
}

// Appearance and behaviour values the UI offers only inside these bounds.
struct NumberRange {
    const char *section;
    const char *key;
    double low;
    double high;
};
constexpr NumberRange kNumberRanges[] = {
    {"appearance", "radius", 110, 240},
    {"appearance", "scale", 0.75, 1.3},
    {"appearance", "centerRadius", 36, 72},
    {"appearance", "cardSize", 66, 110},
    {"appearance", "gap", 4, 24},
    {"appearance", "iconSize", 24, 42},
    {"appearance", "opacity", 0.45, 1},
    {"appearance", "dim", 0, 0.3},
    {"appearance", "selectedScale", 1, 1.12},
    {"appearance", "animationSpeed", 0.5, 1.5},
    {"behaviour", "deadZone", 20, 180},
    {"behaviour", "selectionSensitivity", 0.5, 2},
    {"behaviour", "scrollCooldownMs", 0, 500},
    {"trigger", "holdThresholdMs", 0, 1000},
};

// A value of the wrong type becomes the default; a number outside the
// range the UI offers is brought into it.
QJsonObject repairSection(const QString &section, const QJsonObject &defaults, QJsonObject actual)
{
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        const auto value = actual.value(it.key());
        const auto fallback = it.value();
        bool wrongType = false;
        if (fallback.isBool()) wrongType = !value.isBool();
        else if (fallback.isDouble()) wrongType = !value.isDouble();
        else if (fallback.isString()) wrongType = !value.isString() || value.toString().size() > ConfigStore::MaxNameLength;
        else if (fallback.isArray()) wrongType = !value.isArray();
        if (wrongType) actual.insert(it.key(), fallback);
    }
    for (const auto &range : kNumberRanges) {
        if (section != QLatin1String(range.section)) continue;
        const auto value = actual.value(QLatin1String(range.key));
        if (value.isDouble()) actual.insert(QLatin1String(range.key), std::clamp(value.toDouble(), range.low, range.high));
    }
    return actual;
}

bool isSensitiveField(QString key)
{
    key = key.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    const bool deviceIdentity = key.contains(QStringLiteral("device")) && (key.contains(QStringLiteral("id")) || key.contains(QStringLiteral("identity")) || key.contains(QStringLiteral("key")) || key.contains(QStringLiteral("fingerprint")));
    return key.contains(QStringLiteral("secret")) || key.contains(QStringLiteral("password")) || key.contains(QStringLiteral("credential")) || key == QStringLiteral("auth") || key.contains(QStringLiteral("authorization")) || key == QStringLiteral("token") || key.endsWith(QStringLiteral("token")) || key == QStringLiteral("apikey") || key.endsWith(QStringLiteral("apikey")) || key == QStringLiteral("privatekey") || key.endsWith(QStringLiteral("privatekey")) || deviceIdentity;
}

QJsonValue portableValue(const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject portable;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!isSensitiveField(it.key())) portable.insert(it.key(), portableValue(it.value()));
        }
        return portable;
    }
    if (value.isArray()) {
        QJsonArray portable;
        for (const auto &entry : value.toArray()) portable.append(portableValue(entry));
        return portable;
    }
    return value;
}
void limitActionText(QJsonObject &action)
{
    action.insert(QStringLiteral("name"), action.value(QStringLiteral("name")).toString().left(ConfigStore::MaxNameLength));
    action.insert(QStringLiteral("icon"), action.value(QStringLiteral("icon")).toString().left(ConfigStore::MaxTextLength));
    const auto payload = action.value(QStringLiteral("payload"));
    action.insert(QStringLiteral("payload"), payload.isObject() ? cutText(payload, ConfigStore::MaxTextLength).toObject() : QJsonObject{});
}
} // namespace

ConfigStore::ConfigStore(QString path)
    : m_path(path.isEmpty() ? WheelPaths::settingsFile() : std::move(path)), m_legacyPath(path.isEmpty() ? WheelPaths::readableSettingsFile() : QString())
{
    if (m_legacyPath == m_path) m_legacyPath.clear();
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
         QStringLiteral("calculator"), QStringLiteral("settings")}};
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
                                                    {QStringLiteral("showNotifications"), true},
                                                    {QStringLiteral("theme"), QStringLiteral("system")}}},
            {QStringLiteral("link"), QJsonObject{{QStringLiteral("enabled"), true},
                                                 {QStringLiteral("disabledPeers"), QJsonArray{}}}},
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
                                                       {QStringLiteral("reduceMotion"), false},
                                                       {QStringLiteral("customAccent"), false},
                                                       {QStringLiteral("accentColor"), QStringLiteral("#f59e0b")}}},
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
            {QStringLiteral("decks"), QJsonArray{QJsonObject{{QStringLiteral("id"), freshId()}, {QStringLiteral("name"), QStringLiteral("Applications")}, {QStringLiteral("actions"), actions}}}}};
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
        const QJsonObject oldStyle{{"radius", 164}, {"centerRadius", 54}, {"cardSize", 82}, {"gap", 10}, {"iconSize", 30}, {"opacity", 0.88}, {"selectedScale", 1.10}};
        const auto newStyle = defaults().value(QStringLiteral("appearance")).toObject();
        for (auto it = oldStyle.begin(); it != oldStyle.end(); ++it)
            if (appearance.value(it.key()) == it.value()) appearance.insert(it.key(), newStyle.value(it.key()));
        config.insert(QStringLiteral("appearance"), appearance);
    }
    {
        // Before 0.5 the ring used appearance.accentColor whenever it was set
        // (only a hand-edited or imported file had one): keep that colour.
        auto appearance = config.value(QStringLiteral("appearance")).toObject();
        if (!appearance.contains(QStringLiteral("customAccent")))
            appearance.insert(QStringLiteral("customAccent"),
                              QColor::isValidColorName(appearance.value(QStringLiteral("accentColor")).toString()));
        config.insert(QStringLiteral("appearance"), appearance);
    }
    const auto baseline = defaults();
    for (const auto &section : {"general", "link", "trigger", "appearance", "behaviour"}) {
        const auto key = QString::fromLatin1(section);
        const auto base = baseline.value(key).toObject();
        config.insert(key, repairSection(key, base, mergeDefaults(base, config.value(key).toObject())));
    }
    {
        auto general = config.value(QStringLiteral("general")).toObject();
        const QString theme = general.value(QStringLiteral("theme")).toString();
        if (theme != QStringLiteral("light") && theme != QStringLiteral("dark")) general.insert(QStringLiteral("theme"), QStringLiteral("system"));
        config.insert(QStringLiteral("general"), general);
        auto appearance = config.value(QStringLiteral("appearance")).toObject();
        if (!QColor::isValidColorName(appearance.value(QStringLiteral("accentColor")).toString()))
            appearance.insert(QStringLiteral("accentColor"), QStringLiteral("#f59e0b"));
        config.insert(QStringLiteral("appearance"), appearance);
    }

    QJsonArray decks = config.value(QStringLiteral("decks")).toArray();
    if (decks.isEmpty()) decks = baseline.value(QStringLiteral("decks")).toArray();
    QJsonArray normalizedDecks;
    QSet<QString> ids;
    for (const auto &deckValue : decks) {
        if (!deckValue.isObject()) continue;
        if (normalizedDecks.size() == MaxDecks) break;
        QJsonObject deck = deckValue.toObject();
        QString deckId = deck.value(QStringLiteral("id")).toString();
        if (deckId.isEmpty() || ids.contains(deckId)) deckId = freshId();
        ids.insert(deckId);
        deck.insert(QStringLiteral("id"), deckId);
        const QString deckName = deck.value(QStringLiteral("name")).toString().left(MaxNameLength);
        deck.insert(QStringLiteral("name"), deckName.isEmpty() ? QStringLiteral("Deck") : deckName);
        QJsonArray actions = deck.value(QStringLiteral("actions")).toArray();
        if (actions.isEmpty() && deck.value(QStringLiteral("items")).isArray())
            actions = deck.value(QStringLiteral("items")).toArray();
        QJsonArray normalizedActions;
        for (const auto &value : actions) {
            if (!value.isObject()) continue;
            auto action = mergeDefaults(emptyAction(), migrateAction(value.toObject()));
            QString actionId = action.value(QStringLiteral("id")).toString();
            if (actionId.isEmpty() || ids.contains(actionId)) actionId = freshId();
            ids.insert(actionId);
            action.insert(QStringLiteral("id"), actionId);
            limitActionText(action);
            normalizedActions.append(action);
            if (normalizedActions.size() == MaxDeckActions) break;
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
            auto action = mergeDefaults(emptyAction(), migrateAction(value.toObject()));
            QString id = action.value("id").toString();
            if (id.isEmpty() || ids.contains(id)) id = freshId();
            ids.insert(id);
            action.insert("id", id);
            limitActionText(action);
            actions.append(action);
            if (actions.size() == MaxGestureActions) break;
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
    // The folder move failed or hasn't run: start from a copy of the old file and
    // leave the original where it is.
    if (!QFileInfo::exists(m_path) && !m_legacyPath.isEmpty() && QFileInfo::exists(m_legacyPath)) {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        QFile::copy(m_legacyPath, m_path);
    }
    if (!QFileInfo::exists(m_path)) {
        auto config = defaults(applications);
        save(config, error);
        return config;
    }
    QString readError;
    const auto original = readJson(m_path, &readError, MaxSettingsBytes);
    if (original.isEmpty() && readError.isEmpty()) readError = QStringLiteral("Configuration is empty");
    auto config = original.isEmpty() ? QJsonObject{} : normalize(original, &readError);
    if (!config.isEmpty() && config != original) {
        // Persist migrations and repaired IDs. Otherwise every start would run
        // the migration again and give ID-less entries different IDs.
        QString saveError;
        if (!writeJson(config, m_path, &saveError) && error)
            *error = QStringLiteral("The configuration was upgraded but could not be saved: %1").arg(saveError);
    }
    if (config.isEmpty()) {
        const auto backup = m_path + QStringLiteral(".invalid-") + freshId() + QStringLiteral(".json");
        const bool copied = QFile::copy(m_path, backup);
        if (copied) pruneCopies(m_path, QStringLiteral(".invalid-*.json"), backup);
        if (!copied) {
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

bool ConfigStore::writeJson(const QJsonObject &config, const QString &path, QString *error, bool privateFile)
{
#ifndef Q_OS_UNIX
    Q_UNUSED(privateFile);
#endif
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("Could not create configuration directory");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
#ifdef Q_OS_UNIX
    if (privateFile && !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
#endif
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
    return writeJson(portable(config), path, error, true);
}

bool ConfigStore::importFrom(const QString &path, QJsonObject *config, QString *error) const
{
    QString readError;
    const auto raw = readJson(path, &readError, MaxImportBytes);
    if (raw.isEmpty()) {
        if (error) *error = readError.isEmpty() ? QStringLiteral("This is not a settings file: it is empty.") : readError;
        return false;
    }
    if (!validateImport(raw, &readError)) {
        if (error) *error = readError;
        return false;
    }
    // Validate before sanitizing: unknown/newer schemas must never be made
    // valid merely by omitting their schemaVersion field.
    const auto validated = normalize(raw, &readError);
    const auto imported = validated.isEmpty() ? QJsonObject{} : normalize(portable(validated), &readError);
    if (imported.isEmpty()) {
        if (error) *error = readError;
        return false;
    }
    *config = imported;
    return true;
}

bool ConfigStore::validateImport(const QJsonObject &raw, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error) *error = why;
        return false;
    };
    static const QStringList known{QStringLiteral("schemaVersion"), QStringLiteral("general"), QStringLiteral("link"),
                                   QStringLiteral("trigger"), QStringLiteral("appearance"), QStringLiteral("behaviour"),
                                   QStringLiteral("decks"), QStringLiteral("pages"), QStringLiteral("centerGestures")};
    if (std::none_of(known.begin(), known.end(), [&](const QString &key) { return raw.contains(key); }))
        return fail(QStringLiteral("This is not an Arcade Wheel settings file: it has no settings or decks."));
    if (raw.contains(QStringLiteral("schemaVersion"))) {
        const auto version = raw.value(QStringLiteral("schemaVersion"));
        const double number = version.toDouble(-1);
        if (!version.isDouble() || number != std::floor(number) || number < 0)
            return fail(QStringLiteral("The settings file's schema version must be a whole number."));
        if (number > SchemaVersion) return fail(QStringLiteral("Unsupported configuration schema version %1").arg(static_cast<qint64>(number)));
    }
    for (const auto &section : {QStringLiteral("general"), QStringLiteral("link"), QStringLiteral("trigger"), QStringLiteral("appearance"),
                                QStringLiteral("behaviour"), QStringLiteral("centerGestures")})
        if (raw.contains(section) && !raw.value(section).isObject()) return fail(QStringLiteral("\"%1\" must be an object.").arg(section));
    const QString deckKey = raw.contains(QStringLiteral("decks")) ? QStringLiteral("decks") : QStringLiteral("pages");
    const auto list = raw.value(deckKey);
    if (!list.isArray()) return fail(QStringLiteral("The settings file has no list of decks, so nothing was imported."));
    const auto decks = list.toArray();
    if (decks.isEmpty()) return fail(QStringLiteral("The settings file's list of decks is empty, so nothing was imported."));
    if (decks.size() > MaxDecks) return fail(QStringLiteral("The settings file has %1 decks, but at most %2 are allowed.").arg(decks.size()).arg(MaxDecks));
    QSet<QString> ids;
    for (int i = 0; i < decks.size(); ++i) {
        const QString where = QStringLiteral("Deck %1").arg(i + 1);
        if (!decks.at(i).isObject()) return fail(where + QStringLiteral(": a deck must be an object."));
        const auto deck = decks.at(i).toObject();
        for (const auto &key : {QStringLiteral("id"), QStringLiteral("name")}) {
            const auto field = deck.value(key);
            if (field.isUndefined()) continue;
            if (!field.isString()) return fail(where + QStringLiteral(": %1 must be text.").arg(key));
            if (field.toString().size() > (key == QStringLiteral("name") ? MaxNameLength : MaxTextLength))
                return fail(where + QStringLiteral(": %1 is too long (at most %2 characters).").arg(key).arg(key == QStringLiteral("name") ? MaxNameLength : MaxTextLength));
        }
        const QString id = deck.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            if (ids.contains(id)) return fail(where + QStringLiteral(": this id is already used by another deck or action."));
            ids.insert(id);
        }
        const auto actions = deck.contains(QStringLiteral("actions")) ? deck.value(QStringLiteral("actions")) : deck.value(QStringLiteral("items"));
        if (actions.isUndefined()) continue;
        if (!validateActionList(actions, MaxDeckActions, where, &ids, error)) return false;
    }
    const auto gestures = raw.value(QStringLiteral("centerGestures")).toObject();
    for (const auto &key : {QStringLiteral("singleClick"), QStringLiteral("doubleClick"), QStringLiteral("tripleClick"), QStringLiteral("longPress")}) {
        if (!gestures.contains(key)) continue;
        if (!gestures.value(key).isObject()) return fail(QStringLiteral("Centre gesture \"%1\" must be an object.").arg(key));
        const auto actions = gestures.value(key).toObject().value(QStringLiteral("actions"));
        if (!actions.isUndefined() && !validateActionList(actions, MaxGestureActions, QStringLiteral("Centre gesture \"%1\"").arg(key), &ids, error)) return false;
    }
    return true;
}

QJsonObject ConfigStore::portable(const QJsonObject &config)
{
    const auto select = [](const QJsonObject &object, const QStringList &keys) {
        QJsonObject out;
        for (const auto &key : keys)
            if (object.contains(key)) out.insert(key, portableValue(object.value(key)));
        return out;
    };
    const auto action = [&](const QJsonObject &object) {
        auto out = select(object, {"id", "type", "name", "icon"});
        out.insert("payload", select(object.value("payload").toObject(),
                                     {"desktopId", "command", "workingDirectory", "url", "path", "id", "app", "action",
                                      "version", "input", "options", "preset", "providerId", "toolId"}));
        return out;
    };
    const auto actions = [&](const QJsonArray &array) {
        QJsonArray out;
        for (const auto &entry : array)
            if (entry.isObject()) out.append(action(entry.toObject()));
        return out;
    };
    QJsonObject out{{"schemaVersion", config.value("schemaVersion")}};
    const auto baseline = defaults();
    for (const auto &key : {"general", "link", "trigger", "appearance", "behaviour"}) {
        QStringList keys = baseline.value(key).toObject().keys();
        if (QString::fromLatin1(key) == u"trigger") keys << "overrideConflict";
        out.insert(key, select(config.value(key).toObject(), keys));
    }
    QJsonArray decks;
    for (const auto &entry : config.value("decks").toArray()) {
        auto deck = select(entry.toObject(), {"id", "name"});
        deck.insert("actions", actions(entry.toObject().value("actions").toArray()));
        decks.append(deck);
    }
    out.insert("decks", decks);
    const auto source = config.value("centerGestures").toObject();
    auto gestures = select(source, {"clickIntervalMs", "longPressMs"});
    for (const auto &key : {"singleClick", "doubleClick", "tripleClick", "longPress"}) {
        auto group = select(source.value(key).toObject(), {"name", "enabled"});
        group.insert("actions", actions(source.value(key).toObject().value("actions").toArray()));
        gestures.insert(key, group);
    }
    out.insert("centerGestures", gestures);
    return out;
}

bool ConfigStore::backupCurrent(QString *backupPath, QString *error) const
{
    QFile source(m_path);
    if (!source.exists()) {
        if (backupPath) backupPath->clear();
        return true;
    }
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) *error = source.errorString();
        return false;
    }
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'"));
    const QString destination = m_path + QStringLiteral(".backup-%1-%2.json").arg(stamp, freshId());
    QSaveFile backup(destination);
    if (!backup.open(QIODevice::WriteOnly)) {
        if (error) *error = backup.errorString();
        return false;
    }
#ifdef Q_OS_UNIX
    if (!backup.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = backup.errorString();
        backup.cancelWriting();
        return false;
    }
#endif
    const QByteArray data = source.readAll();
    if (backup.write(data) != data.size() || !backup.commit()) {
        if (error) *error = backup.errorString();
        return false;
    }
    pruneCopies(m_path, QStringLiteral(".backup-*.json"), destination);
    if (backupPath) *backupPath = destination;
    return true;
}

void ConfigStore::pruneCopies(const QString &settingsPath, const QString &suffixPattern, const QString &keep)
{
    const QFileInfo info(settingsPath);
    QDir dir(info.absolutePath());
    const auto copies = dir.entryInfoList({info.fileName() + suffixPattern}, QDir::Files, QDir::Time);
    int remaining = KeptBackups - 1; // the copy just written takes the other one
    for (const auto &copy : copies) {
        if (copy.absoluteFilePath() == QFileInfo(keep).absoluteFilePath()) continue;
        if (remaining > 0) {
            --remaining;
            continue;
        }
        QFile::remove(copy.absoluteFilePath());
    }
}
