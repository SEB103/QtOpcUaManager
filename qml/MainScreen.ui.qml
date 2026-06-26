import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Base as Base

Pane {
    id: main

    width: 1200
    height: 800
    padding: 0

    property alias menuBar: menuBar
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
