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
    right-click context menu copies node identity to the clipboard, adds nodes to
    the Data Access View, and pins or clears the focus segment. When \l
    searchEnabled is set, the header offers an incremental search that highlights
    matching nodes and steps through them. The same pane is reused for the full
    address space and for the focus segment.
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

    /*! Whether the pane offers the address-space search bar. */
    property bool searchEnabled: false

    /*! Whether the search bar is currently shown. */
    property bool searchVisible: false

    /*! Zero-based position of the current search match; -1 when there is none. */
    property int searchPosition: -1

    /*! Node id of the current search match, used to accent its row. */
    property string currentMatchNodeId: ""

    /*! Height of a single tree row. */
    property int rowHeight: 32

    /*! Index awaiting centering once its row is laid out; null when idle. */
    property var pendingCenterIndex: null

    /*! Remaining centering retries while the target row is not yet laid out. */
    property int pendingCenterAttempts: 0

    /*! Tree index the context menu currently targets; null when none. */
    property var contextIndex: null

    /*! Node id of the context-menu target, captured when the menu opens. */
    property string contextNodeId: ""

    /*! Display name of the context-menu target, captured when the menu opens. */
    property string contextDisplayName: ""

    /*! Whether the context-menu target supports monitoring. */
    property bool contextCanMonitor: false

    /*! Whether the context-menu target is already in the Data Access View. */
    property bool contextMonitored: false

    /*! Whether a search query is currently highlighting nodes in this pane. */
    readonly property bool searchActive:
        root.paneModel ? root.paneModel.searchQuery.length > 0 : false

    /*! Number of loaded nodes matching the current query. */
    readonly property int searchMatchCount:
        root.paneModel ? root.paneModel.searchMatchCount : 0

    /*! Neutral tint used for inactive header icons. */
    readonly property color mutedColor: Qt.rgba(Material.foreground.r,
                                                Material.foreground.g,
                                                Material.foreground.b,
                                                0.4)

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

    /*!
        Centers search match \a position and updates the match counter. The
        position wraps around both ends of the match list, so stepping past the
        last match returns to the first one. Does nothing while the query has no
        matches.
    */
    function goToMatch(position) {
        if (!root.paneModel)
            return
        const count = root.paneModel.searchMatchCount
        if (count <= 0) {
            root.searchPosition = -1
            root.currentMatchNodeId = ""
            return
        }
        const wrapped = ((position % count) + count) % count
        const index = root.paneModel.searchMatchAt(wrapped)
        if (!index || !index.valid)
            return
        root.searchPosition = wrapped
        root.currentMatchNodeId = root.paneModel.nodeIdAt(index)
        root.expandAndCenter(index)
    }

    /*! Shows the search bar and moves the keyboard focus into the input field. */
    function openSearch() {
        if (!root.searchEnabled)
            return
        root.searchVisible = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    /*! Hides the search bar and clears the query so highlighting is removed. */
    function closeSearch() {
        searchField.clear()
        root.searchVisible = false
        root.searchPosition = -1
        root.currentMatchNodeId = ""
    }

    /*!
        Captures the identity of the row at \a row before opening the context
        menu. \a displayName, \a canMonitor, and \a monitored are taken from the
        delegate because the menu is shared by all rows and outlives the tap.
    */
    function openContextMenu(row, displayName, canMonitor, monitored) {
        const index = treeView.index(row, 0)
        root.contextIndex = index
        root.contextNodeId = root.paneModel ? root.paneModel.nodeIdAt(index) : ""
        root.contextDisplayName = displayName
        root.contextCanMonitor = canMonitor
        root.contextMonitored = monitored
        nodeContextMenu.popup()
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

    // Keeps the match counter inside the match list when a browse adds or drops
    // matching nodes. The view is deliberately not scrolled here, so a branch
    // loading in the background never moves the tree under the user.
    Connections {
        target: root.paneModel

        function onSearchMatchesChanged() {
            const count = root.paneModel ? root.paneModel.searchMatchCount : 0
            if (count <= 0) {
                root.searchPosition = -1
                root.currentMatchNodeId = ""
            } else if (root.searchPosition >= count) {
                root.searchPosition = count - 1
            }
        }
    }

    Shortcut {
        sequences: [ StandardKey.Find ]
        enabled: root.searchEnabled
        onActivated: root.openSearch()
    }

    Shortcut {
        sequences: [ StandardKey.FindNext ]
        enabled: root.searchEnabled && root.searchVisible
        onActivated: root.goToMatch(root.searchPosition + 1)
    }

    Shortcut {
        sequences: [ StandardKey.FindPrevious ]
        enabled: root.searchEnabled && root.searchVisible
        onActivated: root.goToMatch(root.searchPosition - 1)
    }

    // Copies the shared selected node id. Disabled while the search field has
    // focus so Ctrl+C keeps its normal meaning inside the text input.
    Shortcut {
        sequences: [ StandardKey.Copy ]
        enabled: root.searchEnabled && !searchField.activeFocus
        onActivated: {
            const nodeId = cppManagerOpcUa.selectedNodeId
            if (nodeId && nodeId.length > 0)
                cppManagerOpcUa.copyToClipboard(nodeId)
        }
    }

    // Context menu shared by all rows; the target row is captured before opening.
    Menu {
        id: nodeContextMenu

        MenuItem {
            text: qsTr("Copy Node Id")
            enabled: root.contextNodeId.length > 0
            onTriggered: cppManagerOpcUa.copyToClipboard(root.contextNodeId)
        }

        MenuItem {
            text: qsTr("Copy Browse Path")
            enabled: root.contextIndex !== null && root.contextIndex.valid
            onTriggered: cppManagerOpcUa.copyToClipboard(
                             cppManagerOpcUa.browsePathAt(root.contextIndex))
        }

        MenuItem {
            text: qsTr("Copy Display Name")
            enabled: root.contextDisplayName.length > 0
            onTriggered: cppManagerOpcUa.copyToClipboard(root.contextDisplayName)
        }

        MenuSeparator {}

        MenuItem {
            text: root.contextMonitored ? qsTr("Remove from Data View")
                                        : qsTr("Add to Data View")
            enabled: root.contextCanMonitor
            onTriggered: {
                if (root.contextIndex && root.contextIndex.valid)
                    cppManagerOpcUa.setNodeMonitored(root.contextIndex,
                                                     !root.contextMonitored)
            }
        }

        MenuItem {
            text: qsTr("Add All Child Variables")
            enabled: root.contextIndex !== null && root.contextIndex.valid
            onTriggered: cppManagerOpcUa.monitorChildVariables(root.contextIndex)
        }

        MenuSeparator {}

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

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 4
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: root.titleText
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Material.accent
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    visible: root.searchEnabled
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/images/svg/search.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: root.searchVisible ? Material.accent : root.mutedColor
                    Accessible.name: qsTr("Find node")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Find node (Ctrl+F)")
                    onClicked: root.searchVisible ? root.closeSearch() : root.openSearch()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Material.dividerColor
            }
        }

        // Incremental search over the nodes already loaded into the tree.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            visible: root.searchEnabled && root.searchVisible
            color: Material.background

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 4

                TextField {
                    id: searchField

                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    placeholderText: qsTr("Find node — use * for wildcards")
                    selectByMouse: true
                    font.pixelSize: 13
                    ToolTip.visible: hovered && !activeFocus
                    ToolTip.text: qsTr("Searches the nodes already loaded. Expand a branch to search deeper.")

                    onTextChanged: {
                        if (root.paneModel)
                            root.paneModel.searchQuery = text
                        root.goToMatch(0)
                    }
                    onAccepted: root.goToMatch(root.searchPosition + 1)
                    Keys.onEscapePressed: root.closeSearch()
                }

                Label {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 4
                    visible: root.searchActive
                    text: root.searchMatchCount > 0
                          ? qsTr("%1 / %2").arg(root.searchPosition + 1)
                                           .arg(root.searchMatchCount)
                          : qsTr("No matches")
                    font.pixelSize: 12
                    color: root.searchMatchCount > 0 ? Material.foreground
                                                     : Material.color(Material.Red)
                    opacity: root.searchMatchCount > 0 ? 0.7 : 1.0
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "↑"
                    font.pixelSize: 14
                    enabled: root.searchMatchCount > 0
                    Accessible.name: qsTr("Previous match")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Previous match (Shift+F3)")
                    onClicked: root.goToMatch(root.searchPosition - 1)
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "↓"
                    font.pixelSize: 14
                    enabled: root.searchMatchCount > 0
                    Accessible.name: qsTr("Next match")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Next match (F3)")
                    onClicked: root.goToMatch(root.searchPosition + 1)
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "✕"
                    font.pixelSize: 13
                    Accessible.name: qsTr("Close search")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Close search (Esc)")
                    onClicked: root.closeSearch()
                }
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

                    /*! Whether this row is the search match currently stepped to. */
                    readonly property bool currentMatch:
                        root.searchActive && searchMatch
                        && nodeId === root.currentMatchNodeId

                    /*! Whether a search is active and this row is not a match. */
                    readonly property bool searchDimmed:
                        root.searchActive && !searchMatch

                    implicitHeight: root.rowHeight

                    // Plain panel background instead of the Material default. The
                    // currently selected node (shared across panels) gets a
                    // lightened highlight; search matches get an accent wash, with
                    // a stronger one on the match the user stepped to.
                    background: Rectangle {
                        color: nodeId === cppManagerOpcUa.selectedNodeId
                               ? Qt.lighter(Material.background, 1.5)
                               : (treeDelegate.currentMatch
                                  ? Qt.rgba(Material.accent.r, Material.accent.g,
                                            Material.accent.b, 0.30)
                                  : (searchMatch && root.searchActive
                                     ? Qt.rgba(Material.accent.r, Material.accent.g,
                                               Material.accent.b, 0.12)
                                     : Material.background))
                    }

                    // Selecting a node loads its attributes into the Attributes panel.
                    onClicked: cppManagerOpcUa.requestAttributes(treeView.index(row, 0))

                    // A right-click opens the context menu for this row.
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openContextMenu(row, displayName,
                                                       canMonitor, monitoringEnabled)
                    }

                    // Invisible 1x1 proxy that follows the cursor during a drag.
                    // Qt Quick hit-tests drop areas against the dragged item's own
                    // position, so the proxy has to move even though the row must
                    // stay where it is.
                    Item {
                        id: dragProxy

                        /*! Node id handed to the drop target. */
                        readonly property string dragNodeId: nodeId

                        width: 1
                        height: 1
                        Drag.active: rowDragHandler.active
                        Drag.keys: ["application/x-opcua-nodeid"]
                        Drag.supportedActions: Qt.CopyAction
                    }

                    // Only monitorable nodes can be dropped into the Data Access View.
                    DragHandler {
                        id: rowDragHandler

                        target: dragProxy
                        enabled: canMonitor
                        onActiveChanged: {
                            if (!active) {
                                dragProxy.x = 0
                                dragProxy.y = 0
                            }
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
                            opacity: treeDelegate.searchDimmed ? 0.35 : 1.0
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: displayName
                            color: Material.foreground
                            opacity: treeDelegate.searchDimmed ? 0.35 : 1.0
                            font.bold: nodeId === cppManagerOpcUa.selectedNodeId
                                       || treeDelegate.currentMatch
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
