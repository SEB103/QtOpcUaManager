import QtQuick
import QtQuick.Controls
import QtTest
import Base

/*!
    \qmltype tst_layoutstate
    \brief Hosts round-trip tests for the saved pane layout of the browser area.

    The cases verify that BsOpcUaBrowser reports the state of every resizable
    SplitView (columns, address-space segments, attribute sections) and that a
    second browser restored from that state reproduces the pane sizes, while
    missing or empty state leaves the defaults untouched.
*/
Item {
    id: root

    width: 1400
    height: 900

    Component {
        id: browserComponent

        BsOpcUaBrowser {
            width: 1400
            height: 900
        }
    }

    TestCase {
        id: testCase

        /*! Test case for saving and restoring the browser pane layout. */
        name: "LayoutState"
        when: windowShown

        /*!
            Returns the column SplitView of \a browser and its address-space,
            data-view, and attributes panes.
        */
        function columns(browser) {
            const split = browser.children[0]
            return {
                split: split,
                address: split.contentChildren[0],
                data: split.contentChildren[1],
                attributes: split.contentChildren[2]
            }
        }

        /*!
            Verifies that changed column widths survive a save/restore round trip
            into a freshly created browser.
        */
        function test_columnWidthsRoundTrip() {
            const source = createTemporaryObject(browserComponent, root)
            verify(source !== null)
            const sourceColumns = columns(source)
            sourceColumns.address.SplitView.preferredWidth = 480
            sourceColumns.attributes.SplitView.preferredWidth = 410
            tryCompare(sourceColumns.address, "width", 480)

            const states = source.saveSplitStates()
            verify(states.browserColumns !== undefined)
            verify(states.addressSpaceSegments !== undefined)
            verify(states.attributeSections !== undefined)

            const target = createTemporaryObject(browserComponent, root)
            verify(target !== null)
            const targetColumns = columns(target)
            compare(targetColumns.address.width, 340)

            target.restoreSplitStates(states)
            tryCompare(targetColumns.address, "width", 480)
            tryCompare(targetColumns.attributes, "width", 410)
        }

        /*! Verifies that missing or partial state keeps the default widths. */
        function test_missingStateKeepsDefaults() {
            const browser = createTemporaryObject(browserComponent, root)
            verify(browser !== null)
            const browserColumns = columns(browser)

            browser.restoreSplitStates(undefined)
            browser.restoreSplitStates({})
            browser.restoreSplitStates({ addressSpaceSegments: undefined })
            wait(0)
            compare(browserColumns.address.width, 340)
            compare(browserColumns.attributes.width, 320)
        }
    }
}
