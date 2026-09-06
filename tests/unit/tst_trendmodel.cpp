#include <QSignalSpy>
#include <QtTest>

#include "models/trendmodel.h"

/*! Verifies TrendModel sample storage and the coordinate mapping it produces. */
class TrendModelTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that samples are stored per node and counted. */
    void samplesAreStoredPerNode();

    /*! Verifies that a sample older than the newest one is rejected. */
    void outOfOrderSamplesAreRejected();

    /*! Verifies that a full series drops its oldest samples. */
    void reachingTheCapDropsOldestSamples();

    /*! Verifies that pausing freezes the time axis without stopping collection. */
    void pauseFreezesTheTimeAxisOnly();

    /*! Verifies that the value range covers every series inside the window. */
    void rangeCoversEverySeriesInTheWindow();

    /*! Verifies that a constant value is given a range instead of collapsing. */
    void degenerateRangeIsWidened();

    /*! Verifies the coordinates a plain line is mapped to. */
    void polylineMapsSamplesToWidgetCoordinates();

    /*! Verifies that a stepped series gets the corner points of a square wave. */
    void steppedPolylineInsertsCornerPoints();

    /*! Verifies that the line enters from the left edge and reaches the right one. */
    void polylineSpansTheWholeWindow();

    /*! Verifies that an unusable request yields no polyline instead of garbage. */
    void polylineRefusesUnusableRequests();

    /*! Verifies that dropping and clearing remove series. */
    void dropAndClearRemoveSeries();
};

/*!
 * \brief Verifies that samples are stored per node and counted.
 */
void TrendModelTest::samplesAreStoredPerNode()
{
    TrendModel model;
    QSignalSpy seriesSpy(&model, &TrendModel::seriesChanged);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    model.appendSample(QStringLiteral("A"), now, 1.0);
    model.appendSample(QStringLiteral("B"), now, 2.0);
    model.appendSample(QStringLiteral("A"), now + 1, 3.0);

    QCOMPARE(model.sampleCountFor(QStringLiteral("A")), 2);
    QCOMPARE(model.sampleCountFor(QStringLiteral("B")), 1);
    QCOMPARE(model.sampleCountFor(QStringLiteral("missing")), 0);
    QCOMPARE(seriesSpy.count(), 3);

    // A value that cannot be placed on an axis is not a sample.
    model.appendSample(QStringLiteral("A"), now + 2, qQNaN());
    model.appendSample(QStringLiteral("A"), now + 3, qInf());
    model.appendSample(QString(), now + 4, 1.0);
    QCOMPARE(model.sampleCountFor(QStringLiteral("A")), 2);
}

/*!
 * \brief Verifies that a sample older than the newest one is rejected.
 */
void TrendModelTest::outOfOrderSamplesAreRejected()
{
    TrendModel model;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    model.appendSample(QStringLiteral("A"), now, 1.0);

    // A series that is not monotonic in time cannot be drawn as one line, so a
    // late arrival is dropped rather than folding the curve back on itself.
    model.appendSample(QStringLiteral("A"), now - 100, 2.0);
    QCOMPARE(model.sampleCountFor(QStringLiteral("A")), 1);

    model.appendSample(QStringLiteral("A"), now, 3.0);
    QCOMPARE(model.sampleCountFor(QStringLiteral("A")), 2);
}

/*!
 * \brief Verifies that a full series drops its oldest samples.
 */
void TrendModelTest::reachingTheCapDropsOldestSamples()
{
    TrendModel model(20);
    QCOMPARE(model.maximumSamples(), 20);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < 25; ++i)
        model.appendSample(QStringLiteral("A"), now + i, double(i));

    // Memory stays bounded in a long session; the newest samples survive.
    QVERIFY(model.sampleCountFor(QStringLiteral("A")) <= 20);
    QVERIFY(model.sampleCountFor(QStringLiteral("A")) > 0);

    // A pathologically small cap is raised to a usable minimum.
    TrendModel tiny(1);
    QCOMPARE(tiny.maximumSamples(), 10);
}

/*!
 * \brief Verifies that pausing freezes the time axis without stopping collection.
 */
