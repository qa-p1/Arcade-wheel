#pragma once
#include "link/ArcadeLink.h"

// App-only compatibility metadata; the vendored frozen Link module is unchanged.
namespace ShelfMetadata {
inline const QString Shelf = QStringLiteral("arcade.shelf");
inline QStringList apps() { auto known = ArcadeLink::Ids::apps(); if (!known.contains(Shelf)) known.append(Shelf); return known; }
inline QString appName(const QString &id) { return id == Shelf ? QStringLiteral("Arcade Shelf") : ArcadeLink::appName(id); }
inline QString appPitch(const QString &id) { return id == Shelf ? QStringLiteral("Collect, organize and transfer desktop content.") : ArcadeLink::appPitch(id); }
inline QString releasesUrl(const QString &id) { return id == Shelf ? QStringLiteral("https://github.com/qa-p1/Arcade-Shelf/releases") : ArcadeLink::releasesUrl(id); }
}
