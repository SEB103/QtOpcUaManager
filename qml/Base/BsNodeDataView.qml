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

    // Writability values mirroring DataAccessModel::Writability.
    /*! Writability value meaning the server has not reported an AccessLevel. */
    readonly property int writabilityUnknown: 0
    /*! Writability value meaning the value can be written. */
    readonly property int writabilityWritable: 1
    /*! Writability value meaning the server withholds CurrentWrite. */
    readonly property int writabilityReadOnly: 2

    /*!
        Accepted ranges of the numeric types the address space reports.

        Both vocabularies occur: the tree names types in IEC 61131 terms while a
        live value update names the OPC UA built-in type, so both spellings map
        to the same range. \c wide marks the 64-bit types, whose bounds cannot be
        represented exactly in JavaScript, so only their digits are checked.
    */
    readonly property var numericTypes: ({
        "SINT":    { min: -128, max: 127 },
        "SBYTE":   { min: -128, max: 127 },
        "BYTE":    { min: 0, max: 255 },
        "USINT":   { min: 0, max: 255 },
        "INT":     { min: -32768, max: 32767 },
        "INT16":   { min: -32768, max: 32767 },
        "UINT":    { min: 0, max: 65535 },
        "UINT16":  { min: 0, max: 65535 },
        "DINT":    { min: -2147483648, max: 2147483647 },
        "INT32":   { min: -2147483648, max: 2147483647 },
        "UDINT":   { min: 0, max: 4294967295 },
        "UINT32":  { min: 0, max: 4294967295 },
        "LINT":    { wide: true },
        "INT64":   { wide: true },
        "ULINT":   { wide: true, unsigned: true },
        "UINT64":  { wide: true, unsigned: true },
        "REAL":    { real: true },
        "FLOAT":   { real: true },
        "LREAL":   { real: true },
        "DOUBLE":  { real: true }
    })

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

    /*! View row currently under the pointer, or -1; drives the hover delete button. */
    property int hoveredViewRow: -1

    /*! Top of the hovered row within the table pane, positioning the delete button. */
    property real hoveredRowY: 0

    /*! Neutral tint used for inactive header icons and secondary text. */
    readonly property color mutedColor: Qt.rgba(Material.foreground.r,
                                                Material.foreground.g,
                                                Material.foreground.b,
                                                0.45)

    /*! Emitted with the node id and browse path when a row is selected, so the tree can reveal it. */
    signal nodeSelected(string nodeId, string nodePath)

    /*! Node ids of the selected rows, in display order; what the trend plots. */
    property var selectedNodeIds: []

    /*! Display names matching \l selectedNodeIds. */
    property var selectedNames: []

    /*! Whether each of \l selectedNodeIds is a boolean, drawn as a square wave. */
    property var selectedStepped: []

    /*!
        Republishes the selected rows for the trend panel.

        Selection lives in an ItemSelectionModel, which reports a change rather
        than exposing a ready-made list, so the three parallel arrays the plot
        needs are rebuilt whenever it changes.
    */
    function refreshSelection() {
        const viewRows = root.selectedViewRows()
        const model = cppManagerOpcUa.dataModel

        let nodeIds = []
        let names = []
        let stepped = []
        for (let i = 0; i < viewRows.length; ++i) {
            const row = root.sourceRow(viewRows[i])
            if (row < 0)
                continue
            nodeIds.push(model.nodeIdAt(row))
            names.push(model.displayNameAt(row))
            stepped.push(model.isBooleanAt(row))
        }

        root.selectedNodeIds = nodeIds
        root.selectedNames = names
        root.selectedStepped = stepped
    }

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    // The built-in defaults are the layout the table starts from, so recording
    // them here keeps an untouched table from marking the project as modified.
    Component.onCompleted: root.lastPushedState = JSON.stringify(root.currentState())

    /*!
        Returns the editor kind for \a dataType: one of \c bool, \c enum,
        \c integer, \c real, or \c text. \a enumOptions decides the enum case,
        because an enumeration is reported as a plain integer type.
    */
    function editorKindFor(dataType, enumOptions) {
        if (enumOptions && enumOptions.length > 0)
            return "enum"

        const key = String(dataType).toUpperCase()
        if (key === "BOOL" || key === "BOOLEAN")
            return "bool"

        const numeric = root.numericTypes[key]
        if (numeric)
            return numeric.real === true ? "real" : "integer"

        return "text"
    }

    /*! Returns the range descriptor for \a dataType, or null for non-numeric types. */
    function numericRangeFor(dataType) {
        const numeric = root.numericTypes[String(dataType).toUpperCase()]
        return numeric ? numeric : null
    }

    /*!
        Returns whether \a text is an acceptable value of \a dataType.

        Bounds are checked for the types JavaScript can represent exactly. The
        64-bit types are only checked for shape, because their bounds exceed the
        precision of a JavaScript number; the server rejects an out-of-range
        value and the reason is now readable.
    */
    function isValueInRange(dataType, text) {
        const range = root.numericRangeFor(dataType)
        if (!range)
            return true
        if (text.length === 0)
            return false

        if (range.real === true)
            return isFinite(Number(text))

        if (range.wide === true)
            return range.unsigned === true ? /^\d+$/.test(text) : /^[+-]?\d+$/.test(text)

        const value = Number(text)
        return isFinite(value) && value >= range.min && value <= range.max
    }

    /*! Returns a short description of the accepted input for \a dataType. */
    function rangeHintFor(dataType) {
        const range = root.numericRangeFor(dataType)
        if (!range)
            return ""
        if (range.real === true)
            return qsTr("A decimal number, for example 12.5. Use a dot as the separator.")
        if (range.wide === true)
            return qsTr("A whole number. The server checks the exact range of this type.")
        return qsTr("A whole number between %1 and %2.").arg(range.min).arg(range.max)
    }

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

    /*!
        Opens the value editor for the view row \a viewRow, choosing the input
        control from the row's data type and refusing rows the server marks
        read-only.

        Returns whether the editor was opened.
    */
    function editValue(viewRow) {
        const row = root.sourceRow(viewRow)
        if (row < 0)
            return false

        const model = cppManagerOpcUa.dataModel
        if (model.writabilityAt(row) === root.writabilityReadOnly)
            return false

        valueEditor.editRow = row
        valueEditor.editNodeId = model.nodeIdAt(row)
        valueEditor.editDataType = model.dataTypeAt(row)
        valueEditor.editDisplayName = model.displayNameAt(row)
        valueEditor.reset(model.valueAt(row))
        valueEditor.open()
        return true
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

        function onSelectionChanged(selected, deselected) {
            root.refreshSelection()
        }

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
            enabled: {
                const row = root.sourceRow(rowMenu.viewRow)
                return row >= 0
                       && cppManagerOpcUa.dataModel.writabilityAt(row)
                          !== root.writabilityReadOnly
            }
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
            id: tablePane

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
                    required property int writability

                    /*! Whether the server withholds CurrentWrite for this row. */
                    readonly property bool readOnly:
                        cellDelegate.writability === root.writabilityReadOnly

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

                        // A read-only value is shown in italics so the row says
                        // so before the user double-clicks it.
                        font.italic: cellDelegate.readOnly
                                     && cellDelegate.column === root.valueColumn

                        ToolTip.visible: cellHover.hovered
                                         && (cellDelegate.readOnly
                                             ? cellDelegate.column === root.valueColumn
                                             : (cellDelegate.display.length > 0
                                                && contentWidth > width))
                        ToolTip.text: cellDelegate.readOnly
                                      && cellDelegate.column === root.valueColumn
                                      ? qsTr("Read-only: the server does not grant CurrentWrite.")
                                      : cellDelegate.display
                    }

                    HoverHandler {
                        id: cellHover

                        // Any hovered cell publishes its row so the shared delete
                        // button can pin itself to the right edge of that row.
                        onHoveredChanged: {
                            if (hovered) {
                                root.hoveredViewRow = cellDelegate.row
                                root.hoveredRowY = cellDelegate.mapToItem(tablePane, 0, 0).y
                            }
                        }
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

            // Tracks whether the pointer is anywhere over the table pane, so the
            // per-row delete button hides again once the pointer leaves.
            HoverHandler {
                id: tablePaneHover
            }

            // Per-row delete affordance pinned to the right edge of the hovered
            // row. It restores the remove control the ListView table carried
            // before the TableView rebuild, without adding a scrolling column.
            ToolButton {
                id: rowRemoveButton

                width: 26
                height: 24
                padding: 0
                visible: tablePaneHover.hovered
                         && root.hoveredViewRow >= 0
                         && root.hoveredViewRow < tableView.rows
                x: tableView.x + tableView.width - width - 8
                y: root.hoveredRowY + (root.rowHeight - height) / 2
                text: "✕"
                font.pixelSize: 14
                Accessible.name: qsTr("Remove from Data Access View")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Remove from Data Access View")

                // A faint chip keeps the glyph readable over the row's value,
                // turning red while hovered to signal the destructive action.
                background: Rectangle {
                    radius: 4
                    color: rowRemoveButton.hovered
                           ? Qt.rgba(Material.color(Material.Red).r,
                                     Material.color(Material.Red).g,
                                     Material.color(Material.Red).b, 0.22)
                           : Qt.rgba(Material.background.r,
                                     Material.background.g,
                                     Material.background.b, 0.85)
                }

                onClicked: {
                    const row = root.sourceRow(root.hoveredViewRow)
                    root.hoveredViewRow = -1
                    if (row >= 0)
                        cppManagerOpcUa.removeNode(row)
                }
            }
        }
    }

    Dialog {
        id: valueEditor

        objectName: "valueEditorDialog"

        /*! Source-model row currently being edited. */
        property int editRow: -1

        /*! Node id of the row being edited, used to match the selected node. */
        property string editNodeId: ""

        /*! Data-type text of the row being edited. */
        property string editDataType: ""

        /*! Display name of the row being edited, shown in the dialog. */
        property string editDisplayName: ""

        /*!
            Enumeration choices for the edited node.

            The choices come from the attribute read of the selected node, so
            they are only offered while the edited row is the selected one; any
            other row falls back to the numeric editor, which is always correct.
        */
        readonly property var enumOptions:
            valueEditor.editNodeId.length > 0
            && valueEditor.editNodeId === cppManagerOpcUa.selectedNodeId
                ? cppManagerOpcUa.selectedEnumOptions
                : []

        /*! Input control the current data type calls for. */
        readonly property string editorKind:
            root.editorKindFor(valueEditor.editDataType, valueEditor.enumOptions)

        /*! Whether the current input can be written. */
        readonly property bool inputAcceptable: {
            if (valueEditor.editorKind === "bool")
                return true
            if (valueEditor.editorKind === "enum")
                return enumField.currentIndex >= 0
            if (valueEditor.editorKind === "text")
                return true
            return root.isValueInRange(valueEditor.editDataType, valueField.text)
        }

        /*!
            Value handed to the write, typed for its editor.

            Numbers travel as text so a 64-bit value keeps its exact digits: the
            service converts to the node's own type, and a JavaScript number
            would already have lost precision by then.
        */
        readonly property var editedValue: {
            if (valueEditor.editorKind === "bool")
                return boolField.checked
            if (valueEditor.editorKind === "enum")
                return enumField.currentIndex >= 0
                       ? valueEditor.enumOptions[enumField.currentIndex].value
                       : 0
            return valueField.text
        }

        /*! Loads \a currentValue into whichever control the data type calls for. */
        function reset(currentValue) {
            const text = String(currentValue)
            valueField.text = text
            boolField.checked = text.toLowerCase() === "true" || text === "1"

            enumField.currentIndex = -1
            for (let i = 0; i < valueEditor.enumOptions.length; ++i) {
                if (String(valueEditor.enumOptions[i].value) === text) {
                    enumField.currentIndex = i
                    break
                }
            }
            if (enumField.currentIndex < 0 && valueEditor.enumOptions.length > 0)
                enumField.currentIndex = 0
        }

        anchors.centerIn: parent
        width: 420
        modal: true
        title: valueEditor.editDisplayName.length > 0
               ? qsTr("Write %1").arg(valueEditor.editDisplayName)
               : qsTr("Write value")
        standardButtons: Dialog.Ok | Dialog.Cancel

        // The accept path is the gate, not the OK button: the button binding is
        // installed only if standardButton() already has the button, and an
        // accept can also arrive from the keyboard, so an input the node cannot
        // accept must be refused here instead of by the server afterwards.
        onAccepted: {
            if (valueEditor.editRow >= 0 && valueEditor.inputAcceptable)
                cppManagerOpcUa.writeValue(valueEditor.editRow, valueEditor.editedValue)
            valueEditor.editRow = -1
        }
        onRejected: valueEditor.editRow = -1

        // Show the refusal on the button as well, so an unacceptable input is
        // visible before the user reaches for OK.
        Component.onCompleted: {
            const okButton = valueEditor.standardButton(Dialog.Ok)
            if (okButton)
                okButton.enabled = Qt.binding(function () { return valueEditor.inputAcceptable })
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: valueEditor.editDataType.length > 0
                      ? qsTr("New value (%1)").arg(valueEditor.editDataType)
                      : qsTr("New value")
                color: Material.foreground
                elide: Text.ElideRight
            }

            Switch {
                id: boolField

                Layout.fillWidth: true
                visible: valueEditor.editorKind === "bool"
                text: boolField.checked ? qsTr("TRUE") : qsTr("FALSE")
            }

            ComboBox {
                id: enumField

                Layout.fillWidth: true
                visible: valueEditor.editorKind === "enum"
                model: valueEditor.enumOptions
                textRole: "label"
                valueRole: "value"
            }

            TextField {
                id: valueField

                Layout.fillWidth: true
                visible: valueEditor.editorKind !== "bool"
                         && valueEditor.editorKind !== "enum"
                selectByMouse: true
                // Locale-independent on purpose: a validator that follows the
                // locale would accept a comma that the conversion then rejects.
                validator: valueEditor.editorKind === "integer"
                           ? integerValidator
                           : (valueEditor.editorKind === "real" ? realValidator : null)
            }

            Label {
                Layout.fillWidth: true
                visible: valueEditor.editorKind === "integer"
                         || valueEditor.editorKind === "real"
                text: root.rangeHintFor(valueEditor.editDataType)
                color: valueEditor.inputAcceptable ? root.mutedColor
                                                   : Material.color(Material.Red)
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
    }

    RegularExpressionValidator {
        id: integerValidator

        regularExpression: /^[+-]?\d*$/
    }

    RegularExpressionValidator {
        id: realValidator

        regularExpression: /^[+-]?\d*[.]?\d*([eE][+-]?\d+)?$/
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