void TrendModelTest::pauseFreezesTheTimeAxisOnly()
{
    TrendModel model;
    QSignalSpy pausedSpy(&model, &TrendModel::pausedChanged);

    model.setPaused(true);
    QCOMPARE(pausedSpy.count(), 1);

    const qint64 frozen = model.referenceTimeMs();
    QTest::qWait(30);
    QCOMPARE(model.referenceTimeMs(), frozen);

    // Samples keep being recorded, so nothing is lost while the view is frozen.
    model.appendSample(QStringLiteral("A"), frozen + 10, 1.0);
    QCOMPARE(model.sampleCountFor(QStringLiteral("A")), 1);

    model.setPaused(false);
    QVERIFY(model.referenceTimeMs() >= frozen);

    // The window has a lower bound so a zero window cannot divide the mapping.
    model.setWindowMs(0);
    QCOMPARE(model.windowMs(), 1000);
}

/*!
 * \brief Verifies that the value range covers every series inside the window.
 */
void TrendModelTest::rangeCoversEverySeriesInTheWindow()
{
    TrendModel model;
    model.setWindowMs(10000);
    model.setPaused(true);
    const qint64 ref = model.referenceTimeMs();

    model.appendSample(QStringLiteral("A"), ref - 20000, 999.0);   // outside
    model.appendSample(QStringLiteral("A"), ref - 5000, 2.0);
    model.appendSample(QStringLiteral("B"), ref - 3000, 8.0);

    const QVariantMap range = model.rangeFor({QStringLiteral("A"), QStringLiteral("B")});
    QVERIFY(range.value(QStringLiteral("hasData")).toBool());
    QCOMPARE(range.value(QStringLiteral("min")).toDouble(), 2.0);

    // The sample from before the window must not stretch the axis.
    QCOMPARE(range.value(QStringLiteral("max")).toDouble(), 8.0);

    const QVariantMap empty = model.rangeFor({QStringLiteral("missing")});
    QVERIFY(!empty.value(QStringLiteral("hasData")).toBool());
}

/*!
 * \brief Verifies that a constant value is given a range instead of collapsing.
 */
void TrendModelTest::degenerateRangeIsWidened()
{
    TrendModel model;
    model.setWindowMs(10000);
    model.setPaused(true);
    const qint64 ref = model.referenceTimeMs();

    model.appendSample(QStringLiteral("A"), ref - 4000, 5.0);
    model.appendSample(QStringLiteral("A"), ref - 2000, 5.0);

    // Without widening, a constant line would divide by a zero range and end up
    // pinned to an edge of the plot.
    const QVariantMap range = model.rangeFor({QStringLiteral("A")});
    QCOMPARE(range.value(QStringLiteral("min")).toDouble(), 4.5);
    QCOMPARE(range.value(QStringLiteral("max")).toDouble(), 5.5);
}

/*!
 * \brief Verifies the coordinates a plain line is mapped to.
 */
void TrendModelTest::polylineMapsSamplesToWidgetCoordinates()
{
    TrendModel model;
    model.setWindowMs(10000);
    model.setPaused(true);
    const qint64 ref = model.referenceTimeMs();

    model.appendSample(QStringLiteral("A"), ref - 10000, 0.0);
    model.appendSample(QStringLiteral("A"), ref - 5000, 10.0);

    const QList<QPointF> points =
        model.polylineFor(QStringLiteral("A"), 1000.0, 100.0, 0.0, 10.0, false);

    QCOMPARE(points.size(), 3);

    // The left edge is now minus the window, and y grows downward, so the
    // smallest value sits at the bottom.
    QCOMPARE(points.at(0), QPointF(0.0, 100.0));
    QCOMPARE(points.at(1), QPointF(500.0, 0.0));

    // The last known value is held to the right edge: a subscription reports
    // changes, so silence means the value still holds.
    QCOMPARE(points.at(2), QPointF(1000.0, 0.0));
}

/*!
 * \brief Verifies that a stepped series gets the corner points of a square wave.
 */
