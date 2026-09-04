pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype LauncherScreen
    \inqmlmodule OpcUaManager
    \brief Project selection start page shown when no project is active.

    Lists recent projects and offers opening an existing project or creating a new
    one. The project itself is the entry point into the application: no OPC UA
    connection exists until a project is opened here. File selection is delegated to
    the host (Main) via \c openProjectRequested and \c createProjectRequested, while
    recent entries act directly on \c cppProjectManager.
*/
Pane {
    id: launcher

    padding: 0

    /*! Whether child controls should follow the dark theme state. */
    property bool darkTheme: false

    /*!
        \qmlsignal LauncherScreen::openProjectRequested()
        Emitted when the user asks to open an existing project file. The host shows
        a file dialog and calls \c cppProjectManager.openProject().
    */
    signal openProjectRequested()

    /*!
        \qmlsignal LauncherScreen::createProjectRequested()
        Emitted when the user asks to create a new project. The host shows a save
        dialog and calls \c cppProjectManager.createProjectAtPath().
    */
    signal createProjectRequested()

    /*!
        Formats an ISO 8601 \a iso timestamp as a compact local date-time, or
        returns it unchanged when it cannot be parsed.
    */
    function formatTimestamp(iso) {
        if (!iso)
            return ""
        const date = new Date(iso)
        if (isNaN(date.getTime()))
            return iso
        return Qt.formatDateTime(date, "yyyy-MM-dd hh:mm")
    }

    background: Rectangle {
        color: Material.background
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: card.implicitHeight + 80
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: card

            width: Math.min(parent.width - 80, 760)
            anchors.horizontalCenter: parent.horizontalCenter
            y: 40
            spacing: 16

            Label {
                text: qsTr("OPC UA Manager")
                font.pixelSize: 28
                font.bold: true
                color: Material.foreground
            }

            Label {
                text: qsTr("Select a project to begin. A project holds the OPC UA connection and its monitored nodes.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Qt.darker(Material.foreground, 1.0)
                opacity: 0.7
            }

            // Section header, matching the workspace panel style.
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.preferredHeight: 34
                color: Qt.lighter(Material.background, 1.3)

                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("RECENT PROJECTS")
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Material.accent
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Material.dividerColor
                }
            }

            Label {
                visible: cppProjectManager.recentProjects.length === 0
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                text: qsTr("No recent projects. Open an existing project or create a new one.")
                wrapMode: Text.Wrap
                color: Material.foreground
                opacity: 0.6
            }

            // Recent-project list. Each row opens the project on click; a missing
            // file is dimmed and can only be removed from the list.
            Repeater {
                model: cppProjectManager.recentProjects

                delegate: ItemDelegate {
                    id: recentRow

                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: 64
                    enabled: recentRow.modelData.available

                    onClicked: cppProjectManager.openRecent(recentRow.index)

                    contentItem: RowLayout {
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: recentRow.modelData.displayName.length > 0
                                      ? recentRow.modelData.displayName
                                      : recentRow.modelData.path
                                font.pixelSize: 15
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                color: Material.foreground
                            }

                            Label {
                                text: recentRow.modelData.available
                                      ? (recentRow.modelData.endpoint.length > 0
                                         ? recentRow.modelData.endpoint
                                         : recentRow.modelData.path)
                                      : qsTr("Unavailable — %1").arg(recentRow.modelData.path)
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                color: recentRow.modelData.available ? Material.accent
                                                                     : Material.color(Material.Red)
                                opacity: recentRow.modelData.available ? 0.9 : 1.0
                            }
                        }

                        Label {
                            text: launcher.formatTimestamp(recentRow.modelData.lastOpened)
                            font.pixelSize: 12
                            color: Material.foreground
                            opacity: 0.6
                        }

                        ToolButton {
                            text: "✕"
                            flat: true
                            ToolTip.text: qsTr("Remove from list")
                            ToolTip.visible: hovered
                            onClicked: cppProjectManager.removeRecent(recentRow.index)
                        }
                    }
                }
            }

            RowLayout {
                Layout.topMargin: 16
                Layout.fillWidth: true
                spacing: 12

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Open Project…")
                    onClicked: launcher.openProjectRequested()
                }

                Button {
                    text: qsTr("Create New Project")
                    highlighted: true
                    onClicked: launcher.createProjectRequested()
                }
            }
        }
    }
}
