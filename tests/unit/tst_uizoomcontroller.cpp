// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>

#include "uizoomcontroller.h"

/*!
 * \internal
 * \brief Unit tests for UiZoomController: zoom stepping, bounds, snapping, and
 *        persistence of the global UI zoom.
 */
class TestUiZoomController : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a controller without saved state starts at 100 %. */
    void defaultsTo100Percent();

    /*! Verifies that zoomIn() and zoomOut() walk the step ladder one step at a time. */
    void stepsThroughLadder();

    /*! Verifies that zooming stops at the smallest and largest step. */
    void clampsAtBounds();

    /*! Verifies that resetZoom() returns to 100 % and reports the change once. */
    void resetRestoresDefault();

    /*! Verifies that arbitrary percentages snap to the nearest step. */
    void nearestStep_data();
    void nearestStep();

    /*! Verifies that the zoom round-trips through QSettings and bad values are snapped. */
    void persistsThroughSettings();

    /*! Verifies that Ctrl + one wheel notch steps the zoom and the event is consumed. */
    void ctrlWheelStepsZoom();

    /*! Verifies that wheel events without exactly Ctrl pass through unchanged. */
    void wheelWithoutCtrlPassesThrough();

    /*! Verifies that small touchpad deltas accumulate to one step per notch. */
    void smallWheelDeltasAccumulate();

    /*! Verifies that reversing the wheel direction discards the pending remainder. */
    void wheelDirectionChangeResetsRemainder();

    /*! Verifies that horizontal-only rotation is not consumed. */
    void horizontalWheelPassesThrough();

private:
    /*! Creates an INI settings store inside \a dir. */
    static std::unique_ptr<QSettings> createSettings(const QTemporaryDir &dir);

    /*!
        Sends a wheel event with vertical rotation \a deltaY, horizontal rotation
        \a deltaX, and \a modifiers to \a target; returns whether it was consumed.
    */
    static bool sendWheel(QObject *target, int deltaY, Qt::KeyboardModifiers modifiers,
                          int deltaX = 0);
};

bool TestUiZoomController::sendWheel(QObject *target, int deltaY,
                                     Qt::KeyboardModifiers modifiers, int deltaX)
{
    const QPointF pos(10, 10);
    QWheelEvent event(pos, pos, QPoint(), QPoint(deltaX, deltaY), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    // An accepted event that reaches the plain QObject target was consumed by
    // the filter; QObject::event() itself never handles wheel events.
    event.setAccepted(false);
    QCoreApplication::sendEvent(target, &event);
    return event.isAccepted();
}

std::unique_ptr<QSettings> TestUiZoomController::createSettings(const QTemporaryDir &dir)
{
    return std::make_unique<QSettings>(dir.filePath(QStringLiteral("zoom.ini")),
                                       QSettings::IniFormat);
}

void TestUiZoomController::defaultsTo100Percent()
{
    UiZoomController controller(nullptr);
    QCOMPARE(controller.zoomPercent(), 100);
    QCOMPARE(controller.zoomFactor(), 1.0);
    QVERIFY(controller.isDefaultZoom());
    QVERIFY(controller.canZoomIn());
    QVERIFY(controller.canZoomOut());
}

void TestUiZoomController::stepsThroughLadder()
{
    UiZoomController controller(nullptr);
    QSignalSpy spy(&controller, &UiZoomController::zoomChanged);

    controller.zoomIn();
    QCOMPARE(controller.zoomPercent(), 110);
    controller.zoomIn();
    QCOMPARE(controller.zoomPercent(), 125);
    QCOMPARE(controller.zoomFactor(), 1.25);
    QVERIFY(!controller.isDefaultZoom());

    controller.zoomOut();
    controller.zoomOut();
    controller.zoomOut();
    QCOMPARE(controller.zoomPercent(), 90);
    QCOMPARE(spy.count(), 5);
}

void TestUiZoomController::clampsAtBounds()
{
    UiZoomController controller(nullptr);

    for (int i = 0; i < 20; ++i)
        controller.zoomIn();
    QCOMPARE(controller.zoomPercent(), UiZoomController::zoomSteps().constLast());
    QVERIFY(!controller.canZoomIn());

    QSignalSpy spy(&controller, &UiZoomController::zoomChanged);
    controller.zoomIn();
    QCOMPARE(spy.count(), 0);

    for (int i = 0; i < 20; ++i)
        controller.zoomOut();
    QCOMPARE(controller.zoomPercent(), UiZoomController::zoomSteps().constFirst());
    QVERIFY(!controller.canZoomOut());
}

void TestUiZoomController::resetRestoresDefault()
{
    UiZoomController controller(nullptr);
    controller.setZoomPercent(175);
    QCOMPARE(controller.zoomPercent(), 175);

    QSignalSpy spy(&controller, &UiZoomController::zoomChanged);
    controller.resetZoom();
    QCOMPARE(controller.zoomPercent(), 100);
    QVERIFY(controller.isDefaultZoom());
    QCOMPARE(spy.count(), 1);

    controller.resetZoom();
    QCOMPARE(spy.count(), 1);
}

void TestUiZoomController::nearestStep_data()
{
    QTest::addColumn<int>("requested");
    QTest::addColumn<int>("expected");

    QTest::newRow("exact step") << 125 << 125;
    QTest::newRow("between, closer to upper") << 120 << 125;
    QTest::newRow("between, closer to lower") << 104 << 100;
    QTest::newRow("tie picks smaller") << 105 << 100;
    QTest::newRow("below range") << 10 << 50;
    QTest::newRow("negative") << -40 << 50;
    QTest::newRow("above range") << 500 << 200;
}

void TestUiZoomController::nearestStep()
{
    QFETCH(int, requested);
    QFETCH(int, expected);
    QCOMPARE(UiZoomController::nearestStep(requested), expected);
}

void TestUiZoomController::persistsThroughSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        auto settings = createSettings(dir);
        UiZoomController controller(settings.get());
        controller.zoomIn();
        controller.zoomIn();
        QCOMPARE(controller.zoomPercent(), 125);
    }
    {
        auto settings = createSettings(dir);
        UiZoomController controller(settings.get());
        QCOMPARE(controller.zoomPercent(), 125);

        // A hand-edited value outside the ladder is snapped on the next start.
        settings->setValue(QStringLiteral("ui/zoomPercent"), 133);
    }
    {
        auto settings = createSettings(dir);
        UiZoomController controller(settings.get());
        QCOMPARE(controller.zoomPercent(), 125);

        settings->setValue(QStringLiteral("ui/zoomPercent"), QStringLiteral("garbage"));
    }
    {
        auto settings = createSettings(dir);
        UiZoomController controller(settings.get());
        QCOMPARE(controller.zoomPercent(), 100);
    }
}

