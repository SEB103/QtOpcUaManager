/*
This is a UI file (.ui.qml) that is intended to be edited in Qt Design Studio only.
It is supposed to be strictly declarative and only uses a subset of QML. If you edit
this file manually, you might introduce QML code that is not supported by Qt Design Studio.
Check out https://doc.qt.io/qtcreator/creator-quick-ui-forms.html for details on .ui.qml files.
*/
import QtQuick
import QtQuick.Controls

import Base as Base

Rectangle {
    id: main
    width: 1200
    height: 800

    property alias menuBar: menuBar

    color: "#EAEAEA"

    Pane {
        anchors.fill: parent
    }

    Base.BsMenuBar {
        id: menuBar
        width: main.width
    }

    Base.BsOpcUaBrowser {
        id: opcUaBrowser
        anchors.top: menuBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
