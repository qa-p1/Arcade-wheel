#include "core/IconTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace IconTheme {
namespace {

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && (value.startsWith(u'"') || value.startsWith(u'\''))
        && value.endsWith(value.front()))
        value = value.mid(1, value.size() - 2).trimmed();
    return value;
}

QString usable(const QString &theme, const QStringList &searchPaths)
{
    const QString name = unquote(theme);
    if (name.isEmpty() || name.compare(QStringLiteral("hicolor"), Qt::CaseInsensitive) == 0) return {};
    return isInstalled(name, searchPaths) ? name : QString();
}

} // namespace

QString readIniValue(const QString &path, const QString &section, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    bool inSection = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine(4096)).trimmed();
        if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u';')) continue;
        if (line.startsWith(u'[')) {
            inSection = line == u'[' + section + u']';
            continue;
        }
        if (!inSection) continue;
        const qsizetype equals = line.indexOf(u'=');
        if (equals > 0 && line.left(equals).trimmed() == key) return unquote(line.mid(equals + 1));
    }
    return {};
}

QString readQtEngine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonObject root = QJsonDocument::fromJson(file.read(1 << 20)).object();
    const QString nested = root.value(QStringLiteral("theme")).toObject().value(QStringLiteral("iconTheme")).toString();
    return (nested.isEmpty() ? root.value(QStringLiteral("iconTheme")).toString() : nested).trimmed();
}

QString parseGsettings(const QString &output)
{
    const QString value = unquote(output.section(u'\n', 0, 0));
    return value == u"@as []" ? QString() : value;
}

bool isInstalled(const QString &theme, const QStringList &searchPaths)
{
    if (theme.isEmpty() || theme.contains(u'/') || theme == u"..") return false;
    for (const QString &base : searchPaths) {
        if (base.startsWith(u':')) continue; // resources never hold a user theme
        if (QFileInfo::exists(QDir(base).filePath(theme + QStringLiteral("/index.theme")))) return true;
    }
    return false;
}

QString detect(const Sources &sources)
{
    const QDir config(sources.configHome);
    const auto qtengine = [&] { return readQtEngine(config.filePath(QStringLiteral("qtengine/config.json"))); };
    const auto ct = [&](const QString &name) {
        return readIniValue(config.filePath(name + u'/' + name + QStringLiteral(".conf")),
                            QStringLiteral("Appearance"), QStringLiteral("icon_theme"));
    };
    const auto kde = [&] {
        return readIniValue(config.filePath(QStringLiteral("kdeglobals")), QStringLiteral("Icons"), QStringLiteral("Theme"));
    };
    const auto gtk = [&] {
        return readIniValue(config.filePath(QStringLiteral("gtk-3.0/settings.ini")),
                            QStringLiteral("Settings"), QStringLiteral("gtk-icon-theme-name"));
    };

    // 1. The platform theme the user asked Qt to load (its plugin may be
    //    unavailable to this build). 2. Without one, qtengine's config.
    const QString platform = sources.platformTheme.section(u':', 0, 0).trimmed().toLower();
    QString found;
    if (platform.isEmpty()) found = usable(qtengine(), sources.searchPaths);
    else if (platform == u"qtengine") found = usable(qtengine(), sources.searchPaths);
    else if (platform == u"qt6ct" || platform == u"qt5ct") found = usable(ct(platform), sources.searchPaths);
    else if (platform == u"kde") found = usable(kde(), sources.searchPaths);
    if (!found.isEmpty()) return found;
    // 3. GNOME settings, 4. KDE, 5. GTK 3.
    if (sources.gsettings) found = usable(parseGsettings(sources.gsettings()), sources.searchPaths);
    if (found.isEmpty()) found = usable(kde(), sources.searchPaths);
    if (found.isEmpty()) found = usable(gtk(), sources.searchPaths);
    return found;
}

Sources systemSources()
{
    Sources sources;
    sources.platformTheme = qEnvironmentVariable("QT_QPA_PLATFORMTHEME");
    sources.configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    sources.searchPaths = QIcon::themeSearchPaths();
    sources.gsettings = [] {
        const QString program = QStandardPaths::findExecutable(QStringLiteral("gsettings"));
        if (program.isEmpty()) return QString();
        QProcess process;
        process.start(program, {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("icon-theme")});
        if (!process.waitForFinished(400)) {
            process.kill();
            process.waitForFinished(100);
            return QString();
        }
        return process.exitCode() == 0 ? QString::fromUtf8(process.readAllStandardOutput()) : QString();
    };
    return sources;
}

QString applyUserTheme()
{
    const QString current = QIcon::themeName();
    if (!current.isEmpty() && current.compare(QStringLiteral("hicolor"), Qt::CaseInsensitive) != 0) return {};
    const QString theme = detect(systemSources());
    if (theme.isEmpty()) return {};
    QIcon::setThemeName(theme);
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    return theme;
}

QStringList withNewSearchPaths(const QStringList &current, const QStringList &candidates)
{
    QStringList added;
    for (const QString &path : candidates) {
        const QString clean = QDir::cleanPath(path);
        if (!current.contains(clean) && !added.contains(clean) && QFileInfo(clean).isDir()) added.append(clean);
    }
    return added + current;
}

void reloadSearchPaths()
{
    QStringList candidates;
#ifdef Q_OS_LINUX
    candidates.append(QDir::homePath() + QStringLiteral("/.icons"));
    for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        candidates.append(data + QStringLiteral("/icons"));
#endif
    // Setting the paths, even unchanged, also forgets failed lookups.
    QIcon::setThemeSearchPaths(withNewSearchPaths(QIcon::themeSearchPaths(), candidates));
}

} // namespace IconTheme
