import QtQuick
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
    }
}
