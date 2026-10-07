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

    /*! Active global UI zoom in percent, shown in the View > Zoom submenu title. */
    property int zoomPercent: 100

    /*! Whether a larger zoom step is available; enables "Zoom In". */
    property bool canZoomIn: true

    /*! Whether a smaller zoom step is available; enables "Zoom Out". */
    property bool canZoomOut: true

    /*!
        \qmlsignal BsMenuBar::zoomInRequested()
        Emitted when the user selects View > Zoom > Zoom In. The corresponding
        handler is \c onZoomInRequested.
    */
    signal zoomInRequested()

    /*!
        \qmlsignal BsMenuBar::zoomOutRequested()
        Emitted when the user selects View > Zoom > Zoom Out. The corresponding
        handler is \c onZoomOutRequested.
    */
    signal zoomOutRequested()

    /*!
        \qmlsignal BsMenuBar::zoomResetRequested()
        Emitted when the user resets the zoom to 100 % from View > Zoom. The
        corresponding handler is \c onZoomResetRequested.
    */
    signal zoomResetRequested()

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

    background: Rectangle {
        implicitHeight: 40
        color: BsTheme.menuBandColor
    }

    // A highlighted title gets a light accent fill; the title whose menu is open
    // also gets a thin accent underline.
    delegate: MenuBarItem {
        id: menuBarItem

        background: Rectangle {
            implicitWidth: 40
            implicitHeight: 40
            color: menuBarItem.highlighted ? BsTheme.menuHighlightColor : "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                height: 2
                radius: 1
                color: BsTheme.accentLineColor
                visible: menuBarItem.menu !== null && menuBarItem.menu.visible
            }
        }
    }

    Menu {
        title: qsTr("Application")
        // The entries of submenus are created from this delegate.
        delegate: BsMenuItem {}

        BsMenuItem {
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

            BsMenuItem {
                text: cppManagerOpcUa.connected ? qsTr("Disconnect") : qsTr("Connect")
                icon.source: cppManagerOpcUa.connected ? "qrc:/images/svg/link_off.svg"
                                                       : "qrc:/images/svg/link.svg"
                enabled: !cppManagerOpcUa.busy
                onTriggered: appMenuBar.apiServerConnectionRequested()
            }

            BsMenuItem {
                text: qsTr("Connect to Last Server")
                icon.source: "qrc:/images/svg/replay.svg"
                enabled: !cppManagerOpcUa.busy
                         && !cppManagerOpcUa.connected
                         && cppManagerOpcUa.hasLastConnection
                onTriggered: appMenuBar.lastConnectionRequested()
            }
        }

        BsMenuItem {
            text: qsTr("Sta&rt/Stop")
            icon.source: "qrc:/images/svg/power_settings_new.svg"
            enabled: false
        }

        MenuSeparator {}

        BsMenuItem {
            text: qsTr("Server &Manager")
            icon.source: "qrc:/images/svg/dns.svg"
            onTriggered: appMenuBar.serverManagerRequested()
        }

        MenuSeparator {}

        BsMenuItem {
            text: qsTr("&Settings…")
            icon.source: "qrc:/images/svg/settings.svg"
            onTriggered: appMenuBar.settingsRequested()
        }

        MenuSeparator {}

        BsMenuItem {
            text: qsTr("&Quit")
            icon.source: "qrc:/images/svg/exit_to_app.svg"
            onTriggered: appMenuBar.quitRequested()
        }
    }

    Menu {
        title: qsTr("Project")
        // The entries of submenus are created from this delegate.
        delegate: BsMenuItem {}

        BsMenuItem {
            text: qsTr("&New Project…")
            icon.source: "qrc:/images/svg/note_add.svg"
            onTriggered: appMenuBar.newProjectRequested()
        }

        BsMenuItem {
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

                delegate: BsMenuItem {
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

        BsMenuItem {
            text: qsTr("&Save")
            icon.source: "qrc:/images/svg/save.svg"
            enabled: cppProjectManager.hasActiveProject && cppProjectManager.dirty
            onTriggered: appMenuBar.saveProjectRequested()
        }

        BsMenuItem {
            text: qsTr("Save &As…")
            icon.source: "qrc:/images/svg/save_as.svg"
            enabled: cppProjectManager.hasActiveProject
            onTriggered: appMenuBar.saveProjectAsRequested()
        }

        MenuSeparator {}

        BsMenuItem {
            text: qsTr("&Close Project")
            icon.source: "qrc:/images/svg/close.svg"
            enabled: cppProjectManager.hasActiveProject
            onTriggered: appMenuBar.closeProjectRequested()
        }

        MenuSeparator {}

        BsMenuItem {
            text: qsTr("Clone to Server Studio")
            icon.source: "qrc:/images/svg/content_copy.svg"
            enabled: cppManagerOpcUa.connected
            onTriggered: appMenuBar.cloneToServerStudioRequested()
        }
    }

    Menu {
        title: qsTr("View")
        // The entries of submenus are created from this delegate.
        delegate: BsMenuItem {}

        BsMenuItem {
            text: appMenuBar.darkTheme
                  ? qsTr("Switch to &Light Theme")
                  : qsTr("Switch to &Dark Theme")
            icon.source: appMenuBar.darkTheme ? "qrc:/images/svg/light_mode.svg"
                                              : "qrc:/images/svg/dark_mode.svg"
            onTriggered: appMenuBar.themeToggleRequested()
        }

        BsMenuItem {
            text: appMenuBar.trendPanelVisible ? qsTr("Hide &Trend Panel")
                                               : qsTr("Show &Trend Panel")
            icon.source: "qrc:/images/svg/show_chart.svg"
            onTriggered: appMenuBar.trendPanelToggleRequested()
        }

        BsMenuItem {
            text: appMenuBar.logPanelVisible ? qsTr("Hide &Log Panel")
                                             : qsTr("Show &Log Panel")
            icon.source: "qrc:/images/svg/article.svg"
            onTriggered: appMenuBar.logPanelToggleRequested()
        }

        MenuSeparator {}

        // Global UI zoom. The same actions are bound to Ctrl++, Ctrl+- and Ctrl+0
        // in the main window; the hints name those shortcuts. The menu is wider
        // than the style's 200 px so the hints are not elided in any language.
        Menu {
            width: 280
            title: qsTr("&Zoom (%1%)").arg(appMenuBar.zoomPercent)
            icon.source: "qrc:/images/svg/search.svg"
            icon.width: 24
            icon.height: 24

            BsMenuItem {
                text: qsTr("Zoom &In") + "    Ctrl++"
                enabled: appMenuBar.canZoomIn
                onTriggered: appMenuBar.zoomInRequested()
            }

            BsMenuItem {
                text: qsTr("Zoom &Out") + "    Ctrl+-"
                enabled: appMenuBar.canZoomOut
                onTriggered: appMenuBar.zoomOutRequested()
            }

            BsMenuItem {
                text: qsTr("&Reset to 100%") + "    Ctrl+0"
                enabled: appMenuBar.zoomPercent !== 100
                onTriggered: appMenuBar.zoomResetRequested()
            }
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
            BsMenuItem {
                text: qsTr("JSON")
                checkable: true
                ActionGroup.group: valueFormatGroup
                checked: cppManagerOpcUa.valueFormat === 0
                onTriggered: cppManagerOpcUa.valueFormat = 0
            }

            BsMenuItem {
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

            BsMenuItem {
                text: qsTr("&Main Toolbar")
                checkable: true
            }
        }
    }

    Menu {
        title: qsTr("Info")

        BsMenuItem {
            text: qsTr("&Documentation")
            icon.source: "qrc:/images/svg/menu_book.svg"
            onTriggered: appMenuBar.helpRequested()
        }

        BsMenuItem {
            text: qsTr("&Check for updates…")
            icon.source: "qrc:/images/svg/update.svg"
            onTriggered: appMenuBar.checkForUpdatesRequested()
        }

        MenuSeparator {}

        BsMenuItem {
            text: qsTr("&About OpcUaManager…")
            icon.source: "qrc:/images/svg/info.svg"
            onTriggered: appMenuBar.aboutRequested()
        }
    }
}
