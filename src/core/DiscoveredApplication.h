#pragma once

#include <QString>

// An application the desktop knows about, as the wheel offers it. Platform backends find them; configuration and
// action routing only read them, so they don't depend on the platform interface.
struct DiscoveredApplication {
    QString id;
    QString name;
    QString icon;
    QString description;
    QString launchTarget;
};
