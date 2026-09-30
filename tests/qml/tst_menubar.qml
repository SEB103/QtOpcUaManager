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
        id: scanDialogComponent

        BsNetworkScanDialog {}
    }

    Component {
        id: browserComponent

        BsOpcUaBrowser {
            width: 800
            height: 500
        }
    }

    SignalSpy {
        id: checkForUpdatesSpy

        signalName: "checkForUpdatesRequested"
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

        /*! Verifies that the consolidated Info menu exposes the update-check signal. */
        function test_menuBarCheckForUpdatesSignal() {
            const menuBar = createTemporaryObject(menuBarComponent, root);
            verify(menuBar !== null);
            checkForUpdatesSpy.target = menuBar;
            verify(checkForUpdatesSpy.valid);
        }

        /*!
            Verifies that the menu bar titles are text-only while every dropdown
            still shows icons next to its items.
        */
        function test_menuBarTitlesHaveNoIcons() {
            const menuBar = createTemporaryObject(menuBarComponent, root);
            verify(menuBar !== null);
            compare(menuBar.count, 4);
            for (let i = 0; i < menuBar.count; ++i) {
                const menu = menuBar.menuAt(i);
                compare(menuBar.itemAt(i).icon.source.toString(), "",
                        "menu bar title " + menu.title + " shows an icon");

                let iconCount = 0;
                for (let j = 0; j < menu.count; ++j) {
                    const item = menu.itemAt(j);
                    if (item && item.icon !== undefined
                            && item.icon.source.toString().length > 0) {
                        ++iconCount;
                    }
                }
                verify(iconCount > 0, "menu " + menu.title + " has no item icons");
            }
        }

        /*!
            Verifies that submenu entries draw their icons at the same size as
            plain menu items. A submenu entry copies the whole icon from
            Menu.icon, so an unset size there falls back to the SVG's
            intrinsic size instead of the style's menu item icon size.
        */
        function test_menuItemIconsHaveUniformSize() {
            const menuBar = createTemporaryObject(menuBarComponent, root);
            verify(menuBar !== null);

            const items = [];
            const collect = menu => {
                for (let i = 0; i < menu.count; ++i) {
                    const item = menu.itemAt(i);
                    if (!item || item.icon === undefined)
                        continue;
                    if (item.icon.source.toString().length > 0)
                        items.push(item);
                    if (item.subMenu)
                        collect(item.subMenu);
                }
            };
            for (let i = 0; i < menuBar.count; ++i)
                collect(menuBar.menuAt(i));

            const plainItem = items.find(item => !item.subMenu);
            verify(plainItem !== undefined);
            verify(items.some(item => item.subMenu), "no submenu entry with an icon found");
            verify(plainItem.icon.width > 0);

            for (const item of items) {
                compare(item.icon.width, plainItem.icon.width,
                        "icon width of \"" + item.text + "\"");
                compare(item.icon.height, plainItem.icon.height,
                        "icon height of \"" + item.text + "\"");
            }
        }

        /*! Verifies that BsOpcUaConnectionForm can be created with its default state. */
        function test_connectionFormCreation() {
            const connectionForm = createTemporaryObject(connectionFormComponent, root);
            verify(connectionForm !== null);
            compare(connectionForm.usernameRequired, false);
            verify(connectionForm.implicitWidth > 0);
        }

        /*! Verifies that BsNetworkScanDialog opens, fills its inputs, and starts and stops a scan. */
        function test_networkScanDialog() {
            const dialog = createTemporaryObject(scanDialogComponent, root);
            verify(dialog !== null);
            dialog.open();
            tryCompare(dialog, "opened", true);
            compare(dialog.scanning, false);
            dialog.startScan();
            compare(dialog.scanning, true);
            dialog.close();
            tryCompare(dialog, "visible", false);
            compare(cppNetworkScanner.state, 3);
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
