import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    width: 1600
    height: 900

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        BsOpcUaConnectionForm {
            id: connectionForm

            Layout.preferredHeight: implicitHeight
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: Material.background
            radius: 6
            border.color: Material.primary
            border.width: 2

            Label {
                anchors.centerIn: parent
                visible: !cppManagerOpcUa.connected
                text: qsTr("Connect to an OPC UA endpoint to browse the address space.")
                color: Material.foreground
                opacity: 0.7
            }

            TreeView {
                id: treeView

                anchors.fill: parent
                anchors.margins: 10
                visible: cppManagerOpcUa.connected
                clip: true
                model: cppManagerOpcUa.treeModel

                delegate: TreeViewDelegate {
                    contentItem: Row {
                        spacing: 8

                        Label {
                            text: displayName
                            width: 300
                            elide: Text.ElideRight
                        }

                        Label {
                            text: nodeClassName
                            width: 140
                            elide: Text.ElideRight
                        }

                        Label {
                            text: nodeId
                            width: Math.max(240, treeView.width - 480)
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
