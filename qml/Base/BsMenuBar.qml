pragma ComponentBehavior: Bound

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

    /*!
        \qmlsignal BsMenuBar::lastConnectionRequested()
        Emitted when the user selects "Connect to Last Server". The corresponding
        handler is \c onLastConnectionRequested.
    */
    signal lastConnectionRequested()

    /*!
        \qmlsignal BsMenuBar::newProjectRequested()
        Emitted when the user selects "New Project". The corresponding handler is
        \c onNewProjectRequested.
    */
    signal newProjectRequested()

    /*!
        \qmlsignal BsMenuBar::openProjectRequested()
        Emitted when the user selects "Open Project". The corresponding handler is
        \c onOpenProjectRequested.
    */
    signal openProjectRequested()

    /*!
        \qmlsignal BsMenuBar::closeProjectRequested()
        Emitted when the user selects "Close Project". The corresponding handler is
        \c onCloseProjectRequested.
    */
    signal closeProjectRequested()

    /*!
        \qmlsignal BsMenuBar::saveProjectRequested()
        Emitted when the user selects "Save". The corresponding handler is
        \c onSaveProjectRequested.
    */
    signal saveProjectRequested()

    /*!
        \qmlsignal BsMenuBar::saveProjectAsRequested()
        Emitted when the user selects "Save As". The corresponding handler is
        \c onSaveProjectAsRequested.
    */
    signal saveProjectAsRequested()

    /*!
        \qmlsignal BsMenuBar::openRecentRequested(int index)
        Emitted when the user selects a recent project at \a index. The
        corresponding handler is \c onOpenRecentRequested.
    */
    signal openRecentRequested(int index)

    /*!
        \qmlsignal BsMenuBar::settingsRequested()
        Emitted when the user opens application settings. The corresponding
        handler is \c onSettingsRequested.
    */
    signal settingsRequested()

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

            MenuItem {
                text: qsTr("Connect to Last Server")
                enabled: !cppManagerOpcUa.busy
                         && !cppManagerOpcUa.connected
                         && cppManagerOpcUa.hasLastConnection
                onTriggered: appMenuBar.lastConnectionRequested()
            }
        }

        MenuItem {
            text: qsTr("Sta&rt/Stop")
            enabled: false
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Settings…")
            onTriggered: appMenuBar.settingsRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Quit")
            onTriggered: appMenuBar.quitRequested()
        }
    }

    Menu {
        title: qsTr("Project")

        MenuItem {
            text: qsTr("&New Project…")
            onTriggered: appMenuBar.newProjectRequested()
        }

        MenuItem {
            text: qsTr("&Open Project…")
            onTriggered: appMenuBar.openProjectRequested()
        }

        Menu {
            id: recentMenu

            title: qsTr("Open &Recent")
            enabled: cppProjectManager.recentProjects.length > 0

            Instantiator {
                model: cppProjectManager.recentProjects

                delegate: MenuItem {
                    required property int index
                    required property var modelData

                    text: modelData.displayName.length > 0
                          ? modelData.displayName : modelData.path
                    enabled: modelData.available
                    onTriggered: appMenuBar.openRecentRequested(index)
                }

                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Save")
            enabled: cppProjectManager.hasActiveProject && cppProjectManager.dirty
            onTriggered: appMenuBar.saveProjectRequested()
        }

        MenuItem {
            text: qsTr("Save &As…")
            enabled: cppProjectManager.hasActiveProject
            onTriggered: appMenuBar.saveProjectAsRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Close Project")
            enabled: cppProjectManager.hasActiveProject
            onTriggered: appMenuBar.closeProjectRequested()
        }
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
            title: qsTr("&Value Format")

            ActionGroup {
                id: valueFormatGroup
                exclusive: true
            }

            // Values mirror OpcUaManager::ValueFormat (FormatJson = 0, FormatXml = 1).
            MenuItem {
                text: qsTr("JSON")
                checkable: true
                ActionGroup.group: valueFormatGroup
                checked: cppManagerOpcUa.valueFormat === 0
                onTriggered: cppManagerOpcUa.valueFormat = 0
            }

            MenuItem {
                text: qsTr("XML")
                checkable: true
                ActionGroup.group: valueFormatGroup
                checked: cppManagerOpcUa.valueFormat === 1
                onTriggered: cppManagerOpcUa.valueFormat = 1
            }
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
