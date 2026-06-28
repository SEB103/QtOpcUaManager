import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Base as Base

/*!
    \qmltype MainScreen
    \inqmlmodule OpcUaManager
    \brief Composes the main menu and OPC UA browser area.
*/
Pane {
    id: main

    width: 1200
    height: 800
    padding: 0

    /*! Exposes the menu bar so Main can connect to its public signals. */
    property alias menuBar: menuBar

    /*! Whether child controls should follow the dark theme state. */
    property bool darkTheme: false

    Base.BsMenuBar {
        id: menuBar

        width: main.width
        darkTheme: main.darkTheme
    }

    Base.BsOpcUaBrowser {
        id: opcUaBrowser

        anchors.top: menuBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
