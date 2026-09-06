import QtQuick
import QtTest
import Base

/*!
    \qmltype tst_diagnostics
    \brief Hosts behavior tests for the feedback and diagnostics components.

    The cases cover which outcomes reach the notification banner, that the status
    bar reports what the window feeds it, and that the log panel follows the real
    LogModel and LogFilterModel the test mock exposes.
*/
Item {
    id: root

    width: 1000
    height: 700

    Component {
        id: bannerComponent

        BsNotificationBanner {}
    }

    Component {
        id: statusBarComponent

        BsStatusBar {
            width: 800
        }
    }

    Component {
        id: logPanelComponent

        BsLogPanel {
            width: 800
            height: 300
        }
    }

    TestCase {
        id: testCase

        /*! Test case for the notification banner, status bar, and log panel. */
        name: "Diagnostics"
        when: windowShown

        function cleanup() {
            cppAppEngine.clearLog();
            cppAppEngine.logModel.minimumLevel = 1;
            cppAppEngine.logModel.filterText = "";
        }

        /*! Verifies that routine progress never covers the workspace with a banner. */
        function test_bannerIgnoresRoutineProgress() {
            const banner = createTemporaryObject(bannerComponent, root);
            verify(banner !== null);

            compare(banner.visible, false);

            // Debug and Info belong in the status bar only.
            banner.show(0, "developer detail");
            compare(banner.visible, false);
            banner.show(1, "project saved");
            compare(banner.visible, false);

            // An empty message is not worth showing either.
            banner.show(3, "");
            compare(banner.visible, false);
        }

        /*! Verifies that a warning is shown and an error waits for the user. */
        function test_bannerKeepsErrorsUntilDismissed() {
            const banner = createTemporaryObject(bannerComponent, root);
            verify(banner !== null);

            banner.show(2, "endpoint rewritten");
            compare(banner.visible, true);
            compare(banner.message, "endpoint rewritten");
            compare(banner.persistent, false);

            // An error means an action did not happen, so it stays put.
            banner.show(3, "write failed");
            compare(banner.visible, true);
            compare(banner.persistent, true);

            banner.dismiss();
            compare(banner.visible, false);
            compare(banner.message, "");
        }

        /*! Verifies that a warning hides itself after its auto-hide interval. */
        function test_bannerAutoHidesAWarning() {
            const banner = createTemporaryObject(bannerComponent, root);
            verify(banner !== null);

            banner.autoHideInterval = 150;
            banner.show(2, "temporary");
            compare(banner.visible, true);

            tryCompare(banner, "visible", false, 2000);
        }

        /*! Verifies that the status bar shows what the window feeds it. */
        function test_statusBarReportsTheLastOutcome() {
            const statusBar = createTemporaryObject(statusBarComponent, root);
            verify(statusBar !== null);

            compare(statusBar.message, "");

            statusBar.message = "Wrote the value of Temperature.";
            statusBar.messageLevel = 1;
            compare(statusBar.message, "Wrote the value of Temperature.");

            // The colour follows the severity so an error is not read as a warning.
            statusBar.messageLevel = 2;
            const warningColor = String(statusBar.messageColor);
            statusBar.messageLevel = 3;
            const errorColor = String(statusBar.messageColor);
            verify(warningColor !== errorColor,
                   "warning " + warningColor + " vs error " + errorColor);
        }

        /*! Verifies that the status bar asks the window to toggle the log panel. */
        function test_statusBarRequestsTheLogPanel() {
            const statusBar = createTemporaryObject(statusBarComponent, root);
            verify(statusBar !== null);

            const spy = createTemporaryObject(signalSpyComponent, root,
                                              { target: statusBar,
                                                signalName: "logToggleRequested" });
            verify(spy !== null);

            statusBar.logToggleRequested();
            compare(spy.count, 1);
        }

        /*! Verifies that the log panel shows the entries the engine collected. */
        function test_logPanelShowsCollectedEntries() {
            const panel = createTemporaryObject(logPanelComponent, root);
            verify(panel !== null);

            compare(cppAppEngine.logModel.count, 0);

            cppAppEngine.log(1, "browse finished");
            cppAppEngine.log(3, "connect failed");
            compare(cppAppEngine.logModel.count, 2);

            // Raising the severity hides the routine entry.
            cppAppEngine.logModel.minimumLevel = 3;
            compare(cppAppEngine.logModel.count, 1);
            verify(cppAppEngine.logModel.visibleText().indexOf("connect failed") !== -1);
            compare(cppAppEngine.logModel.visibleText().indexOf("browse finished"), -1);
        }

        /*! Verifies that the log panel asks the window to close it. */
        function test_logPanelRequestsClose() {
            const panel = createTemporaryObject(logPanelComponent, root);
            verify(panel !== null);

            const spy = createTemporaryObject(signalSpyComponent, root,
                                              { target: panel,
                                                signalName: "closeRequested" });
            verify(spy !== null);

            panel.closeRequested();
            compare(spy.count, 1);
        }
    }

    Component {
        id: signalSpyComponent

        SignalSpy {}
    }
}
