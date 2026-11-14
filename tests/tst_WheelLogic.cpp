#include "core/WheelLogic.h"

#include <QtTest>
#include <cmath>

class WheelLogicTest final : public QObject {
    Q_OBJECT

private slots:
    // Multi-monitor placement: the wheel opens on the monitor with the pointer
    // and is centred on it, whatever the layout.
    void monitorPlacement_data()
    {
        QTest::addColumn<QList<QRect>>("screens");
        QTest::addColumn<QPointF>("pointer");
        QTest::addColumn<int>("expected");
        QTest::addColumn<QPointF>("centre");
        const QList<QRect> sideBySide{{0, 0, 2560, 1440}, {2560, 0, 1920, 1080}};
        QTest::newRow("left of two") << sideBySide << QPointF(10, 10) << 0 << QPointF(1280, 720);
        QTest::newRow("right of two") << sideBySide << QPointF(3000, 500) << 1 << QPointF(3520, 540);
        QTest::newRow("the shared edge belongs to the right monitor") << sideBySide << QPointF(2560, 100) << 1 << QPointF(3520, 540);
        QTest::newRow("just left of the shared edge") << sideBySide << QPointF(2559.5, 100) << 0 << QPointF(1280, 720);
        const QList<QRect> stacked{{0, -1080, 1920, 1080}, {0, 0, 1920, 1080}};
        QTest::newRow("above") << stacked << QPointF(900, -5) << 0 << QPointF(960, -540);
        QTest::newRow("below") << stacked << QPointF(900, 5) << 1 << QPointF(960, 540);
        const QList<QRect> negative{{-1920, 0, 1920, 1080}, {0, 0, 1920, 1080}};
        QTest::newRow("negative origin") << negative << QPointF(-1, 40) << 0 << QPointF(-960, 540);
        const QList<QRect> gap{{0, 0, 1000, 800}, {1200, 0, 1000, 800}};
        QTest::newRow("in a gap, nearer the left") << gap << QPointF(1050, 400) << 0 << QPointF(500, 400);
        QTest::newRow("in a gap, nearer the right") << gap << QPointF(1150, 400) << 1 << QPointF(1700, 400);
        QTest::newRow("beyond every monitor") << gap << QPointF(9000, 9000) << 1 << QPointF(1700, 400);
        QTest::newRow("one monitor") << QList<QRect>{{0, 0, 1280, 720}} << QPointF(-50, -50) << 0 << QPointF(640, 360);
        QTest::newRow("no monitors") << QList<QRect>{} << QPointF(1, 1) << -1 << QPointF();
    }
    void monitorPlacement()
    {
        QFETCH(QList<QRect>, screens);
        QFETCH(QPointF, pointer);
        QFETCH(int, expected);
        QFETCH(QPointF, centre);
        const int index = WheelLogic::screenIndexFor(screens, pointer);
        QCOMPARE(index, expected);
        if (index >= 0) QCOMPARE(WheelLogic::centreOf(screens.at(index)), centre);
    }
    void angularSelection_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("slot");

        for (const int count : {4, 6, 8}) {
            for (int slot = 0; slot < count; ++slot) {
                const QByteArray label = QByteArray("count_") + QByteArray::number(count) + "_slot_" + QByteArray::number(slot);
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
};

QTEST_APPLESS_MAIN(WheelLogicTest)
#include "tst_WheelLogic.moc"
