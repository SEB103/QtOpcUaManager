import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

/*!
    \qmltype BsAddressSpaceTree
    \inqmlmodule Base
    \brief Shows the OPC UA address space as two stacked, resizable segments.

    The top segment browses the full server address space from \c
    cppManagerOpcUa.treeModel. The bottom segment is rooted at the pinned focus
    node (\c cppManagerOpcUa.focusModel) so a frequently used container node can be
    opened in isolation; it is shown only while a focus node is pinned and the
    session is connected. A node is pinned through each segment's right-click menu.
*/
Item {
    id: root

    /*!
        Reveals the node with \a nodeId in the full address-space segment, using
        the slash-separated \a nodePath to materialize a collapsed branch when the
        node is not loaded yet. Delegates to the top segment so cross-panel reveal
        from the Data Access View keeps working.
    */
    function revealNode(nodeId, nodePath) {
        fullTreePane.revealNode(nodeId, nodePath)
    }

    /*!
        Returns the size of the address-space and focus-node segments as an
        opaque SplitView state for restoreSplitState().
    */
    function saveSplitState() {
        return segmentSplit.saveState()
    }

    /*!
        Restores the segment sizes from \a state, previously returned by
        saveSplitState(). Returns \c false for a missing or unusable state, which
        keeps the default sizes. The focus-node segment keeps its restored size
        while it is hidden and uses it when a focus node is pinned.
    */
    function restoreSplitState(state) {
        return state ? segmentSplit.restoreState(state) : false
    }

    SplitView {
        id: segmentSplit

        anchors.fill: parent
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: 8
            color: "transparent"

            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: SplitHandle.pressed ? 3 : 2
                radius: 1
                color: SplitHandle.pressed
                       ? Material.accent
                       : SplitHandle.hovered
                         ? BsTheme.splitHandleHoverColor
                         : BsTheme.dividerColor
            }
        }

        BsAddressSpaceTreePane {
            id: fullTreePane

            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumHeight: 120

            paneModel: cppManagerOpcUa.treeModel
            titleText: qsTr("ADDRESS SPACE")
            searchEnabled: true
            emptyText: cppManagerOpcUa.connected
                       ? ""
                       : qsTr("Connect to an OPC UA endpoint to browse the address space.")
        }

        BsAddressSpaceTreePane {
            id: focusTreePane

            SplitView.fillWidth: true
            SplitView.preferredHeight: 220
            SplitView.minimumHeight: 100

            visible: cppManagerOpcUa.connected
                     && cppManagerOpcUa.focusNodeId.length > 0

            paneModel: cppManagerOpcUa.focusModel
            titleText: cppManagerOpcUa.focusNodeName.length > 0
                       ? cppManagerOpcUa.focusNodeName.toUpperCase()
                       : qsTr("FOCUS NODE")
            emptyText: qsTr("Right-click a node and choose \"Open as segment\".")
            showClearAction: true
        }
    }
}
