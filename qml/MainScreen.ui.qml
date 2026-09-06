import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Base as Base

/*!
    \qmltype MainScreen
    \inqmlmodule OpcUaManager
    \brief Composes the main menu, top-bar quick actions, and OPC UA browser area.
*/
Pane {
    id: main

    width: 1200
    height: 800
    padding: 0

    /*! Exposes the menu bar so Main can connect to its public signals. */
    property alias menuBar: menuBar

    /*! Exposes the top-bar quick actions so Main can connect to their signals. */
    property alias topActions: topActions

    /*! Exposes the notification banner so Main can feed it. */
    property alias notificationBanner: notificationBanner

    /*! Exposes the log panel so Main can react to its close request. */
    property alias logPanel: logPanel

    /*! Exposes the trend panel so Main can react to its close request. */
    property alias trendPanel: trendPanel

    /*! Whether child controls should follow the dark theme state. */
    property bool darkTheme: false

    /*! Whether the collapsible log panel is shown below the browser. */
    property bool logPanelVisible: false

    /*! Height of the log panel while it is shown. */
    property int logPanelHeight: 200

    /*! Whether the collapsible trend panel is shown below the browser. */
    property bool trendPanelVisible: false

    /*! Height of the trend panel while it is shown. */
    property int trendPanelHeight: 220

    // A column keeps the browser, the banner, and the log panel from overlapping:
    // a hidden layout child takes no space, so no anchor points at an invisible
    // item when the banner or the log panel is collapsed.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Single top row: menu titles on the left, connection indicator and quick
        // actions on the right. Keeping both in one row leaves the browser area intact.
        RowLayout {
            id: topBar

            Layout.fillWidth: true
            spacing: 0

            Base.BsMenuBar {
                id: menuBar

                Layout.fillWidth: true
                darkTheme: main.darkTheme
                logPanelVisible: main.logPanelVisible
                trendPanelVisible: main.trendPanelVisible
            }

            Base.BsTopBarActions {
                id: topActions

                Layout.alignment: Qt.AlignVCenter
                darkTheme: main.darkTheme
            }
        }

        Base.BsNotificationBanner {
            id: notificationBanner

            Layout.fillWidth: true
        }

        Base.BsOpcUaBrowser {
            id: opcUaBrowser

            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Base.BsTrendPanel {
            id: trendPanel

            Layout.fillWidth: true
            Layout.preferredHeight: main.trendPanelHeight
            Layout.minimumHeight: 120
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            visible: main.trendPanelVisible

            plottedNodeIds: opcUaBrowser.selectedNodeIds
            plottedNames: opcUaBrowser.selectedNames
            plottedStepped: opcUaBrowser.selectedStepped
        }

        Base.BsLogPanel {
            id: logPanel

            Layout.fillWidth: true
            Layout.preferredHeight: main.logPanelHeight
            Layout.minimumHeight: 100
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            visible: main.logPanelVisible
        }
    }
}
