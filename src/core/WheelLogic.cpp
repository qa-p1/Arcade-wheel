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

QPointF WheelLogic::clampCenter(QPointF desired, QRectF screen, qreal extent)
{
    const qreal usableExtent = std::min({extent, screen.width() / 2, screen.height() / 2});
    return {std::clamp(desired.x(), screen.left() + usableExtent, screen.right() - usableExtent),
            std::clamp(desired.y(), screen.top() + usableExtent, screen.bottom() - usableExtent)};
}
