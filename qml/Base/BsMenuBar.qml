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
        \qmlsignal BsMenuBar::serverManagerRequested()
        Emitted when the user switches to the Server Manager (Server Studio)
        section. The corresponding handler is \c onServerManagerRequested.
    */
    signal serverManagerRequested()

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
        \qmlsignal BsMenuBar::logPanelToggleRequested()

        Emitted when the user asks to show or hide the log panel.
    */
    signal logPanelToggleRequested()

    /*!
        \qmlsignal BsMenuBar::trendPanelToggleRequested()

        Emitted when the user asks to show or hide the trend panel.
    */
    signal trendPanelToggleRequested()

    /*! Whether the log panel is currently shown, used to word the menu item. */
    property bool logPanelVisible: false

    /*! Whether the trend panel is currently shown, used to word the menu item. */
    property bool trendPanelVisible: false

    /*!
        \qmlsignal BsMenuBar::settingsRequested()
        Emitted when the user opens application settings. The corresponding
        handler is \c onSettingsRequested.
    */
    signal settingsRequested()

    /*!
        \qmlsignal BsMenuBar::aboutRequested()
        Emitted when the user opens the About dialog from the Info menu. The
        corresponding handler is \c onAboutRequested.
    */
    signal aboutRequested()

    /*!
        \qmlsignal BsMenuBar::helpRequested()
        Emitted when the user opens the offline documentation from the Info menu.
        The corresponding handler is \c onHelpRequested.
    */
    signal helpRequested()

    /*!
        \qmlsignal BsMenuBar::checkForUpdatesRequested()
        Emitted when the user selects "Check for updates" from the Info menu. The
        corresponding handler is \c onCheckForUpdatesRequested.
    */
    signal checkForUpdatesRequested()

    /*!
        \qmlsignal BsMenuBar::cloneToServerStudioRequested()
        Emitted to clone the browsed client address space into a Server Studio
        project. The host calls \c cppServerStudio.cloneFromClient().
    */
    signal cloneToServerStudioRequested()

    Menu {
        title: qsTr("Application")

        MenuItem {
            text: qsTr("&Login")
            icon.source: "qrc:/images/svg/login.svg"
            enabled: false
        }

        Menu {
            // A submenu entry copies the whole Menu.icon, so an unset size would
            // replace the style's 24 px item icon with the SVG's intrinsic size.
            // Every submenu therefore states the menu item icon size explicitly.
            title: qsTr("OPC UA")
            icon.source: "qrc:/images/svg/hub.svg"
            icon.width: 24
            icon.height: 24

            MenuItem {
                text: cppManagerOpcUa.connected ? qsTr("Disconnect") : qsTr("Connect")
                icon.source: cppManagerOpcUa.connected ? "qrc:/images/svg/link_off.svg"
                                                       : "qrc:/images/svg/link.svg"
                enabled: !cppManagerOpcUa.busy
                onTriggered: appMenuBar.apiServerConnectionRequested()
            }

            MenuItem {
                text: qsTr("Connect to Last Server")
                icon.source: "qrc:/images/svg/replay.svg"
                enabled: !cppManagerOpcUa.busy
                         && !cppManagerOpcUa.connected
                         && cppManagerOpcUa.hasLastConnection
                onTriggered: appMenuBar.lastConnectionRequested()
            }
        }

        MenuItem {
            text: qsTr("Sta&rt/Stop")
            icon.source: "qrc:/images/svg/power_settings_new.svg"
            enabled: false
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Server &Manager")
            icon.source: "qrc:/images/svg/dns.svg"
            onTriggered: appMenuBar.serverManagerRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Settings…")
            icon.source: "qrc:/images/svg/settings.svg"
            onTriggered: appMenuBar.settingsRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Quit")
            icon.source: "qrc:/images/svg/exit_to_app.svg"
            onTriggered: appMenuBar.quitRequested()
        }
    }

    Menu {
        title: qsTr("Project")

        MenuItem {
            text: qsTr("&New Project…")
            icon.source: "qrc:/images/svg/note_add.svg"
            onTriggered: appMenuBar.newProjectRequested()
        }

        MenuItem {
            text: qsTr("&Open Project…")
            icon.source: "qrc:/images/svg/folder_open.svg"
            onTriggered: appMenuBar.openProjectRequested()
        }

        Menu {
            id: recentMenu

            title: qsTr("Open &Recent")
            icon.source: "qrc:/images/svg/history.svg"
            icon.width: 24
            icon.height: 24
            enabled: cppProjectManager.recentProjects.length > 0

            Instantiator {
                model: cppProjectManager.recentProjects

                delegate: MenuItem {
                    required property int index
                    required property var modelData

                    text: modelData.displayName.length > 0
                          ? modelData.displayName : modelData.path
                    icon.source: "qrc:/images/svg/description.svg"
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
            icon.source: "qrc:/images/svg/save.svg"
            enabled: cppProjectManager.hasActiveProject && cppProjectManager.dirty
            onTriggered: appMenuBar.saveProjectRequested()
        }

        MenuItem {
            text: qsTr("Save &As…")
            icon.source: "qrc:/images/svg/save_as.svg"
            enabled: cppProjectManager.hasActiveProject
            onTriggered: appMenuBar.saveProjectAsRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&Close Project")
            icon.source: "qrc:/images/svg/close.svg"
            enabled: cppProjectManager.hasActiveProject
            onTriggered: appMenuBar.closeProjectRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Clone to Server Studio")
            icon.source: "qrc:/images/svg/content_copy.svg"
            enabled: cppManagerOpcUa.connected
            onTriggered: appMenuBar.cloneToServerStudioRequested()
        }
    }

    Menu {
        title: qsTr("View")

        MenuItem {
            text: appMenuBar.darkTheme
                  ? qsTr("Switch to &Light Theme")
                  : qsTr("Switch to &Dark Theme")
            icon.source: appMenuBar.darkTheme ? "qrc:/images/svg/light_mode.svg"
                                              : "qrc:/images/svg/dark_mode.svg"
            onTriggered: appMenuBar.themeToggleRequested()
        }

        MenuItem {
            text: appMenuBar.trendPanelVisible ? qsTr("Hide &Trend Panel")
                                               : qsTr("Show &Trend Panel")
            icon.source: "qrc:/images/svg/show_chart.svg"
            onTriggered: appMenuBar.trendPanelToggleRequested()
        }

        MenuItem {
            text: appMenuBar.logPanelVisible ? qsTr("Hide &Log Panel")
                                             : qsTr("Show &Log Panel")
            icon.source: "qrc:/images/svg/article.svg"
            onTriggered: appMenuBar.logPanelToggleRequested()
        }

        MenuSeparator {}

        Menu {
            title: qsTr("&Value Format")
            icon.source: "qrc:/images/svg/code.svg"
            icon.width: 24
            icon.height: 24

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
            icon.source: "qrc:/images/svg/toolbar.svg"
            icon.width: 24
            icon.height: 24
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
            text: qsTr("&Documentation")
            icon.source: "qrc:/images/svg/menu_book.svg"
            onTriggered: appMenuBar.helpRequested()
        }

        MenuItem {
            text: qsTr("&Check for updates…")
            icon.source: "qrc:/images/svg/update.svg"
            onTriggered: appMenuBar.checkForUpdatesRequested()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("&About OpcUaManager…")
            icon.source: "qrc:/images/svg/info.svg"
            onTriggered: appMenuBar.aboutRequested()
        }
    }
}
