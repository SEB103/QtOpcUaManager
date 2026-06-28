import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

/*!
    \qmltype BsMenuBar
    \inqmlmodule Base
    \brief Provides the application menu bar and user action signals.
*/
MenuBar {
    id: appMenuBar

    /*! Whether the menu should describe the current theme as dark. */
    property bool darkTheme: false

    /*!
        \qmlsignal BsMenuBar::quitRequested()
        Emitted when the user selects the Quit menu item. The corresponding
        handler is \c onQuitRequested.
    */
    signal quitRequested()

    /*!
        \qmlsignal BsMenuBar::apiServerConnectionRequested()
        Emitted when the user requests connect or disconnect from the OPC UA
        menu. The corresponding handler is \c onApiServerConnectionRequested.
    */
    signal apiServerConnectionRequested()

    /*!
        \qmlsignal BsMenuBar::themeToggleRequested()
        Emitted when the user requests switching between light and dark themes.
        The corresponding handler is \c onThemeToggleRequested.
    */
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
