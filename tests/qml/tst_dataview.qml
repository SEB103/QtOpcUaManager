import QtQuick
import QtQuick.Controls.Material
import QtTest
import Base

/*!
    \qmltype tst_dataview
    \brief Hosts behavior tests for the Data Access View table.

    The cases run against the real DataAccessModel and DataViewFilterModel that
    the test mock exposes, so column metadata and the persisted layout state are
    exercised the same way the application uses them.
*/
Item {
    id: root

    width: 1000
    height: 700

    Component {
        id: dataViewComponent

        BsNodeDataView {
            width: 800
            height: 400
        }
    }

    // A parent item that carries the Material theme, as the application window
    // does, so the table inherits the theme instead of setting it itself.
    Component {
        id: themedDataViewComponent

        Item {
            width: 800
            height: 400

            BsNodeDataView {
                anchors.fill: parent
            }
        }
    }

    // The table next to the address-space tree under one themed parent, as in
    // the browser layout, to compare their selection colors.
    Component {
        id: tableAndTreeComponent

        Item {
            property alias table: themedTable
            property alias tree: themedTree

            width: 800
            height: 400

            BsNodeDataView {
                id: themedTable

                width: 400
                height: 400
            }

            BsAddressSpaceTreePane {
                id: themedTree

                x: 400
                width: 400
                height: 400
            }
        }
    }

    TestCase {
        id: testCase

        /*! Test case for Data Access View column metadata and layout persistence. */
        name: "DataViewTable"
        when: windowShown

        /*! Verifies that the table exposes every model column with sane defaults. */
        function test_defaultColumnsMatchTheModel() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            compare(view.columnVisible.length, view.columnCount);

            // The QML column count must agree with the C++ model: the last column
            // has a title and the one past it does not.
            const model = cppManagerOpcUa.dataModel;
            verify(model.columnTitle(view.columnCount - 1).length > 0);
            compare(model.columnTitle(view.columnCount), "");

            // Node path, server timestamp, and server start hidden so the columns
            // a commissioning engineer reads are visible without scrolling.
            const visible = view.visibleColumns();
            verify(visible.indexOf(0) !== -1);
            verify(visible.indexOf(view.valueColumn) !== -1);
            verify(visible.indexOf(view.statusColumn) !== -1);
            verify(visible.indexOf(view.intervalColumn) !== -1);
            verify(visible.length < view.columnCount);
        }

        /*! Verifies that the column titles come from the C++ model. */
        function test_columnTitlesComeFromTheModel() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            const model = cppManagerOpcUa.dataModel;
            verify(model.columnTitle(view.valueColumn).length > 0);
            verify(model.columnTitle(view.statusColumn).length > 0);
            verify(model.columnTitle(view.intervalColumn).length > 0);
        }

        /*! Verifies that hiding and showing a column updates the visible set. */
        function test_columnVisibilityToggles() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            verify(view.visibleColumns().indexOf(view.valueColumn) !== -1);

            view.setColumnVisible(view.valueColumn, false);
            compare(view.visibleColumns().indexOf(view.valueColumn), -1);

            view.setColumnVisible(view.valueColumn, true);
            verify(view.visibleColumns().indexOf(view.valueColumn) !== -1);
        }

        /*! Verifies that a stored layout is restored and that restoring is silent. */
        function test_layoutStateRoundTrip() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            view.setColumnVisible(view.statusColumn, false);
            const saved = view.currentState();
            compare(saved.visible.length, view.columnCount);
            compare(saved.visible[view.statusColumn], false);

            // Bring the column back, then restore the captured layout.
            view.setColumnVisible(view.statusColumn, true);
            verify(view.visibleColumns().indexOf(view.statusColumn) !== -1);

            view.applyState(saved);
            compare(view.visibleColumns().indexOf(view.statusColumn), -1);
            compare(view.restoring, false);
        }

        /*! Verifies that a layout with a mismatched column count falls back to defaults. */
        function test_layoutStateFromAnotherBuildIsIgnored() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            view.setColumnVisible(0, false);
            view.applyState({ "visible": [true, false], "widths": [10, 20] });

            // The mismatched arrays are discarded, so column 0 is visible again.
            verify(view.visibleColumns().indexOf(0) !== -1);
        }

        /*! Verifies that an empty table reports no rows to export or copy. */
        function test_emptyTableHasNoRows() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            compare(view.allViewRows().length, 0);
            compare(view.selectedViewRows().length, 0);
        }

        /*! Verifies that the editor control follows the data type of the row. */
        function test_editorKindFollowsTheDataType() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            // Both vocabularies occur: the tree names IEC types, a live value
            // update names the OPC UA built-in type.
            compare(view.editorKindFor("BOOL", []), "bool");
            compare(view.editorKindFor("Boolean", []), "bool");
            compare(view.editorKindFor("DINT", []), "integer");
            compare(view.editorKindFor("Int32", []), "integer");
            compare(view.editorKindFor("uint", []), "integer");
            compare(view.editorKindFor("LREAL", []), "real");
            compare(view.editorKindFor("Double", []), "real");
            compare(view.editorKindFor("STRING", []), "text");
            compare(view.editorKindFor("", []), "text");

            // An enumeration is reported as an integer, so the choices decide.
            compare(view.editorKindFor("Int32", [{ value: 0, label: "Manual" }]), "enum");
        }

        /*! Verifies that a value outside the range of its type is refused. */
        function test_numericInputIsRangeChecked() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            verify(view.isValueInRange("SINT", "127"));
            verify(!view.isValueInRange("SINT", "128"));
            verify(view.isValueInRange("SINT", "-128"));
            verify(!view.isValueInRange("SINT", "-129"));

            verify(view.isValueInRange("BYTE", "0"));
            verify(!view.isValueInRange("BYTE", "-1"));
            verify(view.isValueInRange("UINT", "65535"));
            verify(!view.isValueInRange("UINT", "65536"));

            // Empty input is never a value.
            verify(!view.isValueInRange("DINT", ""));

            // Reals accept a decimal point and exponents.
            verify(view.isValueInRange("LREAL", "12.5"));
            verify(view.isValueInRange("REAL", "-1e3"));
            verify(!view.isValueInRange("REAL", "abc"));

            // 64-bit bounds exceed the precision of a JavaScript number, so only
            // the shape is checked and the server decides the rest.
            verify(view.isValueInRange("LINT", "-9223372036854775808"));
            verify(!view.isValueInRange("ULINT", "-1"));
            verify(!view.isValueInRange("LINT", "12.5"));

            // A non-numeric type imposes no range at all.
            verify(view.isValueInRange("STRING", "anything"));

            // Every numeric type explains what it accepts.
            verify(view.rangeHintFor("DINT").length > 0);
            verify(view.rangeHintFor("LREAL").length > 0);
            verify(view.rangeHintFor("LINT").length > 0);
            compare(view.rangeHintFor("STRING"), "");
        }

        /*! Verifies that a node the server marks read-only cannot be edited. */
        function test_readOnlyRowsRefuseTheEditor() {
            cppManagerOpcUa.clearMockNodes();
            // AccessLevel 3 is CurrentRead|CurrentWrite, 1 is CurrentRead only.
            cppManagerOpcUa.addMockNode("ns=1;s=W", "Writable", "DINT", 3);
            cppManagerOpcUa.addMockNode("ns=1;s=R", "ReadOnly", "DINT", 1);
            cppManagerOpcUa.addMockNode("ns=1;s=U", "Unknown", "DINT", -1);

            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);
            compare(view.allViewRows().length, 3);

            verify(view.editValue(0));

            // The server withholds CurrentWrite, so the editor never opens.
            verify(!view.editValue(1));

            // Nothing is known about the third node yet; refusing it would block
            // a write the server would have accepted.
            verify(view.editValue(2));

            verify(!view.editValue(-1));

            cppManagerOpcUa.clearMockNodes();
        }

        /*!
            Verifies that accepting the editor with an unacceptable input writes nothing.

            The OK button is disabled through a binding installed after the dialog
            is created, so it cannot be the only gate: any accept that does not go
            through the button would otherwise reach the server with a value the
            node cannot hold.
        */
        function test_acceptingAnInvalidInputWritesNothing() {
            cppManagerOpcUa.clearMockNodes();
            // AccessLevel 3 is CurrentRead|CurrentWrite.
            cppManagerOpcUa.addMockNode("ns=1;s=I", "Counter", "DINT", 3);

            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);
            verify(view.editValue(0));

            const dialog = findChild(view, "valueEditorDialog");
            verify(dialog !== null);

            // The row has no value yet, so the editor starts on empty text, which
            // is not a whole number and therefore not writable.
            compare(dialog.inputAcceptable, false);

            dialog.accept();
            compare(cppManagerOpcUa.writeCount, 0);

            cppManagerOpcUa.clearMockNodes();
        }

        /*!
            Verifies that clicking a row highlights every cell of that row.

            A click only moves the current index without selecting the row, so
            the highlight must follow the current row, not just the selection.
        */
        function test_clickedRowIsHighlighted() {
            cppManagerOpcUa.clearMockNodes();
            // AccessLevel 3 is CurrentRead|CurrentWrite.
            cppManagerOpcUa.addMockNode("ns=1;s=A", "First", "DINT", 3);
            cppManagerOpcUa.addMockNode("ns=1;s=B", "Second", "DINT", 3);

            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);
            const table = findChild(view, "dataTable");
            verify(table !== null);
            waitForRendering(view);

            const clicked = table.itemAtCell(Qt.point(1, 1));
            verify(clicked !== null);
            verify(!clicked.highlighted);

            mouseClick(clicked);
            tryCompare(table, "currentRow", 1);

            // Every cell of the clicked row is tinted, the other row is not.
            const sibling = table.itemAtCell(Qt.point(0, 1));
            verify(clicked.highlighted);
            verify(sibling.highlighted);
            compare(Qt.colorEqual(clicked.color, view.selectedRowColor), true);
            // The tests run in the light theme, where lightening the near-white
            // background would leave the highlight invisible.
            verify(!Qt.colorEqual(view.selectedRowColor, view.color));
            verify(!table.itemAtCell(Qt.point(1, 0)).highlighted);
            // Unselected rows are not striped, so they show the plain panel.
            compare(table.itemAtCell(Qt.point(1, 0)).color.a, 0);

            cppManagerOpcUa.clearMockNodes();
        }

        /*!
            Verifies that an untouched table never writes its own defaults back.

            The table samples its layout on a timer because TableView has no
            resize-finished signal. If that sampling pushed the built-in defaults,
            merely opening a project would immediately mark it as modified.
        */
        function test_untouchedTableDoesNotPersistDefaults() {
            cppManagerOpcUa.dataViewState = {};

            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);

            // Outlast one full sampling period without touching the table.
            wait(1800);
            compare(Object.keys(cppManagerOpcUa.dataViewState).length, 0);

            // A real change is still persisted.
            view.setColumnVisible(view.statusColumn, false);
            compare(cppManagerOpcUa.dataViewState.visible[view.statusColumn], false);

            cppManagerOpcUa.dataViewState = {};
        }

        /*!
            Verifies that the value highlight palette follows the inherited theme.

            The window sets the Material theme and the table inherits it, so the
            theme is switched on a parent item here. The model must receive the
            matching palette in both directions; otherwise the value colors keep
            the dark palette, which is too pale on the light background.
        */
        function test_valuePaletteFollowsTheInheritedTheme() {
            const host = createTemporaryObject(themedDataViewComponent, root);
            verify(host !== null);
            const model = cppManagerOpcUa.dataModel;

            host.Material.theme = Material.Dark;
            tryCompare(model, "darkTheme", true);

            host.Material.theme = Material.Light;
            tryCompare(model, "darkTheme", false);

            host.Material.theme = Material.Dark;
            tryCompare(model, "darkTheme", true);
        }

        /*!
            Verifies that unselected rows are not striped.

            Alternating row shading was removed on purpose: every unselected row,
            even or odd, shows the plain panel background in both themes.
        */
        function test_unselectedRowsAreNotStriped() {
            cppManagerOpcUa.clearMockNodes();
            cppManagerOpcUa.addMockNode("ns=1;s=A", "First", "DINT", 3);
            cppManagerOpcUa.addMockNode("ns=1;s=B", "Second", "DINT", 3);
            cppManagerOpcUa.addMockNode("ns=1;s=C", "Third", "DINT", 3);

            const host = createTemporaryObject(themedDataViewComponent, root);
            verify(host !== null);
            const table = findChild(host, "dataTable");
            verify(table !== null);

            const themes = [Material.Dark, Material.Light];
            for (let t = 0; t < themes.length; ++t) {
                host.Material.theme = themes[t];
                waitForRendering(host);
                for (let row = 0; row < 3; ++row) {
                    const cell = table.itemAtCell(Qt.point(1, row));
                    verify(cell !== null);
                    verify(!cell.highlighted);
                    compare(cell.color.a, 0, "row " + row + " is shaded");
                }
            }

            cppManagerOpcUa.clearMockNodes();
        }

        /*!
            Verifies that a selected table row looks like the selected tree node.

            The Data Access View and the address-space tree sit side by side, so
            their selection backgrounds must be identical in both themes.
        */
        function test_selectedRowMatchesTheTreeInBothThemes() {
            const host = createTemporaryObject(tableAndTreeComponent, root);
            verify(host !== null);

            host.Material.theme = Material.Dark;
            verify(Qt.colorEqual(host.table.selectedRowColor, host.tree.selectedRowColor));

            host.Material.theme = Material.Light;
            verify(Qt.colorEqual(host.table.selectedRowColor, host.tree.selectedRowColor));
        }

        /*!
            Verifies that rows with the same name show the server's name unchanged.

            Rows are told apart by the context column, so the name cell and the
            value editor title keep the plain display name.
        */
        function test_namesStayPlain() {
            cppManagerOpcUa.clearMockNodes();
            cppManagerOpcUa.addMockNodeAtPath("ns=1;s=A", "Objects/s1/value");
            cppManagerOpcUa.addMockNodeAtPath("ns=1;s=B", "Objects/s2/value");

            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);
            const table = findChild(view, "dataTable");
            verify(table !== null);
            waitForRendering(view);

            const cell = table.itemAtCell(Qt.point(view.displayNameColumn, 0));
            verify(cell !== null);
            const label = findChild(cell, "cellLabel");
            verify(label !== null);
            compare(label.text, "value");

            verify(view.editValue(1));
            const dialog = findChild(view, "valueEditorDialog");
            compare(dialog.editDisplayName, "value");
            dialog.close();

            cppManagerOpcUa.clearMockNodes();
        }

        /*! Returns the built-in column order: the context column right after the name. */
        function defaultOrder() {
            return [0, 1, 11, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        }

        /*! Creates a table and waits until TableView has built its columns. */
        function createTable() {
            const view = createTemporaryObject(dataViewComponent, root);
            verify(view !== null);
            const table = findChild(view, "dataTable");
            tryCompare(table, "columns", view.columnCount);
            return view
        }

        /*! Verifies that the context column follows the display name by default. */
        function test_contextColumnFollowsTheName() {
            const view = createTable();

            compare(view.columnOrder, defaultOrder());
            compare(view.visibleColumns().slice(0, 4), [0, 1, 11, 2]);
            compare(cppManagerOpcUa.dataModel.columnTitle(view.contextColumn), "Context");
        }

        /*! Verifies that the context cell shows the parent path with the full path as tooltip. */
        function test_contextCellShowsPathTooltip() {
            cppManagerOpcUa.clearMockNodes();
            cppManagerOpcUa.addMockNodeAtPath("ns=1;s=A", "Objects/Boiler/Drum/Level");
            const view = createTable();
            const table = findChild(view, "dataTable");
            waitForRendering(view);

            const cell = table.itemAtCell(Qt.point(view.visualColumn(view.contextColumn), 0));
            verify(cell !== null);
            const label = findChild(cell, "cellLabel");
            compare(label.text, "Boiler.Drum");
            compare(label.elide, Text.ElideLeft);
            compare(label.ToolTip.text, "Objects/Boiler/Drum/Level");

            cppManagerOpcUa.clearMockNodes();
        }

        /*!
            Verifies that a layout saved before the context column existed migrates.

            Its widths stay on their columns, the context column is added visible
            after the name, and a sort stored by that build is not restored.
        */
        function test_elevenColumnLayoutMigrates() {
            const view = createTable();
            const table = findChild(view, "dataTable");

            let visible = []
            let widths = []
            for (let c = 0; c < 11; ++c) {
                visible.push(c !== 4)
                widths.push(c === 2 ? 250 : -1)
            }
            cppManagerOpcUa.dataViewModel.applySort(1, 0);
            view.applyState({ "visible": visible, "widths": widths,
                              "sortColumn": 1, "sortOrder": 1 });

            compare(view.columnOrder, defaultOrder());
            compare(view.columnVisible[4], false);
            compare(view.columnVisible[view.contextColumn], true);
            compare(table.explicitColumnWidth(view.visualColumn(2)), 250);
            compare(cppManagerOpcUa.dataViewModel.sortColumn, -1);
        }

        /*! Verifies that the sort is not part of the saved layout. */
        function test_sortIsNotSaved() {
            const view = createTable();

            cppManagerOpcUa.dataViewModel.applySort(1, 0);
            const state = view.currentState();
            verify(state.sortColumn === undefined);
            verify(state.sortOrder === undefined);
            cppManagerOpcUa.dataViewModel.applySort(-1, 0);
        }

        /*! Verifies that a moved column is reported in the saved order and in copies. */
        function test_movedColumnIsSavedInOrder() {
            const view = createTable();

            // Logical column 9 is the node id.
            view.moveColumn(view.visualColumn(9), 1);

            compare(view.currentState().order, [0, 9, 1, 11, 2, 3, 4, 5, 6, 7, 8, 10]);
            compare(view.visibleColumns()[1], 9);
        }

        /*! Verifies that a stored column order is restored silently. */
        function test_orderRoundTrip() {
            const view = createTable();

            view.moveColumn(view.visualColumn(9), 1);
            const saved = view.currentState();

            view.applyState({});
            compare(view.columnOrder, defaultOrder());

            view.applyState(saved);
            compare(view.columnOrder, saved.order);
            compare(view.restoring, false);
        }

        /*!
            Verifies that a column order stored in the project is restored.

            The layout makes a round trip through the C++ project state, where the
            arrays come back as sequences rather than plain JavaScript arrays.
        */
        function test_orderSurvivesTheProjectRoundTrip() {
            const view = createTable();

            view.moveColumn(view.visualColumn(9), 1);
            view.saveState();
            const stored = cppManagerOpcUa.dataViewState;

            view.applyState({});
            compare(view.columnOrder, defaultOrder());

            view.applyState(stored);
            compare(view.columnOrder, [0, 9, 1, 11, 2, 3, 4, 5, 6, 7, 8, 10]);
        }

        /*!
            Drags the header of the column at visual position \a fromVisual onto the
            header at visual position \a toVisual with real mouse events.
        */
        function dragHeader(view, fromVisual, toVisual) {
            const header = findChild(view, "dataHeader");
            verify(header !== null);
            const source = header.itemAtCell(Qt.point(fromVisual, 0));
            const target = header.itemAtCell(Qt.point(toVisual, 0));
            verify(source !== null);
            verify(target !== null);
            const start = source.mapToItem(header, source.width / 2, source.height / 2);
            const end = target.mapToItem(header, target.width / 2, target.height / 2);

            mousePress(header, start.x, start.y);
            const steps = 10;
            for (let i = 1; i <= steps; ++i) {
                mouseMove(header, start.x + (end.x - start.x) * i / steps,
                          start.y + (end.y - start.y) * i / steps);
            }
            mouseRelease(header, end.x, end.y);
            wait(0);
        }

        /*! Verifies that dragging a header moves its column there. */
        function test_draggingAHeaderMovesTheColumn() {
            const view = createTable();
            waitForRendering(view);

            // Logical 4 is the status, logical 1 the display name.
            dragHeader(view, view.visualColumn(4), view.visualColumn(1));

            compare(view.visualColumn(4), 1);
            compare(view.desiredOrder, view.columnOrder);
        }

        /*!
            Verifies that a click on a column border does not break dragging.

            In Qt's own header dragging such a click leaves the resize handler
            active for good, after which no header can be dragged any more.
        */
        function test_dragAfterBorderClickStillMoves() {
            const view = createTable();
            waitForRendering(view);
            const header = findChild(view, "dataHeader");

            const nameHeader = header.itemAtCell(Qt.point(view.visualColumn(1), 0));
            const border = nameHeader.mapToItem(header, nameHeader.width - 1,
                                                nameHeader.height / 2);
            mouseClick(header, border.x, border.y);

            dragHeader(view, view.visualColumn(4), view.visualColumn(1));
            compare(view.visualColumn(4), 1);
        }

        /*! Verifies that a header dropped onto # lands right behind it. */
        function test_dragOntoRowNumberLandsSecond() {
            const view = createTable();
            waitForRendering(view);

            dragHeader(view, view.visualColumn(4), 0);

            compare(view.columnOrder[0], 0);
            compare(view.visualColumn(4), 1);
        }

        /*! Verifies that dragging a header does not toggle the sort. */
        function test_dragDoesNotSort() {
            const view = createTable();
            waitForRendering(view);

            dragHeader(view, view.visualColumn(4), view.visualColumn(1));

            compare(view.visualColumn(4), 1);
            compare(cppManagerOpcUa.dataViewModel.sortColumn, -1);
        }

        /*! Returns the cell label of logical \a column in view row \a row. */
        function cellLabel(view, column, row) {
            const table = findChild(view, "dataTable");
            const cell = table.itemAtCell(Qt.point(view.visualColumn(column), row));
            verify(cell !== null, "no cell for column " + column);
            return findChild(cell, "cellLabel");
        }

        /*!
            Verifies the two text levels of the table.

            Name, value, and status are the primary columns; every other column is
            reference information drawn muted. A row still waiting for its first
            value dims its primary columns only, so the muted columns stay readable.
        */
        function test_secondaryColumnsAreMuted() {
            cppManagerOpcUa.clearMockNodes();
            cppManagerOpcUa.addMockNode("ns=1;s=A", "Live", "DINT", 3);
            cppManagerOpcUa.addMockNode("ns=1;s=B", "Waiting", "DINT", 3);
            cppManagerOpcUa.setMockValue("ns=1;s=A", "42");
            const view = createTable();
            waitForRendering(view);

            const muted = view.secondaryOpacity;
            verify(muted > 0.3 && muted < 1.0);

            // A row with a value: primary columns at full strength.
            compare(cellLabel(view, view.displayNameColumn, 0).opacity, 1.0);
            compare(cellLabel(view, view.valueColumn, 0).opacity, 1.0);
            compare(cellLabel(view, view.statusColumn, 0).opacity, 1.0);
            compare(cellLabel(view, 0, 0).opacity, muted);
            compare(cellLabel(view, 3, 0).opacity, muted);
            compare(cellLabel(view, view.contextColumn, 0).opacity, muted);

            // A row without a value: primary columns dimmed, muted ones unchanged.
            compare(cellLabel(view, view.displayNameColumn, 1).opacity, 0.5);
            compare(cellLabel(view, view.valueColumn, 1).opacity, 0.5);
            compare(cellLabel(view, 0, 1).opacity, muted);
            compare(cellLabel(view, 3, 1).opacity, muted);

            cppManagerOpcUa.clearMockNodes();
        }

        /*! Verifies that the row-number column cannot leave the first position. */
        function test_rowNumberColumnStaysFirst() {
            const view = createTable();

            view.moveColumn(0, 3);

            tryVerify(function () { return view.columnOrder[0] === 0 });
            compare(view.columnOrder, defaultOrder());
        }

        /*! Verifies that a layout saved before columns could move keeps the default order. */
        function test_layoutWithoutOrderKeepsDefaultOrder() {
            const view = createTable();
            const table = findChild(view, "dataTable");

            let widths = []
            for (let c = 0; c < view.columnCount; ++c)
                widths.push(c === 1 ? 222 : -1)
            view.applyState({ "visible": view.columnVisible.slice(), "widths": widths });

            compare(view.columnOrder, defaultOrder());
            compare(table.explicitColumnWidth(view.visualColumn(1)), 222);
        }

        /*! Verifies that an order that is not a permutation starting with 0 is ignored. */
        function test_invalidOrderIsIgnored() {
            const view = createTable();

            view.applyState({ "order": [1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] });
            compare(view.columnOrder, defaultOrder());

            view.applyState({ "order": [0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] });
            compare(view.columnOrder, defaultOrder());

            view.applyState({ "order": [0, 1, 2] });
            compare(view.columnOrder, defaultOrder());
        }

        /*! Verifies that a dragged width stays with its column when the column moves. */
        function test_widthsFollowMovedColumns() {
            const view = createTable();
            const table = findChild(view, "dataTable");

            table.setColumnWidth(view.visualColumn(2), 260);
            view.moveColumn(view.visualColumn(2), 5);

            const state = view.currentState();
            compare(state.widths[2], 260);

            view.applyState({});
            view.applyState(state);
            compare(view.visualColumn(2), 5);
            compare(table.explicitColumnWidth(view.visualColumn(2)), 260);
        }
    }
}
