import QtQuick
import QtQuick.Controls.Material
import QtTest
import Base

/*!
    \qmltype tst_treedrag
    \brief Hosts drag-and-drop tests from the address-space tree to the Data View.

    The tree pane shows the mock tree model and sits next to the real Data
    Access View, as in the browser layout. A drop calls the mock manager's
    monitorNodeById(), which adds a table row and sets the node's checkbox flag
    the same way the real manager does, so the cases check that a drop by mouse
    or by touch long press adds the node and ticks its checkbox.
*/
Item {
    id: root

    width: 1000
    height: 500

    Component {
        id: layoutComponent

        Item {
            property alias tree: treePane
            property alias table: dataView

            width: 940
            height: 400

            BsAddressSpaceTreePane {
                id: treePane

                width: 300
                height: 400
                paneModel: cppManagerOpcUa.dragTreeModel
                titleText: "ADDRESS SPACE"
            }

            BsNodeDataView {
                id: dataView

                x: 320
                width: 620
                height: 400
            }
        }
    }

    TestCase {
        id: testCase

        /*! Test case for dragging tree nodes into the Data Access View. */
        name: "TreeDragAndDrop"
        when: windowShown

        /*! Layout created by the current test, or null. */
        property var layout: null

        /*!
            Returns the first descendant of \a item that is a check box, or null.
            The check box is the only descendant exposing \c checkState.
        */
        function findCheckBox(item) {
            for (let i = 0; i < item.children.length; ++i) {
                const child = item.children[i]
                if (child.checkState !== undefined)
                    return child
                const nested = findCheckBox(child)
                if (nested)
                    return nested
            }
            return null
        }

        /*! Returns the tree view inside the current layout. */
        function treeView() {
            return findChild(testCase.layout.tree, "addressSpaceTree")
        }

        /*! Returns the delegate of tree row \a row once it has been laid out. */
        function rowItem(row) {
            const view = treeView()
            tryVerify(function () {
                return view.itemAtIndex(view.index(row, 0)) !== null
            })
            return view.itemAtIndex(view.index(row, 0))
        }

        /*!
            Returns the center of the Data View table in the coordinates of
            \a item, which is where every drag in these cases ends.
        */
        function tableCenterIn(item) {
            const table = testCase.layout.table
            return item.mapFromItem(table, table.width / 2, table.height / 2)
        }

        /*!
            Drags tree row \a row into the table with the mouse, moving in small
            steps so the drag threshold is crossed as a real pointer would.
        */
        function mouseDragRowToTable(row) {
            const item = rowItem(row)
            const startX = 60
            const startY = item.height / 2
            const end = tableCenterIn(item)
            mousePress(item, startX, startY, Qt.LeftButton)
            const steps = 12
            for (let i = 1; i <= steps; ++i) {
                mouseMove(item,
                          startX + (end.x - startX) * i / steps,
                          startY + (end.y - startY) * i / steps,
                          -1, Qt.LeftButton)
            }
            mouseRelease(item, end.x, end.y, Qt.LeftButton)
        }

        /*!
            Drags tree row \a row into the table with one finger. When \a hold is
            true the finger rests long enough for a long press before moving.
        */
        function touchDragRowToTable(row, hold) {
            const item = rowItem(row)
            const startX = 60
            const startY = item.height / 2
            const end = tableCenterIn(item)
            const touch = touchEvent(item)
            touch.press(0, item, startX, startY).commit()
            if (hold)
                wait(Application.styleHints.mousePressAndHoldInterval + 300)
            const steps = 12
            for (let i = 1; i <= steps; ++i) {
                touch.move(0, item,
                           startX + (end.x - startX) * i / steps,
                           startY + (end.y - startY) * i / steps).commit()
                wait(10)
            }
            touch.release(0, item, end.x, end.y).commit()
        }

        function init() {
            cppManagerOpcUa.clearMockNodes()
            cppManagerOpcUa.resetMockTree()
            testCase.layout = createTemporaryObject(layoutComponent, root)
            verify(testCase.layout)
            tryVerify(function () { return treeView().rows === 3 })
        }

        function cleanup() {
            testCase.layout = null
            cppManagerOpcUa.clearMockNodes()
        }

        /*! Verifies that a mouse drag adds the node and ticks its checkbox. */
        function test_mouseDropAddsNodeAndTicksCheckbox() {
            const view = treeView()
            const checkBox = findCheckBox(rowItem(0))
            verify(checkBox)
            verify(!checkBox.checked)

            mouseDragRowToTable(0)

            tryCompare(cppManagerOpcUa, "monitoredNodeCount", 1)
            tryCompare(checkBox, "checked", true)
            // The drag must not have scrolled the tree under the pointer.
            compare(view.contentY, view.originY)
        }

        /*! Verifies that a node already in the table is not added a second time. */
        function test_duplicateDropIsRefused() {
            mouseDragRowToTable(0)
            tryCompare(cppManagerOpcUa, "monitoredNodeCount", 1)

            mouseDragRowToTable(0)
            wait(50)
            compare(cppManagerOpcUa.monitoredNodeCount, 1)
        }

        /*! Verifies that a node without a checkbox cannot be dragged into the table. */
        function test_unmonitorableNodeIsNotDropped() {
            mouseDragRowToTable(1)
            wait(50)
            compare(cppManagerOpcUa.monitoredNodeCount, 0)
        }

        /*! Verifies that a quick touch swipe scrolls rather than drags. */
        function test_touchSwipeDoesNotDrag() {
            touchDragRowToTable(2, false)
            wait(50)
            compare(cppManagerOpcUa.monitoredNodeCount, 0)
            verify(!findCheckBox(rowItem(2)).checked)
        }

        /*! Verifies that a touch long press followed by a move drops the node. */
        function test_touchLongPressDropAddsNodeAndTicksCheckbox() {
            const checkBox = findCheckBox(rowItem(2))
            verify(checkBox)

            touchDragRowToTable(2, true)

            tryCompare(cppManagerOpcUa, "monitoredNodeCount", 1)
            tryCompare(checkBox, "checked", true)
            // The tree is interactive again once the finger lifted.
            tryCompare(treeView(), "interactive", true)
        }

        /*! Verifies that lifting the finger after a long press disarms the row. */
        function test_touchLongPressWithoutMoveDisarms() {
            const item = rowItem(2)
            const touch = touchEvent(item)
            touch.press(0, item, 60, item.height / 2).commit()
            wait(Application.styleHints.mousePressAndHoldInterval + 300)
            compare(testCase.layout.tree.touchDragRow, 2)
            compare(treeView().interactive, false)

            touch.release(0, item, 60, item.height / 2).commit()
            tryCompare(testCase.layout.tree, "touchDragRow", -1)
            compare(treeView().interactive, true)
            compare(cppManagerOpcUa.monitoredNodeCount, 0)
        }

        /*! Verifies that the drag preview is hidden again after the drop. */
        function test_dragPreviewHiddenAfterDrop() {
            mouseDragRowToTable(0)
            tryCompare(cppManagerOpcUa, "monitoredNodeCount", 1)
            compare(testCase.layout.tree.rowDragActive, false)
        }
    }
}
