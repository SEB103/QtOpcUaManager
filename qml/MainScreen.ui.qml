import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Base as Base

/*!
    \qmltype MainScreen
    \inqmlmodule OpcUaManager
    \brief Composes the main menu, top-bar quick actions, and OPC UA browser area.
*/
Pane {
    id: main

    width: 1200
    height: 800
    padding: 0

    /*! Exposes the menu bar so Main can connect to its public signals. */
    property alias menuBar: menuBar

    /*! Exposes the top-bar quick actions so Main can connect to their signals. */
    property alias topActions: topActions

    /*! Whether child controls should follow the dark theme state. */
    property bool darkTheme: false

    // Single top row: menu titles on the left, connection indicator and quick
    // actions on the right. Keeping both in one row leaves the browser area intact.
    RowLayout {
        id: topBar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Base.BsMenuBar {
            id: menuBar

            Layout.fillWidth: true
            darkTheme: main.darkTheme
        }

        Base.BsTopBarActions {
            id: topActions

            Layout.alignment: Qt.AlignVCenter
            darkTheme: main.darkTheme
        }
    }

    Base.BsOpcUaBrowser {
        id: opcUaBrowser

        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
