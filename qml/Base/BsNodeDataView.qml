import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQml.Models

/*!
    \qmltype BsNodeDataView
    \inqmlmodule Base
    \brief Center Data Access View table for monitored OPC UA nodes.

    The panel lists every node the user added from the address-space tree, either
    through the tree checkbox, the tree context menu, or by dropping a node onto
    the table. Rows can be sorted by any column, narrowed with a quick filter,
    selected in groups, copied, and exported to CSV; columns can be resized and
    hidden. The value and the sampling interval of a row are edited by
    double-clicking the matching cell. The column layout, the sorting, and the
    per-node sampling intervals are part of the project and are saved with it.
*/
Rectangle {
    id: root

    /*! Number of logical columns provided by DataAccessModel. */
    readonly property int columnCount: 11

    // Column indexes mirroring DataAccessModel::Column. They are written out as
    // integers because the Base module must not import the C++ QML module.
    /*! Index of the value column. */
    readonly property int valueColumn: 2
    /*! Index of the status column. */
    readonly property int statusColumn: 4
    /*! Index of the sampling-interval column. */
    readonly property int intervalColumn: 5

    /*! Default width and visibility per column, used until the project overrides them. */
    readonly property var columnDefaults: [
        { width: 44,  visible: true  },
        { width: 180, visible: true  },
        { width: 150, visible: true  },
        { width: 110, visible: true  },
        { width: 130, visible: true  },
        { width: 96,  visible: true  },
        { width: 170, visible: true  },
        { width: 170, visible: false },
        { width: 200, visible: false },
        { width: 240, visible: true  },
        { width: 160, visible: false }
    ]

    /*! Current per-column visibility; index matches the model column. */
    property var columnVisible: root.columnDefaults.map(function (c) { return c.visible })

    /*! Whether a project layout is currently being applied, suppressing write-back. */
    property bool restoring: false

    /*!
        Serialized layout last exchanged with the project, used to tell a real
        user change from the layout the table already agrees on. Without it the
        table would push its own defaults into a freshly opened project and mark
        it as modified before the user touched anything.
    */
    property string lastPushedState: ""

    /*! Height of the header and each data row. */
    property int rowHeight: 30

    /*! Neutral tint used for inactive header icons and secondary text. */
    readonly property color mutedColor: Qt.rgba(Material.foreground.r,
                                                Material.foreground.g,
                                                Material.foreground.b,
                                                0.45)

    /*! Emitted with the node id and browse path when a row is selected, so the tree can reveal it. */
    signal nodeSelected(string nodeId, string nodePath)

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    // The built-in defaults are the layout the table starts from, so recording
    // them here keeps an untouched table from marking the project as modified.
    Component.onCompleted: root.lastPushedState = JSON.stringify(root.currentState())

    /*! Returns the source-model row behind the view row \a viewRow. */
    function sourceRow(viewRow) {
        if (!cppManagerOpcUa.dataViewModel || viewRow < 0)
            return -1
        return cppManagerOpcUa.dataViewModel.toSourceRow(viewRow)
    }

    /*! Returns the visible column indexes in display order. */
    function visibleColumns() {
        let columns = []
        for (let c = 0; c < root.columnCount; ++c) {
            if (root.columnVisible[c])
                columns.push(c)
        }
        return columns
    }

    /*! Returns every view row currently shown, in display order. */
    function allViewRows() {
        let rows = []
        for (let r = 0; r < tableView.rows; ++r)
            rows.push(r)
        return rows
    }

    /*! Returns the selected view rows in display order, or all rows when nothing is selected. */
    function selectedViewRows() {
        let rows = []
        const indexes = tableSelection.selectedIndexes
        for (let i = 0; i < indexes.length; ++i) {
            const row = indexes[i].row
            if (rows.indexOf(row) === -1)
                rows.push(row)
        }
        rows.sort(function (a, b) { return a - b })
        return rows
    }

    /*! Shows or hides \a column and stores the changed layout in the project. */
    function setColumnVisible(column, visible) {
        let next = root.columnVisible.slice()
        next[column] = visible === true
        root.columnVisible = next
        tableView.forceLayout()
        root.saveState()
    }

    /*! Returns the current column layout and sorting as a plain object. */
    function currentState() {
        let widths = []
        for (let c = 0; c < root.columnCount; ++c) {
            const width = tableView.explicitColumnWidth(c)
            widths.push(width >= 0 ? width : -1)
        }
        const proxy = cppManagerOpcUa.dataViewModel
        return {
            "widths": widths,
            "visible": root.columnVisible.slice(),
            "sortColumn": proxy ? proxy.sortColumn : -1,
            "sortOrder": proxy ? proxy.sortOrder : 0
        }
    }

    /*! Stores the current layout in the project unless a restore is in progress. */
    function saveState() {
        if (root.restoring)
            return
        const state = root.currentState()
        root.lastPushedState = JSON.stringify(state)
        cppManagerOpcUa.dataViewState = state
    }

    /*!
        Applies the column layout and sorting described by \a state, ignoring
        entries that do not match the current column count so a project written
        by a different build cannot corrupt the table.
    */
    function applyState(state) {
        root.restoring = true

        const visible = state ? state.visible : undefined
        if (visible && visible.length === root.columnCount) {
            let next = []
            for (let i = 0; i < root.columnCount; ++i)
                next.push(visible[i] === true)
            root.columnVisible = next
        } else {
            root.columnVisible = root.columnDefaults.map(function (c) { return c.visible })
        }

        const widths = state ? state.widths : undefined
        for (let c = 0; c < root.columnCount; ++c) {
            const width = (widths && widths.length === root.columnCount) ? widths[c] : -1
            tableView.setColumnWidth(c, (width !== undefined && width >= 0) ? width : -1)
        }

        if (cppManagerOpcUa.dataViewModel) {
            const sortColumn = (state && state.sortColumn !== undefined) ? state.sortColumn : -1
            const sortOrder = (state && state.sortOrder !== undefined) ? state.sortOrder : 0
            cppManagerOpcUa.dataViewModel.applySort(sortColumn, sortOrder)
        }

        tableView.forceLayout()
        root.lastPushedState = JSON.stringify(root.currentState())
        root.restoring = false
    }

    /*! Opens the value editor for the view row \a viewRow. */
    function editValue(viewRow) {
        const row = root.sourceRow(viewRow)
        if (row < 0)
            return
        valueEditor.editRow = row
        valueEditor.editField.text = cppManagerOpcUa.dataModel.valueAt(row)
        valueEditor.open()
    }

    /*! Opens the sampling-interval editor for the view row \a viewRow. */
    function editInterval(viewRow) {
        const row = root.sourceRow(viewRow)
        if (row < 0)
            return
        intervalEditor.editRow = row
        intervalEditor.editField.value = cppManagerOpcUa.dataModel.samplingIntervalAt(row)
        intervalEditor.open()
    }

    /*! Removes every selected row from the Data Access View. */
    function removeSelectedRows() {
        const viewRows = root.selectedViewRows()
        let rows = []
        for (let i = 0; i < viewRows.length; ++i) {
            const row = root.sourceRow(viewRows[i])
            if (row >= 0)
                rows.push(row)
        }
        if (rows.length > 0)
            cppManagerOpcUa.removeNodes(rows)
    }

    /*! Copies the selected rows, or all rows when nothing is selected, as text. */
    function copySelectedRows() {
        let viewRows = root.selectedViewRows()
        if (viewRows.length === 0)
            viewRows = root.allViewRows()
        if (viewRows.length === 0)
            return
        cppManagerOpcUa.copyToClipboard(
                    cppManagerOpcUa.dataViewRowsAsText(viewRows, root.visibleColumns()))
    }

    // Restores the table layout whenever a project supplies a new one.
    Connections {
        target: cppManagerOpcUa

        function onDataViewStateChanged() {
            if (root.restoring)
                return
            // Ignore the echo of a layout this table just pushed itself.
            if (JSON.stringify(cppManagerOpcUa.dataViewState) === root.lastPushedState)
                return
            root.applyState(cppManagerOpcUa.dataViewState)
        }
    }

    // Drives the shared node selection from the table's current row.
    Connections {
        target: tableSelection

        function onCurrentChanged(current, previous) {
            if (!current || !current.valid)
                return
            const row = root.sourceRow(current.row)
            if (row < 0)
                return
            cppManagerOpcUa.selectDataRow(row)
            root.nodeSelected(cppManagerOpcUa.dataModel.nodeIdAt(row),
                              cppManagerOpcUa.dataModel.nodePathAt(row))
        }
    }

    // TableView has no "resize finished" signal, so the layout is sampled and
    // written back only when it actually differs from what the project holds.
    Timer {
        interval: 1500
        running: true
        repeat: true
        onTriggered: {
            if (root.restoring)
                return
            const state = root.currentState()
            const serialized = JSON.stringify(state)
            if (serialized === root.lastPushedState)
                return
            root.lastPushedState = serialized
            cppManagerOpcUa.dataViewState = state
        }
    }

    ItemSelectionModel {
        id: tableSelection

        model: cppManagerOpcUa.dataViewModel
    }

    Menu {
        id: columnsMenu

        // Re-read the current visibility every time the menu opens so a layout
        // restored from a project is reflected without rebuilding the items.
        onAboutToShow: {
            for (let i = 0; i < columnsRepeater.count; ++i) {
                const item = columnsRepeater.itemAt(i)
                if (item)
                    item.checked = root.columnVisible[i] === true
            }
        }

        Repeater {
            id: columnsRepeater

            model: root.columnCount

            MenuItem {
                required property int index

                checkable: true
                text: cppManagerOpcUa.dataModel
                      ? cppManagerOpcUa.dataModel.columnTitle(index)
                      : ""
                onToggled: root.setColumnVisible(index, checked)
            }
        }
    }

    Menu {
        id: rowMenu

        /*! View row the menu was opened on; -1 when none. */
        property int viewRow: -1

        MenuItem {
            text: qsTr("Copy Selected Rows")
            onTriggered: root.copySelectedRows()
        }

        MenuItem {
            text: qsTr("Copy Node Id")
            onTriggered: {
                const row = root.sourceRow(rowMenu.viewRow)
                if (row >= 0)
                    cppManagerOpcUa.copyToClipboard(cppManagerOpcUa.dataModel.nodeIdAt(row))
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Write Value…")
            onTriggered: root.editValue(rowMenu.viewRow)
        }

        MenuItem {
            text: qsTr("Set Sampling Interval…")
            onTriggered: root.editInterval(rowMenu.viewRow)
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Remove Selected Rows")
            onTriggered: root.removeSelectedRows()
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
                spacing: 6

                Label {
                    text: qsTr("DATA VIEW")
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: 1.2
                    color: Material.accent
                    verticalAlignment: Text.AlignVCenter
                }

                Item {
                    Layout.fillWidth: true
                }

                TextField {
                    id: filterField

                    Layout.preferredWidth: 200
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumHeight: 28
                    placeholderText: qsTr("Filter rows")
                    font.pixelSize: 12
                    selectByMouse: true
                    onTextChanged: {
                        if (cppManagerOpcUa.dataViewModel)
                            cppManagerOpcUa.dataViewModel.filterText = text
                    }
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    display: AbstractButton.IconOnly
                    icon.source: cppManagerOpcUa.updatesPaused
                                 ? "qrc:/images/svg/play_arrow.svg"
                                 : "qrc:/images/svg/stop.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: cppManagerOpcUa.updatesPaused ? Material.accent : root.mutedColor
                    Accessible.name: qsTr("Pause value updates")
                    ToolTip.visible: hovered
                    ToolTip.text: cppManagerOpcUa.updatesPaused
                                  ? qsTr("Resume value updates")
                                  : qsTr("Freeze the displayed values")
                    onClicked: cppManagerOpcUa.updatesPaused = !cppManagerOpcUa.updatesPaused
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/images/svg/visibility.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: root.mutedColor
                    Accessible.name: qsTr("Choose columns")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Show or hide columns")
                    onClicked: columnsMenu.popup()
                }

                ToolButton {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    display: AbstractButton.IconOnly
                    icon.source: "qrc:/images/svg/save.svg"
                    icon.width: 16
                    icon.height: 16
                    icon.color: root.mutedColor
                    enabled: tableView.rows > 0
                    Accessible.name: qsTr("Export to CSV")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Export the table to CSV")
                    onClicked: exportDialog.open()
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

        HorizontalHeaderView {
            id: headerView

            Layout.fillWidth: true
            Layout.preferredHeight: root.rowHeight
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            Layout.topMargin: 4
            syncView: tableView
            clip: true

            delegate: Rectangle {
                id: headerCell

                required property int column

                /*! Whether the table is currently sorted by this column. */
                readonly property bool sorted:
                    cppManagerOpcUa.dataViewModel
                    && cppManagerOpcUa.dataViewModel.sortColumn === headerCell.column

                implicitHeight: root.rowHeight
                color: Qt.darker(Material.background, 1.1)

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    text: {
                        const title = cppManagerOpcUa.dataModel
                                    ? cppManagerOpcUa.dataModel.columnTitle(headerCell.column)
                                    : ""
                        if (!headerCell.sorted)
                            return title
                        // Sort order 0 is Qt::AscendingOrder.
                        return cppManagerOpcUa.dataViewModel.sortOrder === 0
                                ? title + " ▲"
                                : title + " ▼"
                    }
                    font.bold: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    color: Material.accent
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        if (cppManagerOpcUa.dataViewModel) {
                            cppManagerOpcUa.dataViewModel.toggleSort(headerCell.column)
                            root.saveState()
                        }
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
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: tableView.rows === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: filterField.text.length > 0
                      ? qsTr("No row matches the filter.")
                      : qsTr("Check nodes in the address space, or drag them here, to add them to the Data Access View.")
                color: Material.foreground
                opacity: 0.6
            }

            TableView {
                id: tableView

                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: cppManagerOpcUa.dataViewModel
                boundsBehavior: Flickable.StopAtBounds
                resizableColumns: true
                selectionModel: tableSelection
                selectionBehavior: TableView.SelectRows
                selectionMode: TableView.ExtendedSelection
                rowHeightProvider: function (row) { return root.rowHeight }

                // A hidden column collapses to zero width; a visible one uses the
                // width the user dragged it to, falling back to its default.
                columnWidthProvider: function (column) {
                    if (!root.columnVisible[column])
                        return 0
                    const explicitWidth = explicitColumnWidth(column)
                    if (explicitWidth >= 0)
                        return explicitWidth
                    return root.columnDefaults[column].width
                }

                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}

                Keys.onDeletePressed: root.removeSelectedRows()

                delegate: Rectangle {
                    id: cellDelegate

                    required property int row
                    required property int column
                    required property bool selected
                    required property string display
                    required property string nodeId
                    required property int statusSeverity
                    required property var lastUpdateMs

                    /*! Whether this row has not received a value in this session. */
                    readonly property bool awaitingValue: cellDelegate.lastUpdateMs === 0

                    implicitHeight: root.rowHeight

                    color: {
                        if (cellDelegate.selected)
                            return Qt.lighter(Material.background, 1.5)
                        // Status severity 3 is DataAccessModel::StatusBad.
                        if (cellDelegate.statusSeverity === 3)
                            return Qt.rgba(Material.color(Material.Red).r,
                                           Material.color(Material.Red).g,
                                           Material.color(Material.Red).b, 0.12)
                        return cellDelegate.row % 2 === 0
                               ? "transparent"
                               : Qt.darker(Material.background, 1.05)
                    }

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: cellDelegate.column === 0
                                             ? Text.AlignRight
                                             : Text.AlignLeft
                        text: cellDelegate.display
                        font.bold: cellDelegate.selected
                        elide: Text.ElideRight
                        color: {
                            if (cellDelegate.column === root.statusColumn) {
                                // 1 Good, 2 Uncertain, 3 Bad in DataAccessModel::StatusSeverity.
                                if (cellDelegate.statusSeverity === 1)
                                    return Material.color(Material.Green)
                                if (cellDelegate.statusSeverity === 2)
                                    return Material.color(Material.Amber)
                                if (cellDelegate.statusSeverity === 3)
                                    return Material.color(Material.Red)
                                return root.mutedColor
                            }
                            return Material.foreground
                        }
                        // A row that never received a value is dimmed so it reads
                        // as "nothing arrived yet" rather than as an empty value.
                        opacity: cellDelegate.awaitingValue
                                 && cellDelegate.column !== 0 ? 0.5 : 1.0

                        ToolTip.visible: cellHover.hovered
                                         && cellDelegate.display.length > 0
                                         && contentWidth > width
                        ToolTip.text: cellDelegate.display
                    }

                    HoverHandler {
                        id: cellHover
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: {
                            if (cellDelegate.column === root.valueColumn)
                                root.editValue(cellDelegate.row)
                            else if (cellDelegate.column === root.intervalColumn)
                                root.editInterval(cellDelegate.row)
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            rowMenu.viewRow = cellDelegate.row
                            // Right-clicking outside the selection acts on that row.
                            if (!cellDelegate.selected) {
                                tableSelection.setCurrentIndex(
                                            cppManagerOpcUa.dataViewModel.index(
                                                cellDelegate.row, 0),
                                            ItemSelectionModel.ClearAndSelect
                                            | ItemSelectionModel.Rows)
                            }
                            rowMenu.popup()
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Material.dividerColor
                        opacity: 0.4
                    }
                }
            }

            // Nodes dragged out of the address-space tree are added here.
            DropArea {
                anchors.fill: parent
                keys: ["application/x-opcua-nodeid"]

                onDropped: function (drop) {
                    // The drop source is the address-space tree's drag proxy, which
                    // carries the node id. Its type is only known at run time, so
                    // the property lookup cannot be checked statically.
                    // qmllint disable missing-property
                    const nodeId = drop.source && drop.source.dragNodeId
                                 ? drop.source.dragNodeId
                                 : ""
                    // qmllint enable missing-property
                    if (nodeId.length > 0 && cppManagerOpcUa.monitorNodeById(nodeId))
                        drop.accept(Qt.CopyAction)
                }

                Rectangle {
                    anchors.fill: parent
                    visible: parent.containsDrag
                    color: "transparent"
                    border.width: 2
                    border.color: Material.accent
                    radius: 2
                }
            }
        }
    }

    Dialog {
        id: valueEditor

        /*! Source-model row currently being edited. */
        property int editRow: -1

        /*! Convenience alias to the value input field. */
        property alias editField: valueField

        anchors.centerIn: parent
        width: 360
        modal: true
        title: qsTr("Write value")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            if (editRow >= 0)
                cppManagerOpcUa.writeValue(editRow, valueField.text)
            editRow = -1
        }
        onRejected: editRow = -1

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: qsTr("New value")
                color: Material.foreground
            }

            TextField {
                id: valueField

                Layout.fillWidth: true
                selectByMouse: true
            }
        }
    }

    Dialog {
        id: intervalEditor

        /*! Source-model row currently being edited. */
        property int editRow: -1

        /*! Convenience alias to the interval input field. */
        property alias editField: intervalField

        anchors.centerIn: parent
        width: 380
        modal: true
        title: qsTr("Sampling interval")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            if (editRow >= 0)
                cppManagerOpcUa.setSamplingInterval(editRow, intervalField.value)
            editRow = -1
        }
        onRejected: editRow = -1

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Requested interval in milliseconds. 0 uses the default.")
                color: Material.foreground
                wrapMode: Text.Wrap
            }

            SpinBox {
                id: intervalField

                Layout.fillWidth: true
                from: 0
                to: 600000
                stepSize: 50
                editable: true
            }
        }
    }

    FileDialog {
        id: exportDialog

        title: qsTr("Export Data View")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV files (*.csv)"), qsTr("All files (*)")]

        onAccepted: cppManagerOpcUa.exportDataViewCsv(selectedFile,
                                                      root.allViewRows(),
                                                      root.visibleColumns())
    }
}