void TestUiZoomController::ctrlWheelStepsZoom()
{
    UiZoomController controller(nullptr);
    QObject window;
    controller.attachToWindow(&window);

    QVERIFY(sendWheel(&window, 120, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 110);
    QVERIFY(sendWheel(&window, 120, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 125);

    QVERIFY(sendWheel(&window, -120, Qt::ControlModifier));
    QVERIFY(sendWheel(&window, -120, Qt::ControlModifier));
    QVERIFY(sendWheel(&window, -120, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 90);

    // A fast spin delivering several notches at once steps several times.
    QVERIFY(sendWheel(&window, 360, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 125);
}

void TestUiZoomController::wheelWithoutCtrlPassesThrough()
{
    UiZoomController controller(nullptr);
    QObject window;
    controller.attachToWindow(&window);

    QVERIFY(!sendWheel(&window, 120, Qt::NoModifier));
    QVERIFY(!sendWheel(&window, 120, Qt::ShiftModifier));
    QVERIFY(!sendWheel(&window, 120, Qt::ControlModifier | Qt::ShiftModifier));
    QCOMPARE(controller.zoomPercent(), 100);
}

void TestUiZoomController::smallWheelDeltasAccumulate()
{
    UiZoomController controller(nullptr);
    QObject window;
    controller.attachToWindow(&window);

    for (int i = 0; i < 3; ++i)
        QVERIFY(sendWheel(&window, 30, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 100);

    QVERIFY(sendWheel(&window, 30, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 110);
}

void TestUiZoomController::wheelDirectionChangeResetsRemainder()
{
    UiZoomController controller(nullptr);
    QObject window;
    controller.attachToWindow(&window);

    QVERIFY(sendWheel(&window, 90, Qt::ControlModifier));
    QVERIFY(sendWheel(&window, -90, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 100);

    // The upward remainder was dropped, so one more small downward delta
    // completes a full notch downwards rather than cancelling out.
    QVERIFY(sendWheel(&window, -30, Qt::ControlModifier));
    QCOMPARE(controller.zoomPercent(), 90);
}

void TestUiZoomController::horizontalWheelPassesThrough()
{
    UiZoomController controller(nullptr);
    QObject window;
    controller.attachToWindow(&window);

    QVERIFY(!sendWheel(&window, 0, Qt::ControlModifier, 120));
    QCOMPARE(controller.zoomPercent(), 100);
}

QTEST_MAIN(TestUiZoomController)

#include "tst_uizoomcontroller.moc"
