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
    the Data Access View, and pins or clears the focus segment. A monitorable
    node can also be dragged into the Data Access View: with the mouse right
    away, and on a touch screen after a long press, so a plain swipe still
    scrolls the tree. A successful drop ticks the node's checkbox. When \l
    searchEnabled is set, the header offers an incremental search that highlights
    matching nodes and steps through them. The same pane is reused for the full
    address space and for the focus segment. When deep branches or long display
    names make the rows wider than the pane, the tree scrolls horizontally.
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

    /*! Row armed for a touch drag by a long press; -1 when none. */
    property int touchDragRow: -1

    /*! Whether a node is currently dragged out of this pane. */
    property bool rowDragActive: false

    /*! Display name of the dragged node, shown by the drag preview. */
    property string rowDragText: ""

    /*! Icon key of the dragged node, shown by the drag preview. */
    property string rowDragIcon: ""

    /*! Pointer position of the drag in window coordinates. */
    property point rowDragPosition: Qt.point(0, 0)

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
        Background of the selected node, shared with the Data Access View rows.

        The dark theme lightens the background. Lightening has no effect on the
        near-white light-theme background, so there a factor below 1 shades the
        row slightly instead.
    */
    readonly property color selectedRowColor:
        Qt.lighter(Material.background, Material.theme === Material.Dark ? 1.5 : 0.9)

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

    /*!
        Starts dragging the node carried by \a proxy out of the pane. \a displayName
        and \a iconKey feed the drag preview that follows the pointer.
    */
    function beginRowDrag(proxy, displayName, iconKey) {
        root.rowDragText = displayName
        root.rowDragIcon = iconKey
        root.rowDragPosition = proxy.mapToItem(null, 0, 0)
        root.rowDragActive = true
        proxy.Drag.active = true
    }

    /*! Moves the drag preview to the current position of \a proxy. */
    function moveRowDrag(proxy) {
        if (root.rowDragActive)
            root.rowDragPosition = proxy.mapToItem(null, 0, 0)
    }

    /*!
        Ends the drag of \a proxy by dropping it at its current position. An
        internal drag only reaches the drop target's \c onDropped through \c
        Drag.drop(); clearing \c Drag.active would merely cancel the drag.
    */
    function endRowDrag(proxy) {
        if (proxy.Drag.active)
            proxy.Drag.drop()
        root.rowDragActive = false
    }

    /*! Abandons the drag of \a proxy without dropping it, e.g. after a grab loss. */
    function cancelRowDrag(proxy) {
        if (proxy.Drag.active)
            proxy.Drag.cancel()
        root.rowDragActive = false
    }

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    // Slim, fully rounded scroll bar used instead of the wide Material default.
    // The handle fades in only while the bar is active.
    component SlimScrollBar: ScrollBar {
        id: slimBar

        implicitWidth: slimBar.orientation === Qt.Vertical ? 8 : 0
        implicitHeight: slimBar.orientation === Qt.Horizontal ? 8 : 0

        contentItem: Rectangle {
            implicitWidth: 6
            implicitHeight: 6
            radius: Math.min(width, height) / 2
            color: slimBar.pressed
                   ? Material.accent
                   : Qt.rgba(Material.foreground.r,
                             Material.foreground.g,
                             Material.foreground.b, 0.4)
            opacity: slimBar.active ? 1.0 : 0.0

            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }
    }

    // Drag preview that follows the pointer while a node is dragged. It lives in
    // the window's content item because this pane clips its children and the
    // drop target is a different panel.
    Rectangle {
        id: rowDragPreview

        parent: root.Window.contentItem ? root.Window.contentItem : root
        visible: root.rowDragActive
        z: 1000
        x: root.rowDragPosition.x + 12
        y: root.rowDragPosition.y + 12
        width: previewRow.implicitWidth + 16
        height: 28
        radius: 4
        color: Qt.lighter(Material.background, 1.3)
        border.color: Material.accent
        border.width: 1
        opacity: 0.92

        Row {
            id: previewRow

            anchors.centerIn: parent
            spacing: 6

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                source: root.rowDragActive ? root.iconSource(root.rowDragIcon) : ""
                sourceSize.width: 16
                sourceSize.height: 16
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.rowDragText
                color: Material.foreground
                font.pixelSize: 12
            }
        }
    }

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

                objectName: "addressSpaceTree"
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.paneModel
                boundsBehavior: Flickable.StopAtBounds

                // A row armed for a touch drag must not flick the tree away.
                interactive: root.touchDragRow < 0

                // The tree model exposes four logical columns (Name/Value/Type/
                // NodeId). The address space only needs the name column, so hide
                // the rest by returning 0. Column 0 fills the view but grows to
                // the widest loaded row (indentation included), which makes the
                // tree scroll horizontally instead of eliding long names.
                columnWidthProvider: function (column) {
                    return column === 0 ? Math.max(width, implicitColumnWidth(0)) : 0
                }

                // TableView keeps column widths while rows load, so re-measure
                // the name column whenever the visible content may have changed.
                onWidthChanged: Qt.callLater(forceLayout)
                onRowsChanged: Qt.callLater(forceLayout)
                onContentYChanged: relayoutTimer.restart()

                ScrollBar.vertical: SlimScrollBar {}
                ScrollBar.horizontal: SlimScrollBar {}

                // Re-measures once vertical scrolling settles, so rows scrolled
                // into view widen the column without a relayout on every frame.
                Timer {
                    id: relayoutTimer

                    interval: 150
                    onTriggered: treeView.forceLayout()
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

                    /*! Whether a touch long press armed this row for dragging. */
                    readonly property bool touchDragArmed:
                        root.touchDragRow >= 0 && root.touchDragRow === row

                    implicitHeight: root.rowHeight

                    // Plain panel background instead of the Material default. A row
                    // armed for a touch drag gets a strong accent wash; the
                    // currently selected node (shared across panels) gets a
                    // lightened highlight; search matches get an accent wash, with
                    // a stronger one on the match the user stepped to.
                    background: Rectangle {
                        color: treeDelegate.touchDragArmed
                               ? Qt.rgba(Material.accent.r, Material.accent.g,
                                         Material.accent.b, 0.30)
                               : nodeId === cppManagerOpcUa.selectedNodeId
                               ? root.selectedRowColor
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

                    // Invisible 1x1 proxy that follows the pointer during a drag.
                    // Qt Quick hit-tests drop areas against the dragged item's own
                    // position, so the proxy has to move even though the row must
                    // stay where it is. The drag is driven imperatively by
                    // beginRowDrag()/endRowDrag(): an internal drag only delivers
                    // the drop when Drag.drop() is called.
                    Item {
                        id: dragProxy

                        /*! Node id handed to the drop target. */
                        readonly property string dragNodeId: nodeId

                        /*! Whether the node is already in the Data Access View. */
                        readonly property bool dragMonitored: monitoringEnabled

                        x: mouseDragHandler.active ? mouseDragHandler.centroid.position.x
                                                   : touchDragHandler.centroid.position.x
                        y: mouseDragHandler.active ? mouseDragHandler.centroid.position.y
                                                   : touchDragHandler.centroid.position.y
                        width: 1
                        height: 1
                        Drag.keys: ["application/x-opcua-nodeid"]
                        Drag.supportedActions: Qt.CopyAction

                        onXChanged: root.moveRowDrag(dragProxy)
                        onYChanged: root.moveRowDrag(dragProxy)
                    }

                    // Mouse, touchpad, and stylus drag a monitorable node as soon
                    // as the pointer moves past the drag threshold.
                    DragHandler {
                        id: mouseDragHandler

                        target: null
                        enabled: canMonitor
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                         | PointerDevice.Stylus
                        cursorShape: Qt.DragCopyCursor
                        onActiveChanged: active ? root.beginRowDrag(dragProxy, displayName,
                                                                    iconName)
                                                : root.endRowDrag(dragProxy)
                        onCanceled: root.cancelRowDrag(dragProxy)
                    }

                    // On a touch screen a plain swipe keeps flicking the tree. A
                    // long press arms the row; only then does the threshold drop
                    // to the normal drag distance so the next move starts a drag.
                    // Until then it sits at 32767, the largest value Qt accepts.
                    DragHandler {
                        id: touchDragHandler

                        target: null
                        enabled: canMonitor
                        acceptedDevices: PointerDevice.TouchScreen
                        dragThreshold: treeDelegate.touchDragArmed
                                       ? Application.styleHints.startDragDistance
                                       : 32767
                        onActiveChanged: {
                            if (active) {
                                root.beginRowDrag(dragProxy, displayName, iconName)
                            } else {
                                root.endRowDrag(dragProxy)
                                root.touchDragRow = -1
                            }
                        }
                        onCanceled: root.cancelRowDrag(dragProxy)

                        // The handler watches the finger passively from the
                        // press on. Losing that watch without having dragged
                        // means the finger lifted (or the tree took the gesture),
                        // so the armed row is released again.
                        onGrabChanged: function (transition, point) {
                            if (!active && root.touchDragRow === row
                                    && (transition === PointerDevice.UngrabPassive
                                        || transition === PointerDevice.CancelGrabPassive))
                                root.touchDragRow = -1
                        }
                    }

                    // Arms the touch drag after a long press. The tap handler
                    // cannot disarm it: the first move after the long press
                    // already cancels the tap, before the drag has started.
                    TapHandler {
                        acceptedDevices: PointerDevice.TouchScreen
                        enabled: canMonitor
                        onLongPressed: root.touchDragRow = row
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
