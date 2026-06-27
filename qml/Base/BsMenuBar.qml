import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

MenuBar {
    id: appMenuBar

    property bool darkTheme: false

    signal quitRequested()
    signal apiServerConnectionRequested()
    signal themeToggleRequested()

    Menu {
        title: qsTr("Application")

        MenuItem {
            text: qsTr("&Login")
            enabled: false
        }

        Menu {
            title: qsTr("OPC UA")

            MenuItem {
                text: cppManagerOpcUa.connected ? qsTr("Disconnect") : qsTr("Connect")
                enabled: !cppManagerOpcUa.busy
                onTriggered: appMenuBar.apiServerConnectionRequested()
            }
        }

        MenuItem {
            text: qsTr("Sta&rt/Stop")
            enabled: false
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Quit")
            onTriggered: appMenuBar.quitRequested()
        }
    }

    Menu {
        title: qsTr("Project")

        MenuItem { text: qsTr("&Changelog"); enabled: false }
        MenuItem { text: qsTr("&Database Service"); enabled: false }
        MenuItem { text: qsTr("&Option"); enabled: false }
        MenuItem { text: qsTr("&Scale"); enabled: false }
        MenuItem { text: qsTr("S&tatistic"); enabled: false }
        MenuItem { text: qsTr("&User Management"); enabled: false }
    }

    Menu {
        title: qsTr("View")

        MenuItem {
            text: appMenuBar.darkTheme
                  ? qsTr("Switch to &Light Theme")
                  : qsTr("Switch to &Dark Theme")
            onTriggered: appMenuBar.themeToggleRequested()
        }

        MenuSeparator {}

        Menu {
            title: qsTr("&Toolbars")
            enabled: false

            MenuItem {
                text: qsTr("&Main Toolbar")
                checkable: true
            }
        }
    }

    Menu {
        title: qsTr("Info")

        MenuItem {
            text: qsTr("&Info")
            enabled: false
        }
    }

    Menu {
        title: qsTr("Help")

        MenuItem {
            text: qsTr("&Help")
            enabled: false
        }
    }
}
