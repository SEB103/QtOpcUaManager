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
