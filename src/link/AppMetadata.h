#pragma once
#include "link/ArcadeLink.h"

// The Arcade apps Wheel knows: the shared app list Arcade Tools keeps (Link SPEC §3.1), so a new Arcade app appears
// without a Wheel update, falling back to the released Link module's values when there is no list.
namespace AppMetadata {
inline ArcadeLink::Catalog catalog() { return ArcadeLink::Catalog::load(ArcadeLink::Locations::discover()); }
inline QStringList apps() { return catalog().appIds(); }
inline QString appName(const QString &id) { return catalog().name(id); }
inline QString appPitch(const QString &id) { return catalog().tagline(id); }
inline QString releasesUrl(const QString &id) { return catalog().releasesUrl(id); }
} // namespace AppMetadata
