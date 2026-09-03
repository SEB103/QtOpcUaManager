import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsAddressSpaceTreePane
    \inqmlmodule Base
    \brief Displays one OPC UA address-space model as a titled, lazy-loading tree.

    The pane renders \l paneModel as a \c TreeView with a node-class icon, the
    display name, a monitoring checkbox, and a marker on the pinned focus node. A
    right-click context menu pins the node as the focus segment or clears it. The
    same pane is reused for the full address space and for the focus segment.
*/
Rectangle {
    id: root

    /*! Tree model shown by this pane (the main tree model or the focus model). */
    property var paneModel: null

    /*! Uppercase section title shown in the pane header. */
    property string titleText: ""

    /*! Message shown centered when the pane has no content to display. */
    property string emptyText: ""

    /*! Whether the context menu offers the "Clear segment" action. */
    property bool showClearAction: false

    /*! Height of a single tree row. */
    property int rowHeight: 32

    /*! Index awaiting centering once its row is laid out; null when idle. */
    property var pendingCenterIndex: null

    /*! Remaining centering retries while the target row is not yet laid out. */
    property int pendingCenterAttempts: 0

    /*! Tree index the context menu currently targets; null when none. */
    property var contextIndex: null

    /*!
        Returns the icon resource for the model \a key (the \c iconName role).
        The key encodes the node kind and, for variables, the data-type category.
        Each SVG already carries its own fill color, so no runtime tinting is
        needed. Unknown keys fall back to the generic glyph.
    */
    function iconSource(key) {
        switch (key) {
        case "folder": return "qrc:/images/svg/folder.svg"
        case "object": return "qrc:/images/svg/deployed_code.svg"
        case "method": return "qrc:/images/svg/function.svg"
        case "objectType": return "qrc:/images/svg/category.svg"
        case "variableType": return "qrc:/images/svg/category_cyan.svg"
        case "dataType":
        case "var-struct": return "qrc:/images/svg/data_object.svg"
        case "referenceType": return "qrc:/images/svg/share.svg"
        case "view": return "qrc:/images/svg/visibility.svg"
        case "array": return "qrc:/images/svg/data_array.svg"
        case "var-bool": return "qrc:/images/svg/toggle_on.svg"
        case "var-int": return "qrc:/images/svg/tag.svg"
        case "var-uint": return "qrc:/images/svg/tag_indigo.svg"
        case "var-real": return "qrc:/images/svg/tag_cyan.svg"
        case "var-string": return "qrc:/images/svg/text_fields.svg"
        case "var-time": return "qrc:/images/svg/schedule.svg"
        default: return "qrc:/images/svg/variable.svg"
        }
    }

    /*!
        Reveals the node with \a nodeId in the tree. When its branch is already
        loaded the node is expanded and centered immediately; otherwise the lazy
        tree is materialized asynchronously along \a nodePath (a slash-separated
        display-name path) and revealed when \c revealPathReady arrives.
    */
    function revealNode(nodeId, nodePath) {
        if (!nodeId || !root.paneModel)
            return
        const idx = root.paneModel.indexForNodeId(nodeId)
        if (idx && idx.valid) {
            root.expandAndCenter(idx)
            return
        }
        if (!nodePath)
            return
        const segments = nodePath.split("/").filter(function (s) { return s.length > 0 })
        if (segments.length === 0)
            return
        root.paneModel.requestRevealPath(segments, nodeId)
    }

    /*!
        Expands the ancestors of \a index and scrolls it to the vertical center.
        Expanding inserts rows that are laid out asynchronously, so centering is
        deferred and retried through \l tryCenter until the target row exists.
    */
    function expandAndCenter(index) {
        treeView.expandToIndex(index)
        root.pendingCenterIndex = index
        root.pendingCenterAttempts = 10
        Qt.callLater(root.tryCenter)
    }

    /*!
        Scrolls the pending index to the vertical center once its row is laid out,
        forcing a layout and retrying a bounded number of times while the row is
        not yet available (\c rowAtIndex returns -1 for not-yet-created rows).
    */
    function tryCenter() {
        const index = root.pendingCenterIndex
        if (!index || !index.valid)
            return
        treeView.forceLayout()
        const row = treeView.rowAtIndex(index)
        if (row >= 0) {
            treeView.positionViewAtRow(row, TableView.AlignVCenter)
            root.pendingCenterIndex = null
            return
        }
        if (root.pendingCenterAttempts > 0) {
            root.pendingCenterAttempts -= 1
            Qt.callLater(root.tryCenter)
        } else {
            root.pendingCenterIndex = null
        }
    }

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    // Completes an asynchronous requestRevealPath() once the target is loaded.
    Connections {
        target: root.paneModel

        function onRevealPathReady(index) {
            if (index && index.valid)
                root.expandAndCenter(index)
        }
    }

    // Context menu shared by all rows; the target index is stored before opening.
    Menu {
        id: nodeContextMenu

        MenuItem {
            text: qsTr("Open as segment")
            onTriggered: {
                if (root.contextIndex && root.contextIndex.valid)
                    cppManagerOpcUa.setFocusNodeFromIndex(root.contextIndex)
            }
        }

        MenuItem {
            text: qsTr("Clear segment")
            visible: root.showClearAction
            height: visible ? implicitHeight : 0
            onTriggered: cppManagerOpcUa.clearFocusNode()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Qt.lighter(Material.background, 1.3)

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                text: root.titleText
                font.pixelSize: 12
                font.bold: true
                font.letterSpacing: 1.2
                color: Material.accent
                elide: Text.ElideRight
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
                visible: root.emptyText.length > 0 && treeView.rows === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: root.emptyText
                color: Material.foreground
                opacity: 0.6
            }

            TreeView {
                id: treeView

                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.paneModel
                boundsBehavior: Flickable.StopAtBounds

                // The tree model exposes four logical columns (Name/Value/Type/
                // NodeId). The address space only needs the name column, so give
                // column 0 the full width and hide the rest by returning 0.
                columnWidthProvider: function (column) {
                    return column === 0 ? width : 0
                }
                onWidthChanged: Qt.callLater(forceLayout)

                // Slim, fully rounded handle instead of the wide Material default.
                ScrollBar.vertical: ScrollBar {
                    id: vScrollBar

                    implicitWidth: 8

                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: width / 2
                        color: vScrollBar.pressed
                               ? Material.accent
                               : Qt.rgba(Material.foreground.r,
                                         Material.foreground.g,
                                         Material.foreground.b, 0.4)
                        opacity: vScrollBar.active ? 1.0 : 0.0

                        Behavior on opacity {
                            NumberAnimation { duration: 150 }
                        }
                    }
                }

                delegate: TreeViewDelegate {
                    id: treeDelegate

                    implicitHeight: root.rowHeight

                    // Plain panel background instead of the Material default. The
                    // currently selected node (shared across panels) gets a
                    // lightened highlight.
                    background: Rectangle {
                        color: nodeId === cppManagerOpcUa.selectedNodeId
                               ? Qt.lighter(Material.background, 1.5)
                               : Material.background
                    }

                    // Selecting a node loads its attributes into the Attributes panel.
                    onClicked: cppManagerOpcUa.requestAttributes(treeView.index(row, 0))

                    // A right-click opens the context menu for this row.
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            root.contextIndex = treeView.index(row, 0)
                            nodeContextMenu.popup()
                        }
                    }

                    contentItem: RowLayout {
                        spacing: 8

                        // Node/type icon: a colored SVG picked from the icon key.
                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            source: root.iconSource(iconName)
                            sourceSize.width: 18
                            sourceSize.height: 18
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: displayName
                            color: Material.foreground
                            font.bold: nodeId === cppManagerOpcUa.selectedNodeId
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        // Marks the node currently pinned as the focus segment.
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: width / 2
                            color: Material.accent
                            visible: nodeId.length > 0
                                     && nodeId === cppManagerOpcUa.focusNodeId
                        }

                        // Checking a variable node adds it to the Data Access View
                        // and the database; unchecking removes it again.
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
