import QtQuick
import QtTest
import Base

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

        name: "QmlComponentSmoke"
        when: windowShown

        function test_menuBarCreationAndThemeProperty() {
            const menuBar = createTemporaryObject(menuBarComponent, root);
            verify(menuBar !== null);
            compare(menuBar.darkTheme, false);
            menuBar.darkTheme = true;
            compare(menuBar.darkTheme, true);
        }

        function test_connectionFormCreation() {
            const connectionForm = createTemporaryObject(connectionFormComponent, root);
            verify(connectionForm !== null);
            compare(connectionForm.validationError, "");
            verify(connectionForm.implicitWidth > 0);
        }

        function test_browserCreation() {
            const browser = createTemporaryObject(browserComponent, root);
            verify(browser !== null);
            compare(browser.width, 800);
            compare(browser.height, 500);
        }
    }
}
