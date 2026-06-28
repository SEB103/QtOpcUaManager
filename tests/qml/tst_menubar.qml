import QtQuick
import QtTest
import Base

/*!
    \qmltype tst_menubar
    \brief Hosts QML smoke tests for reusable Base components.
*/
Item {
    id: root

    width: 1000
    height: 700

    Component {
        id: menuBarComponent

        BsMenuBar {}
    }

    Component {
        id: connectionFormComponent

        BsOpcUaConnectionForm {}
    }

    Component {
        id: browserComponent

        BsOpcUaBrowser {
            width: 800
            height: 500
        }
    }

    TestCase {
        id: testCase

        /*! Smoke test case for menu, connection form, and browser component creation. */
        name: "QmlComponentSmoke"
        when: windowShown

        /*! Verifies that BsMenuBar can be created and its theme property is writable. */
        function test_menuBarCreationAndThemeProperty() {
            const menuBar = createTemporaryObject(menuBarComponent, root);
            verify(menuBar !== null);
            compare(menuBar.darkTheme, false);
            menuBar.darkTheme = true;
            compare(menuBar.darkTheme, true);
        }

        /*! Verifies that BsOpcUaConnectionForm can be created with its default state. */
        function test_connectionFormCreation() {
            const connectionForm = createTemporaryObject(connectionFormComponent, root);
            verify(connectionForm !== null);
            compare(connectionForm.validationError, "");
            verify(connectionForm.implicitWidth > 0);
        }

        /*! Verifies that BsOpcUaBrowser can be created with an explicit size. */
        function test_browserCreation() {
            const browser = createTemporaryObject(browserComponent, root);
            verify(browser !== null);
            compare(browser.width, 800);
            compare(browser.height, 500);
        }
    }
}
