import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsAddressSpaceTree
    \inqmlmodule Base
    \brief Displays the OPC UA server address space as a titled, flexible tree.

    The panel reads \c cppManagerOpcUa.treeModel from the QML context and shows a
    lazy-loading TreeView only while the OPC UA session is connected. Each row is
    rendered as a node-class color badge followed by the localized display name.
*/
Rectangle {
    id: root

    /*! Height of a single tree row. */
    property int rowHeight: 32

    /*!
        Returns the badge color for the numeric QOpcUa::NodeClass \a nodeClass.
        Unknown or undefined classes fall back to a neutral grey.
    */
    function badgeColor(nodeClass) {
        switch (nodeClass) {
        case 1: return Material.color(Material.Blue)       // Object
        case 2: return Material.color(Material.Amber)      // Variable
        case 4: return Material.color(Material.Purple)     // Method
        case 8: return Material.color(Material.Teal)       // ObjectType
        case 16: return Material.color(Material.Cyan)      // VariableType
        case 32: return Material.color(Material.Green)     // DataType
        case 64: return Material.color(Material.Orange)    // ReferenceType
        case 128: return Material.color(Material.LightBlue) // View
        default: return Material.color(Material.Grey)
        }
    }

    color: Material.background
    radius: 6
    border.color: Material.primary
    border.width: 1
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "transparent"

            Label {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                text: qsTr("ADDRESS SPACE")
                font.pixelSize: 12
                font.bold: true
                font.letterSpacing: 1.2
                color: Material.accent
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Material.dividerColor
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: !cppManagerOpcUa.connected
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Connect to an OPC UA endpoint to browse the address space.")
                color: Material.foreground
                opacity: 0.6
            }

            TreeView {
                id: treeView

                anchors.fill: parent
                anchors.margins: 4
                visible: cppManagerOpcUa.connected
                clip: true
                model: cppManagerOpcUa.treeModel
                boundsBehavior: Flickable.StopAtBounds

                // The tree model exposes four logical columns (Name/Value/Type/
                // NodeId). The address space only needs the name column, so give
                // column 0 the full width and hide the rest by returning 0.
                columnWidthProvider: function (column) {
                    return column === 0 ? width : 0
                }
                onWidthChanged: Qt.callLater(forceLayout)

                ScrollBar.vertical: ScrollBar {}

                delegate: TreeViewDelegate {
                    id: treeDelegate

                    implicitHeight: root.rowHeight

                    // Selecting a node loads its attributes into the Attributes panel.
                    onClicked: cppManagerOpcUa.requestAttributes(treeView.index(row, 0))

                    contentItem: RowLayout {
                        spacing: 8

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 3
                            color: root.badgeColor(nodeClass)
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: displayName
                            color: Material.foreground
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        // Checking a variable node adds it to the Data Access View and
                        // the database; unchecking removes it again.
                        CheckBox {
                            Layout.alignment: Qt.AlignVCenter
                            visible: canMonitor
                            checked: monitoringEnabled
                            onToggled: cppManagerOpcUa.setNodeMonitored(treeView.index(row, 0),
                                                                        checked)
                        }
                    }
                }
            }
        }
    }
}
