#pragma once
#include "link/ArcadeLink.h"

// App-only compatibility metadata for apps newer than the vendored, frozen
// Link module (v0.1.0), which stays unchanged. Drop an entry once a reviewed
// Link pin ships it.
namespace AppMetadata {
inline const QString Shelf = QStringLiteral("arcade.shelf");
inline const QString Find = QStringLiteral("arcade.find");
inline QStringList apps()
{
    auto known = ArcadeLink::Ids::apps();
    for (const auto &id : {Shelf, Find}) if (!known.contains(id)) known.append(id);
    return known;
}
inline QString appName(const QString &id)
{
    if (id == Shelf) return QStringLiteral("Arcade Shelf");
    if (id == Find) return QStringLiteral("Arcade Find");
    return ArcadeLink::appName(id);
}
inline QString appPitch(const QString &id)
{
    if (id == Shelf) return QStringLiteral("Collect, organize and transfer desktop content.");
    if (id == Find) return QStringLiteral("Find files and folders instantly.");
    return ArcadeLink::appPitch(id);
}
inline QString releasesUrl(const QString &id)
{
    if (id == Shelf) return QStringLiteral("https://github.com/qa-p1/Arcade-Shelf/releases");
    if (id == Find) return QStringLiteral("https://github.com/qa-p1/Arcade-Find/releases");
    return ArcadeLink::releasesUrl(id);
}
}
