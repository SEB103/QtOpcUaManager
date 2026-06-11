import QtQuick
import QtQuick.Controls
import QtQuick.VirtualKeyboard
import Base as Base

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

        function onApiServerConnectionRequested() {
            if (cppManagerOpcUa.connected)
                cppManagerOpcUa.disconnectFromServer()
            else
                apiServerDialog.open()
        }
    }

    Dialog {
        id: apiServerDialog
        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 940)
        height: Math.min(mainWindow.height - 80, implicitHeight)
        title: qsTr("Connect to API server")
        modal: true
        focus: true
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: Base.BsOpcUaConnectionForm {
            implicitWidth: 900
        }
    }

    Connections {
        target: cppManagerOpcUa

        function onConnectedChanged() {
            if (cppManagerOpcUa.connected && apiServerDialog.opened)
                apiServerDialog.close()
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
