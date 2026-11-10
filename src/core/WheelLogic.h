#pragma once

#include <QList>
#include <QPointF>
#include <QRect>

namespace WheelLogic {
// Slot zero is at 12 o'clock; remaining slots proceed clockwise.
int selectedSlot(QPointF pointer, QPointF origin, int count, qreal deadZone);
int scrollIndex(int current, int count, int steps, bool wrap, bool reverse);
// The monitor the wheel opens on: the one holding the pointer. A pointer
// between monitors or outside every one (a layout with gaps, or a monitor that
// just went away) goes to the nearest. -1 when there are no monitors.
int screenIndexFor(const QList<QRect> &screens, QPointF pointer);
// The wheel's centre in layout coordinates: the middle of that monitor.
QPointF centreOf(const QRect &screen);
} // namespace WheelLogic
