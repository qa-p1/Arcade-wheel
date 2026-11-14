#include "core/WheelLogic.h"

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <numbers>

int WheelLogic::selectedSlot(QPointF pointer, QPointF origin, int count, qreal deadZone)
{
    if (count <= 0) return -1;
    const QPointF delta = pointer - origin;
    if (std::hypot(delta.x(), delta.y()) <= std::max<qreal>(0, deadZone)) return -1;
    constexpr qreal pi = std::numbers::pi_v<qreal>;
    const qreal step = 2 * pi / count;
    qreal angle = std::atan2(delta.y(), delta.x()) + pi / 2 + step / 2;
    angle = std::fmod(angle + 2 * pi, 2 * pi);
    return static_cast<int>(std::floor(angle / step)) % count;
}

int WheelLogic::scrollIndex(int current, int count, int steps, bool wrap, bool reverse)
{
    if (count <= 0) return 0;
    const int target = current + (reverse ? -steps : steps);
    if (wrap) return (target % count + count) % count;
    return std::clamp(target, 0, count - 1);
}

int WheelLogic::screenIndexFor(const QList<QRect> &screens, QPointF pointer)
{
    int nearest = -1;
    qreal nearestDistance = 0;
    for (int i = 0; i < screens.size(); ++i) {
        const QRect &screen = screens.at(i);
        // A monitor owns its left and top edges, like QRect::contains for pixels.
        if (pointer.x() >= screen.left() && pointer.x() < screen.left() + screen.width() &&
            pointer.y() >= screen.top() && pointer.y() < screen.top() + screen.height())
            return i;
        const qreal dx = std::max<qreal>({screen.left() - pointer.x(), 0, pointer.x() - (screen.left() + screen.width())});
        const qreal dy = std::max<qreal>({screen.top() - pointer.y(), 0, pointer.y() - (screen.top() + screen.height())});
        const qreal distance = std::hypot(dx, dy);
        if (nearest < 0 || distance < nearestDistance) {
            nearest = i;
            nearestDistance = distance;
        }
    }
    return nearest;
}

QPointF WheelLogic::centreOf(const QRect &screen)
{
    return QPointF(screen.left() + screen.width() / 2.0, screen.top() + screen.height() / 2.0);
}
