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
        }

        Menu {
            title: qsTr("OPC UA")

            MenuItem {
                text: cppManagerOpcUa.connected ? qsTr("Disconnect") : qsTr("Connect")
                onTriggered: appMenuBar.apiServerConnectionRequested()
            }
        }

        MenuItem {
            text: qsTr("Sta&rt/Stop")
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Quit")
            onTriggered: appMenuBar.quitRequested()
        }
    }

    Menu {
        title: qsTr("Project")

        MenuItem { text: qsTr("&Changelog") }
        MenuItem { text: qsTr("&Database Service") }
        MenuItem { text: qsTr("&Option") }
        MenuItem { text: qsTr("&Scale") }
        MenuItem { text: qsTr("S&tatistic") }
        MenuItem { text: qsTr("&User Management") }
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
        }
    }

    Menu {
        title: qsTr("Help")

        MenuItem {
            text: qsTr("&Help")
        }
    }
}
