#pragma once

#include <QString>
#include <QStringList>

// Which starts run from the unpacked copy of the AppImage (U1, RT-7). main()
// asks before it creates the application object, so this takes the plain
// argument list (without the program name) and touches nothing else.
namespace ResidentStart {

// Whether a start with these arguments becomes, or keeps, the resident Wheel
// when none is running: Settings (also the start with no arguments), the
// background start and the successor a restart starts. Everything else is a
// one-shot: commands that only print or change files (--status, --version,
// --install ...), commands that only talk to the running Wheel (--show,
// --toggle, --press, --hide, --quit ...; with none running they would start
// one, but they are how a running Wheel is driven, not how it is started),
// --smoke-test, whose exit code a relaunch without wait would drop, and
// anything unknown, which prints usage and exits 2.
inline bool keepsResident(const QStringList &arguments)
{
    if (arguments.isEmpty()) return true;
    if (arguments.size() != 1) return false;
    const QString &command = arguments.first();
    return command == u"--settings" || command == u"--background" || command == u"--restarting";
}

} // namespace ResidentStart
