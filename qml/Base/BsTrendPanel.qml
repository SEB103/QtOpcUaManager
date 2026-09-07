import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

/*!
    \qmltype BsTrendPanel
    \inqmlmodule Base
    \brief Collapsible live plot of the values selected in the Data Access View.

    The table answers what a variable is now; this panel answers how it behaved —
    whether a drive ramps smoothly, whether a digital input chatters, how long a
    temperature takes to settle. Only live subscription data is plotted: history
    from before the session would need the OPC UA HistoryRead service, which
    CODESYS controllers rarely provide.

    The curves are drawn with QtQuick.Shapes rather than a charting module, so the
    application takes on no dependency beyond Qt Quick itself.
*/
Rectangle {
    id: root

    /*! Node ids to plot, in the order the Data Access View selected them. */
    property var plottedNodeIds: []

    /*! Display names of \l plottedNodeIds, used by the legend. */
    property var plottedNames: []

    /*! Whether each of \l plottedNodeIds is a boolean, drawn as a square wave. */
    property var plottedStepped: []

    /*! Time windows offered by the selector. */
    readonly property var windowChoices: [
        { label: qsTr("30 s"), ms: 30000 },
        { label: qsTr("5 min"), ms: 300000 },
        { label: qsTr("30 min"), ms: 1800000 }
    ]

    /*! Series colours; one per curve of the fixed pool. */
    readonly property var seriesColors: [
        "#26a69a", "#ef5350", "#42a5f5", "#ffa726",
        "#ab47bc", "#66bb6a", "#ec407a", "#8d6e63"
    ]

    /*! Number of curves the panel can draw at once. */
    readonly property int maximumSeries: 8

    /*!
        Repaint counter.

        The curves depend on it, so the plot is rebuilt at a fixed rate instead of
        once per arriving sample; a fast subscription would otherwise repaint far
        more often than a display can show.
    */
    property int tick: 0

    /*! Value range currently mapped to the plot height. */
    property var valueRange: ({ min: 0, max: 1, hasData: false })

    /*! Emitted when the user closes the panel. */
    signal closeRequested()

    /*! Neutral tint used for secondary text, grid lines, and inactive icons. */
    readonly property color mutedColor: Qt.rgba(Material.foreground.r,
                                                Material.foreground.g,
                                                Material.foreground.b,
                                                0.45)

    /*! Returns the colour of the series at \a index. */
    function seriesColor(index) {
        return root.seriesColors[index % root.seriesColors.length]
    }

    /*! Returns the value label text for \a value. */
    function formatValue(value) {
        if (!isFinite(value))
            return ""
        return Math.abs(value) >= 1000 || Number.isInteger(value)
                ? value.toFixed(0)
                : value.toFixed(2)
    }

    /*! Recomputes the shared value range and triggers a repaint of every curve. */
    function refresh() {
        if (!cppManagerOpcUa.trendModel)
            return
        root.valueRange = cppManagerOpcUa.trendModel.rangeFor(root.plottedNodeIds)
        root.tick = root.tick + 1
    }

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    // A fixed repaint rate keeps a fast subscription from driving the scene graph.
    Timer {
        interval: 100
        running: root.visible && root.plottedNodeIds.length > 0
        repeat: true
        onTriggered: root.refresh()
    }

    // Recompute immediately when the selection changes so the plot does not stay
    // empty for up to one tick.
    onPlottedNodeIdsChanged: root.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Qt.lighter(Material.background, 1.3)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 4
                spacing: 6

                Label {
                    text: qsTr("TREND")
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Material.accent
                    verticalAlignment: Text.AlignVCenter
                }

                Label {
                    Layout.leftMargin: 4
                    text: root.plottedNodeIds.length > root.maximumSeries
                          ? qsTr("%1 of %2 series").arg(root.maximumSeries)
                                                   .arg(root.plottedNodeIds.length)
                          : qsTr("%n series", "", root.plottedNodeIds.length)
                    font.pixelSize: 12
                    color: root.plottedNodeIds.length > root.maximumSeries
                           ? Material.color(Material.Amber) : root.mutedColor
                    verticalAlignment: Text.AlignVCenter
                }

                Item {
                    Layout.fillWidth: true
                }

                ComboBox {
                    id: windowBox

                    Layout.preferredWidth: 110
                    Layout.maximumHeight: 28
                    model: root.windowChoices
                    textRole: "label"
                    font.pixelSize: 12
                    currentIndex: 0
                    onActivated: {
                        if (cppManagerOpcUa.trendModel)
                            cppManagerOpcUa.trendModel.windowMs =
                                    root.windowChoices[currentIndex].ms
                        root.refresh()
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    display: AbstractButton.IconOnly
                    icon.source: cppManagerOpcUa.trendModel
                                 && cppManagerOpcUa.trendModel.paused
                                 ? "qrc:/images/svg/play_arrow.svg"
                                 : "qrc:/images/svg/stop.svg"
                    icon.width: 15
                    icon.height: 15
                    icon.color: cppManagerOpcUa.trendModel
                                && cppManagerOpcUa.trendModel.paused
                                ? Material.accent : root.mutedColor
                    Accessible.name: qsTr("Freeze the time axis")
                    ToolTip.visible: hovered
                    ToolTip.text: cppManagerOpcUa.trendModel
                                  && cppManagerOpcUa.trendModel.paused
                                  ? qsTr("Resume the time axis")
                                  : qsTr("Freeze the time axis; samples keep being recorded")
                    onClicked: {
                        if (cppManagerOpcUa.trendModel) {
                            cppManagerOpcUa.trendModel.paused =
                                    !cppManagerOpcUa.trendModel.paused
                        }
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "✕"
                    font.pixelSize: 13
                    Accessible.name: qsTr("Close the trend panel")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Hide the trend panel")
                    onClicked: root.closeRequested()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Material.dividerColor
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: root.plottedNodeIds.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Select rows in the Data View to plot them.")
                color: Material.foreground
                opacity: 0.6
            }

            Label {
                anchors.centerIn: parent
                visible: root.plottedNodeIds.length > 0 && !root.valueRange.hasData
                text: qsTr("Waiting for values…")
                color: Material.foreground
                opacity: 0.6
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                visible: root.plottedNodeIds.length > 0

                // Value axis: the three labels the eye actually uses.
                ColumnLayout {
                    Layout.preferredWidth: 58
                    Layout.fillHeight: true
                    spacing: 0

                    Label {
                        Layout.alignment: Qt.AlignRight | Qt.AlignTop
                        text: root.valueRange.hasData
                              ? root.formatValue(root.valueRange.max) : ""
                        font.pixelSize: 11
                        color: root.mutedColor
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: root.valueRange.hasData
                              ? root.formatValue((root.valueRange.min + root.valueRange.max) / 2)
                              : ""
                        font.pixelSize: 11
                        color: root.mutedColor
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight | Qt.AlignBottom
                        text: root.valueRange.hasData
                              ? root.formatValue(root.valueRange.min) : ""
                        font.pixelSize: 11
                        color: root.mutedColor
                    }
                }

                Rectangle {
                    id: plotArea

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"
                    border.color: Material.dividerColor
                    border.width: 1
                    clip: true

                    // Horizontal guides at the quarter marks of the value range.
                    Repeater {
                        model: 3

                        Rectangle {
                            required property int index

                            x: 0
                            y: plotArea.height * (index + 1) / 4
                            width: plotArea.width
                            height: 1
                            color: root.mutedColor
                            opacity: 0.25
                        }
                    }

                    // A Repeater can only instantiate items and a ShapePath is
                    // not one, so the curves are a fixed pool: each entry draws
                    // the series at its own index, or nothing when fewer are
                    // plotted. The pool size matches the colour palette.
                    Shape {
                        anchors.fill: parent
                        antialiasing: true

                        BsTrendSeries {
                            panel: root
                            seriesIndex: 0
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 1
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 2
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 3
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 4
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 5
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 6
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                        BsTrendSeries {
                            panel: root
                            seriesIndex: 7
                            plotWidth: plotArea.width
                            plotHeight: plotArea.height
                        }
                    }
                }
            }
        }

        // Time axis and legend share the footer row: both describe the plot above.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            visible: root.plottedNodeIds.length > 0
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                Label {
                    text: qsTr("−%1").arg(windowBox.currentText)
                    font.pixelSize: 11
                    color: root.mutedColor
                }

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: legendRow.width
                    flickableDirection: Flickable.HorizontalFlick
                    clip: true

                    Row {
                        id: legendRow

                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        // Only the drawn curves get a legend entry: the palette
                        // wraps, so a longer legend would give an entry with no
                        // curve the colour of an entry that has one.
                        Repeater {
                            objectName: "trendLegendRepeater"

                            model: Math.min(root.plottedNodeIds.length, root.maximumSeries)

                            Row {
                                id: legendEntry

                                required property int index

                                spacing: 4

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 3
                                    color: root.seriesColor(legendEntry.index)
                                }

                                Label {
                                    text: root.plottedNames[legendEntry.index] !== undefined
                                          ? root.plottedNames[legendEntry.index]
                                          : ""
                                    font.pixelSize: 11
                                    color: Material.foreground
                                }
                            }
                        }
                    }
                }

                Label {
                    text: cppManagerOpcUa.trendModel
                          && cppManagerOpcUa.trendModel.paused
                          ? qsTr("frozen") : qsTr("now")
                    font.pixelSize: 11
                    color: cppManagerOpcUa.trendModel
                           && cppManagerOpcUa.trendModel.paused
                           ? Material.accent : root.mutedColor
                }
            }
        }
    }
}
