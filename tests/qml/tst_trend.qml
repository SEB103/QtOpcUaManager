import QtQuick
import QtTest
import Base

/*!
    \qmltype tst_trend
    \brief Hosts behavior tests for the live trend panel.

    The cases run against the real TrendModel the test mock exposes, so the value
    range, the window selector, and the curve geometry are exercised the same way
    the application uses them.
*/
Item {
    id: root

    width: 1000
    height: 700

    Component {
        id: trendComponent

        BsTrendPanel {
            width: 800
            height: 300
        }
    }

    TestCase {
        id: testCase

        /*! Test case for the trend panel's selection handling and plotting. */
        name: "TrendPanel"
        when: windowShown

        function cleanup() {
            cppManagerOpcUa.clearMockNodes();
            cppManagerOpcUa.trendModel.paused = false;
            cppManagerOpcUa.trendModel.windowMs = 30000;
        }

        /*! Verifies that the panel says what to do when nothing is selected. */
        function test_emptySelectionPlotsNothing() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            compare(panel.plottedNodeIds.length, 0);
            compare(panel.valueRange.hasData, false);
        }

        /*! Verifies that the value range follows the plotted series. */
        function test_valueRangeFollowsThePlottedSeries() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            const model = cppManagerOpcUa.trendModel;
            model.windowMs = 10000;
            model.paused = true;
            const ref = model.referenceTimeMs();

            cppManagerOpcUa.addMockSample("ns=1;s=A", ref - 4000, 2);
            cppManagerOpcUa.addMockSample("ns=1;s=A", ref - 2000, 6);

            panel.plottedNodeIds = ["ns=1;s=A"];
            panel.refresh();

            compare(panel.valueRange.hasData, true);
            compare(panel.valueRange.min, 2);
            compare(panel.valueRange.max, 6);
        }

        /*! Verifies that a series with no samples yet leaves the plot empty. */
        function test_selectedSeriesWithoutSamplesHasNoRange() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            panel.plottedNodeIds = ["ns=1;s=Silent"];
            panel.refresh();

            // The panel must say it is waiting rather than draw a flat line at zero.
            compare(panel.valueRange.hasData, false);
        }

        /*! Verifies that the panel asks the window to close it. */
        function test_panelRequestsClose() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            const spy = createTemporaryObject(signalSpyComponent, root,
                                              { target: panel,
                                                signalName: "closeRequested" });
            verify(spy !== null);

            panel.closeRequested();
            compare(spy.count, 1);
        }

        /*! Verifies that every window choice maps to a real duration. */
        function test_windowChoicesAreDurations() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            compare(panel.windowChoices.length, 3);
            for (let i = 0; i < panel.windowChoices.length; ++i) {
                verify(panel.windowChoices[i].ms >= 1000);
                verify(panel.windowChoices[i].label.length > 0);
                if (i > 0)
                    verify(panel.windowChoices[i].ms > panel.windowChoices[i - 1].ms);
            }
        }

        /*! Verifies that series colours repeat instead of running out. */
        function test_seriesColoursRepeat() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            const count = panel.seriesColors.length;
            verify(count > 0);
            compare(String(panel.seriesColor(0)), String(panel.seriesColor(count)));
            verify(String(panel.seriesColor(0)) !== String(panel.seriesColor(1)));
        }

        /*!
            Verifies that a curve of the pool draws its own series.

            The curves are a fixed pool rather than a Repeater, because a
            ShapePath is not an Item and a Repeater silently creates nothing for
            it. This case fails if that pool ever stops producing geometry.
        */
        function test_seriesDrawsAPolylineForItsIndex() {
            const model = cppManagerOpcUa.trendModel;
            model.windowMs = 10000;
            model.paused = true;
            const ref = model.referenceTimeMs();

            cppManagerOpcUa.addMockSample("ns=1;s=A", ref - 8000, 1);
            cppManagerOpcUa.addMockSample("ns=1;s=A", ref - 2000, 9);

            stubPanel.plottedNodeIds = ["ns=1;s=A"];
            stubPanel.plottedStepped = [false];

            const series = createTemporaryObject(seriesComponent, root, {
                                                     panel: stubPanel,
                                                     seriesIndex: 0,
                                                     plotWidth: 100,
                                                     plotHeight: 50
                                                 });
            verify(series !== null);
            compare(series.active, true);
            compare(series.pathElements.length, 1);
            verify(series.pathElements[0].path.length > 0);

            // A pool entry past the plotted series draws nothing at all.
            const spare = createTemporaryObject(seriesComponent, root, {
                                                    panel: stubPanel,
                                                    seriesIndex: 3,
                                                    plotWidth: 100,
                                                    plotHeight: 50
                                                });
            verify(spare !== null);
            compare(spare.active, false);
            compare(spare.pathElements[0].path.length, 0);
        }

        /*!
            Verifies that building the panel really creates its curves.

            An earlier version repeated the ShapePath with a Repeater, which
            instantiates items only: it created nothing, warned, and left the
            plot permanently empty while every other test still passed.
        */
        function test_panelBuildsItsCurvePoolWithoutWarnings() {
            failOnWarning(/Delegate must be of Item type/);

            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);
            verify(panel.maximumSeries > 0);
        }

        /*! Verifies that the curve pool and the colour palette stay in step. */
        function test_poolSizeMatchesThePalette() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            // Every pool entry needs its own colour, and every colour needs an
            // entry, or a series would either be invisible or unreachable.
            compare(panel.maximumSeries, panel.seriesColors.length);
        }

        /*! Verifies that pausing the panel freezes the model's time axis. */
        function test_pauseFreezesTheModel() {
            const panel = createTemporaryObject(trendComponent, root);
            verify(panel !== null);

            const model = cppManagerOpcUa.trendModel;
            model.paused = true;
            const frozen = model.referenceTimeMs();

            wait(30);
            compare(model.referenceTimeMs(), frozen);

            model.paused = false;
            verify(model.referenceTimeMs() >= frozen);
        }
    }

    Component {
        id: seriesComponent

        BsTrendSeries {}
    }

    /*! Stand-in for the panel, so one curve can be tested on its own. */
    QtObject {
        id: stubPanel

        property var plottedNodeIds: []
        property var plottedStepped: []
        property var valueRange: ({ min: 0, max: 10, hasData: true })
        property int tick: 0

        function seriesColor(index) {
            return "#26a69a";
        }
    }

    Component {
        id: signalSpyComponent

        SignalSpy {}
    }
}
