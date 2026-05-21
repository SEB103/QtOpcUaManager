import QtQuick
import QtQuick.Controls
import QtQuick.VirtualKeyboard

Window {
    id: mainWindow
    width: 1200
    height: 800
    visible: true
    title: qsTr("OPC UA Manager")

    MainScreen {
        id: mainScreen
        anchors.fill: parent
    }

    Connections {
        target: mainScreen.menuBar

        function onQuitRequested() {
            mainWindow.close()
        }
    }


    InputPanel {
        id: inputPanel
        property bool showKeyboard: active

        z: 10
        y: showKeyboard ? parent.height - height : parent.height
        anchors.left: parent.left
        anchors.right: parent.right

        Behavior on y {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }
    }
}
