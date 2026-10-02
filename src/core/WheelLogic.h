#pragma once

#include <QPointF>
#include <QRectF>

namespace WheelLogic {
// Slot zero is at 12 o'clock; remaining slots proceed clockwise.
int selectedSlot(QPointF pointer, QPointF origin, int count, qreal deadZone);
int scrollIndex(int current, int count, int steps, bool wrap, bool reverse);
QPointF clampCenter(QPointF desired, QRectF screen, qreal extent);
}
