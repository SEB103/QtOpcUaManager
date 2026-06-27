import QtQuick
import QtTest
import Base

TestCase {
    id: testCase

    name: "MenuBarSmoke"
    when: windowShown

    Component {
        id: menuBarComponent

        BsMenuBar {}
    }

    function test_creationAndThemeProperty() {
        const menuBar = createTemporaryObject(menuBarComponent, testCase);
        verify(menuBar !== null);
        compare(menuBar.darkTheme, false);
        menuBar.darkTheme = true;
        compare(menuBar.darkTheme, true);
    }
}
