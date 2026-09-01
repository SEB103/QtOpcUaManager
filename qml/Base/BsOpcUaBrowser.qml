import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

/*!
    \qmltype BsOpcUaBrowser
    \inqmlmodule Base
    \brief Hosts the two-zone OPC UA browsing area.

    The component splits its area into a resizable left \l BsAddressSpaceTree
    panel, a center \l BsNodeDataView table, and a right \l BsNodeAttributes
    panel. Connecting to a server is handled separately through the application
    menu dialog.
*/
Item {
    id: root

    width: 1600
    height: 900

    SplitView {
        anchors.fill: parent
        anchors.margins: 8
        orientation: Qt.Horizontal

        handle: Rectangle {
            implicitWidth: 8
            color: "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: SplitHandle.pressed ? 3 : 2
                height: parent.height
                radius: 1
                color: SplitHandle.pressed
                       ? Material.accent
                       : SplitHandle.hovered
                         ? Qt.lighter(Material.dividerColor, 1.6)
                         : Material.dividerColor
            }
        }

        BsAddressSpaceTree {
            id: addressTree

            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 220
            SplitView.fillHeight: true
        }

        BsNodeDataView {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 220
            SplitView.fillHeight: true

            // Selecting a Data View row reveals the same node in the address space.
            onNodeSelected: (nodeId, nodePath) => addressTree.revealNode(nodeId, nodePath)
        }

        BsNodeAttributes {
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 240
            SplitView.fillHeight: true
        }
    }
}
