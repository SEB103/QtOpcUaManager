import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Base as Base

/*!
    \qmltype Main
    \inqmlmodule OpcUaManager
    \brief Provides the main application window.

    The window owns the top-level Material theme state, hosts MainScreen, and
    opens the OPC UA connection dialog from menu actions.
*/
ApplicationWindow {
    id: mainWindow

    width: 1200
    height: 800
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: qsTr("OPC UA Manager")

    /*! Whether the application currently uses the dark Material theme. */
    property bool darkTheme: Application.styleHints.colorScheme === Qt.Dark

    Material.theme: darkTheme ? Material.Dark : Material.Light
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    MainScreen {
        id: mainScreen
        anchors.fill: parent
        darkTheme: mainWindow.darkTheme
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

        function onThemeToggleRequested() {
            mainWindow.darkTheme = !mainWindow.darkTheme
        }

        function onLastConnectionRequested() {
            cppManagerOpcUa.connectToLast()
        }
    }

    Dialog {
        id: apiServerDialog

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 940)
        height: Math.min(mainWindow.height - 80, implicitHeight)
        title: qsTr("Connect to OPC UA server")
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

        function onPasswordRequired(userName) {
            reconnectPasswordField.text = ""
            reconnectPasswordDialog.userName = userName
            reconnectPasswordDialog.open()
        }
    }

    Dialog {
        id: reconnectPasswordDialog

        /*! User name the stored connection authenticates as. */
        property string userName: ""

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 420)
        title: qsTr("Password required")
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        onAccepted: cppManagerOpcUa.provideReconnectPassword(reconnectPasswordField.text)

        Column {
            width: parent.width
            spacing: 8

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Enter the password for user \"%1\".")
                          .arg(reconnectPasswordDialog.userName)
            }

            TextField {
                id: reconnectPasswordField

                width: parent.width
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
                onAccepted: reconnectPasswordDialog.accept()
            }
        }
    }

}
