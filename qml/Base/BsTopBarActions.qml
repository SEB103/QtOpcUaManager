pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsTopBarActions
    \inqmlmodule Base
    \brief Right-aligned connection indicator and quick-action buttons for the top row.

    Sits next to the menu bar in the same top row. Shows a traffic-light connection
    status indicator and the most-used actions (connect/disconnect, connect to last,
    connection settings, save). Action buttons are tinted by connection state — muted
    when offline, the application accent when connected. The component only emits
    signals; the host wires them to the matching handlers.
*/
RowLayout {
    id: root

    /*! Whether the enclosing screen uses the dark theme. */
    property bool darkTheme: false

    /*! Neutral tint used for inactive/offline action icons. */
    readonly property color mutedColor: Qt.rgba(Material.foreground.r,
                                                 Material.foreground.g,
                                                 Material.foreground.b,
                                                 0.4)

    /*! Whether an OPC UA session is currently connected. */
    readonly property bool connected: cppManagerOpcUa.connected

    // clientState 1 is ClientConnecting (see OpcUaManager::ClientState).
    /*! Whether a connect/disconnect transition is in progress. */
    readonly property bool connecting: cppManagerOpcUa.busy || cppManagerOpcUa.clientState === 1

    /*! Emitted to connect when offline or disconnect when connected. */
    signal connectToggleRequested()
    /*! Emitted to reconnect to the last/active connection. */
    signal connectToLastRequested()
    /*! Emitted to open the connection settings dialog. */
    signal connectionSettingsRequested()
    /*! Emitted to save the active project. */
    signal saveProjectRequested()

    spacing: 2

    // Connection status indicator: icon only, traffic-light color, tooltip text.
    ToolButton {
        id: statusIndicator

        Layout.alignment: Qt.AlignVCenter
        flat: true
        focusPolicy: Qt.NoFocus
        display: AbstractButton.IconOnly
        icon.source: root.connected ? "qrc:/images/svg/link.svg"
                                     : "qrc:/images/svg/link_off.svg"
        icon.width: 20
        icon.height: 20
        icon.color: root.connected
                    ? Material.color(Material.Green)
                    : (root.connecting ? Material.color(Material.Amber)
                                       : Material.color(Material.Red))

        ToolTip.visible: hovered
        ToolTip.text: root.connected
                      ? qsTr("Connected")
                      : (root.connecting
                         ? qsTr("Connecting…")
                         : (cppManagerOpcUa.lastError.length > 0
                            ? qsTr("Offline — %1").arg(cppManagerOpcUa.lastError)
                            : qsTr("Offline")))
    }

    ToolSeparator {
        Layout.alignment: Qt.AlignVCenter
    }

    // Connect / disconnect.
    ToolButton {
        Layout.alignment: Qt.AlignVCenter
        display: AbstractButton.IconOnly
        icon.source: "qrc:/images/svg/power_settings_new.svg"
        icon.width: 20
        icon.height: 20
        enabled: !cppManagerOpcUa.busy
        icon.color: root.connected ? Material.accent : root.mutedColor
        ToolTip.visible: hovered
        ToolTip.text: root.connected ? qsTr("Disconnect from server")
                                     : qsTr("Connect to server")
        onClicked: root.connectToggleRequested()
    }

    // Connect to the last/stored connection.
    ToolButton {
        Layout.alignment: Qt.AlignVCenter
        display: AbstractButton.IconOnly
        icon.source: "qrc:/images/svg/login.svg"
        icon.width: 20
        icon.height: 20
        enabled: !cppManagerOpcUa.busy
                 && !cppManagerOpcUa.connected
                 && cppManagerOpcUa.hasLastConnection
        icon.color: enabled ? Material.accent : root.mutedColor
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Connect to last server")
        onClicked: root.connectToLastRequested()
    }

    // Open the connection settings dialog.
    ToolButton {
        Layout.alignment: Qt.AlignVCenter
        display: AbstractButton.IconOnly
        icon.source: "qrc:/images/svg/settings.svg"
        icon.width: 20
        icon.height: 20
        icon.color: root.connected ? Material.accent : Material.foreground
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Connection settings")
        onClicked: root.connectionSettingsRequested()
    }

    // Save the active project.
    ToolButton {
        Layout.alignment: Qt.AlignVCenter
        display: AbstractButton.IconOnly
        icon.source: "qrc:/images/svg/save.svg"
        icon.width: 20
        icon.height: 20
        enabled: cppProjectManager.hasActiveProject && cppProjectManager.dirty
        icon.color: enabled ? Material.accent : root.mutedColor
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Save project")
        onClicked: root.saveProjectRequested()
    }

    // Trailing spacing so the last button is not flush against the window edge.
    Item {
        Layout.preferredWidth: 6
    }
}
