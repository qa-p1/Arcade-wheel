#include "core/WheelLogic.h"

#include <QtTest>
#include <cmath>

class WheelLogicTest final : public QObject {
    Q_OBJECT

private slots:
    void angularSelection_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("slot");

        for (const int count : {4, 6, 8}) {
            for (int slot = 0; slot < count; ++slot) {
                const QByteArray label = QByteArray("count_") + QByteArray::number(count)
                                         + "_slot_" + QByteArray::number(slot);
                QTest::newRow(label.constData()) << count << slot;
            }
        }
    }

    void angularSelection()
    {
        QFETCH(int, count);
        QFETCH(int, slot);

        const QPointF origin(300.0, -120.0);
        const qreal angle = -M_PI_2 + slot * (2.0 * M_PI / count);
        const QPointF pointer = origin + QPointF(std::cos(angle), std::sin(angle)) * 150.0;
        QCOMPARE(WheelLogic::selectedSlot(pointer, origin, count, 48.0), slot);
    }

    void angularBoundariesAndInvalidCounts()
    {
        const QPointF origin(0.0, 0.0);
        const qreal upperRightAngle = -M_PI_2 + 2.0 * M_PI / 6.0;
        const QPointF upperRight(100.0 * std::cos(upperRightAngle), 100.0 * std::sin(upperRightAngle));
        QCOMPARE(WheelLogic::selectedSlot(upperRight, origin, 6, 10.0), 1);
        QCOMPARE(WheelLogic::selectedSlot(QPointF(10.0, 0.0), origin, 0, 0.0), -1);
        QCOMPARE(WheelLogic::selectedSlot(QPointF(10.0, 0.0), origin, -3, 0.0), -1);
    }

    void deadZoneIncludesBoundary()
    {
        const QPointF origin(-80.0, 45.0);
        QCOMPARE(WheelLogic::selectedSlot(origin, origin, 6, 32.0), -1);
        QCOMPARE(WheelLogic::selectedSlot(origin + QPointF(32.0, 0.0), origin, 6, 32.0), -1);
        const qreal slotOneAngle = -M_PI_2 + 2.0 * M_PI / 6.0;
        const QPointF justOutside = origin + QPointF(32.01 * std::cos(slotOneAngle),
                                                    32.01 * std::sin(slotOneAngle));
        QCOMPARE(WheelLogic::selectedSlot(justOutside, origin, 6, 32.0), 1);
        // Negative dead-zone input is clamped to zero.
        QCOMPARE(WheelLogic::selectedSlot(origin + QPointF(0.0, -1.0), origin, 6, -5.0), 0);
    }

    void deckScrolling_data()
    {
        QTest::addColumn<int>("current");
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("steps");
        QTest::addColumn<bool>("wrap");
        QTest::addColumn<bool>("reverse");
        QTest::addColumn<int>("expected");

        QTest::newRow("forward-wrap") << 3 << 4 << 1 << true << false << 0;
        QTest::newRow("backward-wrap") << 0 << 4 << -1 << true << false << 3;
        QTest::newRow("reverse-forward") << 1 << 4 << 1 << true << true << 0;
        QTest::newRow("reverse-backward") << 1 << 4 << -1 << true << true << 2;
        QTest::newRow("multiple-steps") << 0 << 5 << 12 << true << false << 2;
        QTest::newRow("clamp-upper") << 3 << 4 << 1 << false << false << 3;
        QTest::newRow("clamp-lower-reverse") << 0 << 4 << 1 << false << true << 0;
        QTest::newRow("no-decks") << 4 << 0 << 1 << true << false << 0;
    }

    void deckScrolling()
    {
        QFETCH(int, current);
        QFETCH(int, count);
        QFETCH(int, steps);
        QFETCH(bool, wrap);
        QFETCH(bool, reverse);
        QFETCH(int, expected);
        QCOMPARE(WheelLogic::scrollIndex(current, count, steps, wrap, reverse), expected);
    }

    void clampCenter_data()
    {
        QTest::addColumn<QPointF>("desired");
        QTest::addColumn<QRectF>("screen");
        QTest::addColumn<qreal>("extent");
        QTest::addColumn<QPointF>("expected");

        const QRectF leftMonitor(-1920.0, -1080.0, 1920.0, 1080.0);
        QTest::newRow("left-edge-negative-monitor")
            << QPointF(-1910.0, -500.0) << leftMonitor << 200.0 << QPointF(-1720.0, -500.0);
        QTest::newRow("right-edge-negative-monitor")
            << QPointF(-5.0, -500.0) << leftMonitor << 200.0 << QPointF(-200.0, -500.0);
        QTest::newRow("top-edge-negative-monitor")
            << QPointF(-900.0, -1075.0) << leftMonitor << 200.0 << QPointF(-900.0, -880.0);
        QTest::newRow("bottom-edge-negative-monitor")
            << QPointF(-900.0, -2.0) << leftMonitor << 200.0 << QPointF(-900.0, -200.0);
        QTest::newRow("top-left-corner")
            << QPointF(-1919.0, -1079.0) << leftMonitor << 200.0 << QPointF(-1720.0, -880.0);
        QTest::newRow("bottom-right-corner")
            << QPointF(-1.0, -1.0) << leftMonitor << 200.0 << QPointF(-200.0, -200.0);
        QTest::newRow("extent-larger-than-screen")
            << QPointF(8.0, 9.0) << QRectF(-50.0, 100.0, 100.0, 60.0)
            << 200.0 << QPointF(8.0, 130.0);
        QTest::newRow("center-unchanged")
            << QPointF(-960.0, -540.0) << leftMonitor << 200.0 << QPointF(-960.0, -540.0);
    }

    void clampCenter()
    {
        QFETCH(QPointF, desired);
        QFETCH(QRectF, screen);
        QFETCH(qreal, extent);
        QFETCH(QPointF, expected);

        const QPointF result = WheelLogic::clampCenter(desired, screen, extent);
        QCOMPARE(result, expected);
        const qreal safeExtent = std::min({extent, screen.width() / 2.0, screen.height() / 2.0});
        QVERIFY(result.x() >= screen.left() + safeExtent);
        QVERIFY(result.x() <= screen.right() - safeExtent);
        QVERIFY(result.y() >= screen.top() + safeExtent);
        QVERIFY(result.y() <= screen.bottom() - safeExtent);
    }
};

QTEST_APPLESS_MAIN(WheelLogicTest)
#include "tst_WheelLogic.moc"
