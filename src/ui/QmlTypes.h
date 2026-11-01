#pragma once

#include "core/WheelController.h"

#include <QtQml/qqmlregistration.h>

// WheelController lives in arcade_core (the tests link it without QML); this
// registers it for Wheel's QML as a typed, uncreatable type.
struct WheelControllerForeign {
    Q_GADGET
    QML_FOREIGN(WheelController)
    QML_NAMED_ELEMENT(WheelController)
    QML_UNCREATABLE("Created by main.cpp")
};
