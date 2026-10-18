#pragma once

#include <QString>
#include <QStringList>
#include <functional>

// Finds the user's icon theme when Qt could not load the desktop's platform
// theme plugin (for example inside the AppImage, which cannot load a host
// plugin such as qtengine or qt6ct) and fell back to "hicolor".
namespace IconTheme {

struct Sources {
    QString platformTheme;               // QT_QPA_PLATFORMTHEME
    QString configHome;                  // XDG_CONFIG_HOME or ~/.config
    QStringList searchPaths;             // QIcon::themeSearchPaths()
    std::function<QString()> gsettings;  // org.gnome.desktop.interface icon-theme, raw output
};

// Small readers, exposed for tests. Each returns an empty string when the
// file or key is missing.
QString readIniValue(const QString &path, const QString &section, const QString &key);
QString readQtEngine(const QString &path);
QString parseGsettings(const QString &output);
bool isInstalled(const QString &theme, const QStringList &searchPaths);

// The first installed theme from the user's settings, or an empty string.
QString detect(const Sources &sources);

// Sources for the current process. gsettings runs only when it is needed and
// is given a short timeout.
Sources systemSources();

// Applies the detected theme when Qt's current theme is empty or "hicolor".
// Returns the applied theme name, or an empty string when nothing changed.
QString applyUserTheme();

// Qt computes its icon search paths once, leaving out folders that did not
// exist yet (the first app installed into a fresh home creates
// ~/.local/share/icons), and it caches failed lookups. Returns the current
// paths with the new existing candidates in front.
QStringList withNewSearchPaths(const QStringList &current, const QStringList &candidates);

// Adds icon folders created since start-up and drops cached lookups, so
// newly installed apps get their own icons without a restart.
void reloadSearchPaths();

} // namespace IconTheme
