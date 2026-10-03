#pragma once

#include <QPointF>

namespace WheelLogic {
// Slot zero is at 12 o'clock; remaining slots proceed clockwise.
int selectedSlot(QPointF pointer, QPointF origin, int count, qreal deadZone);
int scrollIndex(int current, int count, int steps, bool wrap, bool reverse);
}
