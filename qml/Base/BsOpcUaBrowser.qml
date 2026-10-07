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

    /*! Node ids of the rows selected in the Data Access View. */
    property alias selectedNodeIds: dataView.selectedNodeIds

    /*! Display names of the rows selected in the Data Access View. */
    property alias selectedNames: dataView.selectedNames

    /*! Whether each selected row holds a boolean. */
    property alias selectedStepped: dataView.selectedStepped

    /*!
        Returns the layout of all resizable panes as an object that maps a
        layout name to an opaque SplitView state: the column widths of Address
        Space, Data View, and Attributes (\c browserColumns), the address-space
        segment split (\c addressSpaceSegments), and the attribute/value split of
        the Attributes panel (\c attributeSections).
    */
    function saveSplitStates() {
        return {
            browserColumns: columnSplit.saveState(),
            addressSpaceSegments: addressTree.saveSplitState(),
            attributeSections: attributesPanel.saveSplitState()
        }
    }

    /*!
        Restores the pane layout from \a states, an object in the format returned
        by saveSplitStates(). Missing entries keep their default sizes, so a
        partial or empty object is accepted.
    */
    function restoreSplitStates(states) {
        if (!states)
            return
        if (states.browserColumns)
            columnSplit.restoreState(states.browserColumns)
        addressTree.restoreSplitState(states.addressSpaceSegments)
        attributesPanel.restoreSplitState(states.attributeSections)
    }

    SplitView {
        id: columnSplit

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
                         ? BsTheme.splitHandleHoverColor
                         : BsTheme.dividerColor
            }
        }

        BsAddressSpaceTree {
            id: addressTree

            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 220
            SplitView.fillHeight: true
        }

        BsNodeDataView {
            id: dataView

            SplitView.fillWidth: true
            SplitView.minimumWidth: 220
            SplitView.fillHeight: true

            // Selecting a Data View row reveals the same node in the address space.
            onNodeSelected: (nodeId, nodePath) => addressTree.revealNode(nodeId, nodePath)
        }

        BsNodeAttributes {
            id: attributesPanel

            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 240
            SplitView.fillHeight: true
        }
    }
}
