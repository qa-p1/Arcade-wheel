#pragma once
#include "link/ArcadeLink.h"

// The Arcade apps Wheel knows, from the vendored Link module (v0.2.0 lists
// Shelf and Find). One place for callers to ask.
namespace AppMetadata {
inline const QString Shelf = ArcadeLink::Ids::Shelf;
inline const QString Find = ArcadeLink::Ids::Find;
inline QStringList apps() { return ArcadeLink::Ids::apps(); }
inline QString appName(const QString &id) { return ArcadeLink::appName(id); }
inline QString appPitch(const QString &id) { return ArcadeLink::appPitch(id); }
inline QString releasesUrl(const QString &id) { return ArcadeLink::releasesUrl(id); }
}
