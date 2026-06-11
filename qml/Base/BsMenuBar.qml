import QtQuick
import QtQuick.Controls

MenuBar {
    id: appMenuBar

    signal quitRequested()
    signal apiServerConnectionRequested()

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

        //        MenuSeparator {
        //            visible: recentFilesModel.count > 0
        //        }
        MenuItem {
            text: qsTr("&Quit")
            //        shortcut: "Ctrl+Q"
            //        iconName: "Quit"
            //        iconSource: "/images/power-off-DRED.svg"
            onTriggered: appMenuBar.quitRequested()
        }
    }

    Menu {
        title: qsTr("Project")

        MenuItem {
            text: qsTr("&Changelog")
        }
        MenuItem {
            text: qsTr("&Database Service")
        }
        MenuItem {
            text: qsTr("&Option")
        }
        MenuItem {
            text: qsTr("&Scale")
        }
        MenuItem {
            text: qsTr("S&tatistic")
        }
        MenuItem {
            text: qsTr("&User Managmant")
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

    Menu {
        title: qsTr("View")
        Menu {
            title: qsTr("&Toolbars")
            MenuItem {
                text: qsTr("&Main Toolbar")
                checkable: true
            }
        }
    }

    // delegate: MenuBarItem {
    //     id: menuBarItem
    //     contentItem: Text {
    //         text: menuBarItem.text
    //         font: menuBarItem.font
    //         opacity: enabled ? 1.0 : 0.3
    //         color: menuBarItem.highlighted ? "#ffffff" : "#21be2b"
    //         horizontalAlignment: Text.AlignLeft
    //         verticalAlignment: Text.AlignVCenter
    //         elide: Text.ElideRight
    //     }

    //     background: Rectangle {
    //         implicitWidth: 40
    //         implicitHeight: 40
    //         opacity: enabled ? 1.0 : 0.3
    //         color: menuBarItem.highlighted ? "#21be2b" : "transparent"
    //     }
    // }
}
