import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsStatusBar
    \inqmlmodule Base
    \brief Permanent footer showing connection state, workload, and the last outcome.

    The bar answers the three questions the workspace otherwise leaves open: what
    the session is connected to, whether an operation is running, and how the
    last operation ended. The outcome text is supplied by the window, which
    collects it from the OPC UA and project facades, so this component stays a
    pure display.
*/
Rectangle {
    id: root

    /*! Text of the most recent outcome; empty leaves the message area blank. */
    property string message: ""

    /*! Severity of \l message as a Diagnostics::Level value. */
    property int messageLevel: 1

    /*! Whether the log panel is currently shown, used to highlight the toggle. */
    property bool logPanelVisible: false

    /*! Emitted when the user asks to show or hide the log panel. */
    signal logToggleRequested()

    // Client state 1 is OpcUaManager::ClientConnecting.
    /*! Whether a connect or disconnect transition is in progress. */
    readonly property bool connecting: cppManagerOpcUa.busy || cppManagerOpcUa.clientState === 1

    /*! Neutral tint used for secondary text. */
    readonly property color mutedColor: Qt.rgba(Material.foreground.r,
                                                Material.foreground.g,
                                                Material.foreground.b,
                                                0.55)

    /*! Colour matching \l messageLevel. */
    readonly property color messageColor: {
        // 2 Warning, 3 Error in Diagnostics::Level.
        if (root.messageLevel === 3)
            return Material.color(Material.Red)
        if (root.messageLevel === 2)
            return Material.color(Material.Amber)
        return root.mutedColor
    }

    implicitHeight: 28
    color: Qt.lighter(Material.background, 1.3)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Material.dividerColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 2
        spacing: 8

        // Traffic-light dot mirroring the top-bar connection indicator.
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: width / 2
            color: cppManagerOpcUa.connected
                   ? Material.color(Material.Green)
                   : (root.connecting ? Material.color(Material.Amber)
                                      : Material.color(Material.Red))
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            text: cppManagerOpcUa.connected
                  ? qsTr("Connected")
                  : (root.connecting ? qsTr("Connecting…") : qsTr("Offline"))
            font.pixelSize: 12
            color: Material.foreground
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: 320
            visible: cppManagerOpcUa.connectionSummary.length > 0
            text: cppManagerOpcUa.connectionSummary
            font.pixelSize: 12
            elide: Text.ElideMiddle
            color: root.mutedColor
            ToolTip.visible: summaryHover.hovered
            ToolTip.text: cppManagerOpcUa.connectionSummary

            HoverHandler {
                id: summaryHover
            }
        }

        ToolSeparator {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 18
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("%n node(s) watched", "", cppManagerOpcUa.monitoredNodeCount)
            font.pixelSize: 12
            color: root.mutedColor
        }

        // The only progress indication for browse, read, and connect operations.
        ProgressBar {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 90
            visible: cppManagerOpcUa.busy
            indeterminate: true
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.message
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            color: root.messageColor
            ToolTip.visible: messageHover.hovered && root.message.length > 0
            ToolTip.text: root.message

            HoverHandler {
                id: messageHover
            }
        }

        ToolButton {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            display: AbstractButton.IconOnly
            icon.source: "qrc:/images/svg/info.svg"
            icon.width: 15
            icon.height: 15
            icon.color: root.logPanelVisible ? Material.accent : root.mutedColor
            Accessible.name: qsTr("Toggle the log panel")
            ToolTip.visible: hovered
            ToolTip.text: root.logPanelVisible ? qsTr("Hide the log panel")
                                               : qsTr("Show the log panel")
            onClicked: root.logToggleRequested()
        }
    }
}