void TrendModelTest::steppedPolylineInsertsCornerPoints()
{
    TrendModel model;
    model.setWindowMs(10000);
    model.setPaused(true);
    const qint64 ref = model.referenceTimeMs();

    model.appendSample(QStringLiteral("A"), ref - 10000, 0.0);
    model.appendSample(QStringLiteral("A"), ref - 5000, 1.0);

    const QList<QPointF> points =
        model.polylineFor(QStringLiteral("A"), 1000.0, 100.0, 0.0, 1.0, true);

    // A sloped segment between 0 and 1 would show states a boolean never had,
    // so the previous level is held until the instant it changed.
    QCOMPARE(points.size(), 4);
    QCOMPARE(points.at(0), QPointF(0.0, 100.0));
    QCOMPARE(points.at(1), QPointF(500.0, 100.0));
    QCOMPARE(points.at(2), QPointF(500.0, 0.0));
    QCOMPARE(points.at(3), QPointF(1000.0, 0.0));
}

/*!
 * \brief Verifies that the line enters from the left edge and reaches the right one.
 */
void TrendModelTest::polylineSpansTheWholeWindow()
{
    TrendModel model;
    model.setWindowMs(10000);
    model.setPaused(true);
    const qint64 ref = model.referenceTimeMs();

    model.appendSample(QStringLiteral("A"), ref - 15000, 2.0);
    model.appendSample(QStringLiteral("A"), ref - 5000, 8.0);

    const QList<QPointF> points =
        model.polylineFor(QStringLiteral("A"), 1000.0, 100.0, 0.0, 10.0, false);

    QCOMPARE(points.size(), 3);

    // The sample from before the window is kept so the curve enters from off
    // screen instead of starting wherever the first visible sample happens to be.
    QCOMPARE(points.at(0), QPointF(-500.0, 80.0));
    QCOMPARE(points.at(1), QPointF(500.0, 20.0));
    QCOMPARE(points.at(2), QPointF(1000.0, 20.0));
}

/*!
 * \brief Verifies that an unusable request yields no polyline instead of garbage.
 */
void TrendModelTest::polylineRefusesUnusableRequests()
{
    TrendModel model;
    model.setPaused(true);
    const qint64 ref = model.referenceTimeMs();
    model.appendSample(QStringLiteral("A"), ref - 1000, 1.0);

    QVERIFY(model.polylineFor(QStringLiteral("missing"), 100.0, 100.0, 0.0, 1.0, false).isEmpty());
    QVERIFY(model.polylineFor(QStringLiteral("A"), 0.0, 100.0, 0.0, 1.0, false).isEmpty());
    QVERIFY(model.polylineFor(QStringLiteral("A"), 100.0, 0.0, 0.0, 1.0, false).isEmpty());

    // A collapsed value range would divide by zero; the caller is expected to
    // widen it through rangeFor() first.
    QVERIFY(model.polylineFor(QStringLiteral("A"), 100.0, 100.0, 5.0, 5.0, false).isEmpty());
}

/*!
 * \brief Verifies that dropping and clearing remove series.
 */
void TrendModelTest::dropAndClearRemoveSeries()
{
    TrendModel model;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    model.appendSample(QStringLiteral("A"), now, 1.0);
    model.appendSample(QStringLiteral("B"), now, 2.0);

    QSignalSpy seriesSpy(&model, &TrendModel::seriesChanged);

    model.dropNode(QStringLiteral("A"));
    QCOMPARE(model.sampleCountFor(QStringLiteral("A")), 0);
    QCOMPARE(model.sampleCountFor(QStringLiteral("B")), 1);
    QCOMPARE(seriesSpy.count(), 1);

    // Dropping a series that is not there reports nothing.
    model.dropNode(QStringLiteral("A"));
    QCOMPARE(seriesSpy.count(), 1);

    model.clear();
    QCOMPARE(model.sampleCountFor(QStringLiteral("B")), 0);
    QCOMPARE(seriesSpy.count(), 2);

    model.clear();
    QCOMPARE(seriesSpy.count(), 2);
}

QTEST_MAIN(TrendModelTest)

#include "tst_trendmodel.moc"
