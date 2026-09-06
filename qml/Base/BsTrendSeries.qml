import QtQuick
import QtQuick.Shapes

/*!
    \qmltype BsTrendSeries
    \inqmlmodule Base
    \brief One curve of \l BsTrendPanel.

    Extracted into its own component because a \c Repeater can only instantiate
    items, and a \c ShapePath is not one. The panel therefore declares a fixed
    pool of these and each one draws the series at its own \l seriesIndex, or
    nothing when fewer series are plotted than the pool holds.
*/
ShapePath {
    id: series

    /*! Panel supplying the plotted series, the value range, and the repaint tick. */
    property var panel: null

    /*! Position of this curve among the panel's plotted series. */
    property int seriesIndex: 0

    /*! Width of the plot area in pixels. */
    property real plotWidth: 0

    /*! Height of the plot area in pixels. */
    property real plotHeight: 0

    /*! Whether this pool entry currently has a series to draw. */
    readonly property bool active:
        series.panel !== null && series.seriesIndex < series.panel.plottedNodeIds.length

    strokeColor: series.active ? series.panel.seriesColor(series.seriesIndex)
                               : "transparent"
    strokeWidth: 1.5
    fillColor: "transparent"
    capStyle: ShapePath.RoundCap
    joinStyle: ShapePath.RoundJoin

    PathPolyline {
        path: {
            if (!series.active || !series.panel.valueRange.hasData
                    || !cppManagerOpcUa.trendModel) {
                return []
            }

            // Depend on the panel's tick so the curve is rebuilt at the panel's
            // repaint rate rather than on every arriving sample.
            series.panel.tick

            return cppManagerOpcUa.trendModel.polylineFor(
                        series.panel.plottedNodeIds[series.seriesIndex],
                        series.plotWidth,
                        series.plotHeight,
                        series.panel.valueRange.min,
                        series.panel.valueRange.max,
                        series.panel.plottedStepped[series.seriesIndex] === true)
        }
    }
}
