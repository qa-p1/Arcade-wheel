#pragma once

#include <QString>

// RT-4: a fatal signal writes a local report under <data>/crashes/ (newest 20
// kept) and then dies the way it would have. Nothing is sent anywhere. Linux
// only; elsewhere install() does nothing.
namespace CrashReport {

constexpr int KeptReports = 20;

// Installs the handlers. `folder` is arcade/wheel/crashes; it is created with
// the first report, not here.
void install(const QString &folder, const QString &version);

// Deletes all but the newest `keep` reports in `folder`. Called by install().
void prune(const QString &folder, int keep = KeptReports);

} // namespace CrashReport
