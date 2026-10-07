import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import Base as Base

/*!
    \qmltype Main
    \inqmlmodule OpcUaManager
    \brief Provides the main application window.

    The window owns the top-level Material theme state and switches between the
    Project Launcher (shown when no project is active) and the workspace
    (MainScreen). It also hosts the project file dialogs and the OPC UA connection
    dialogs opened from menu actions.
*/
ApplicationWindow {
    id: mainWindow

    width: 1200
    height: 800
    minimumWidth: 800
    minimumHeight: 600
    // The window starts hidden: restoreUiLayout() applies the saved geometry first
    // and then shows the window in the saved visibility, so it does not jump.
    title: cppProjectManager.hasActiveProject
           ? qsTr("%1 — %2%3")
                 .arg(cppAppInfo.appName)
                 .arg(cppProjectManager.activeProjectName)
                 .arg(cppProjectManager.dirty ? "*" : "")
           : cppAppInfo.appName

    /*!
        Whether the application currently uses the dark Material theme. It is
        resolved from the persisted \c cppTheme mode: "light" and "dark" are fixed
        choices, while "system" follows the operating system color scheme.
    */
    readonly property bool darkTheme: cppTheme.mode === "dark"
                                      || (cppTheme.mode === "system"
                                          && Application.styleHints.colorScheme === Qt.Dark)

    /*!
        Global UI zoom as a scale factor (1.0 = 100 %), driven by \c cppUiZoom.
        It scales \c zoomLayer and the popup overlay, so screens, menus, dialogs,
        and tooltips all follow one value.
    */
    readonly property real uiZoomFactor: cppUiZoom.zoomFactor

    /*!
        Window width in logical (zoom-adjusted) units: the width the UI and its
        popups lay out in. It is valid from the start, unlike the width of the
        content item, which is still 0 while the window is being created.
    */
    readonly property real uiWidth: width / uiZoomFactor

    /*! Window height in logical (zoom-adjusted) units; see \l uiWidth. */
    readonly property real uiHeight: height / uiZoomFactor

    /*!
        Last geometry of the window in the normal (not maximized, not full
        screen) state. It is saved on exit, so un-maximizing after the next start
        returns to the size the user chose.
    */
    property rect normalGeometry: Qt.rect(0, 0, 0, 0)

    /*!
        Last shown visibility (Window.Windowed, Window.Maximized, or
        Window.FullScreen), saved on exit. Minimized and hidden states are not
        recorded, so the next start never opens minimized.
    */
    property int lastVisibility: Window.Windowed

    /*! Visibility to return to when full screen mode is left. */
    property int preFullScreenVisibility: Window.Windowed

    /*! Pending action deferred until the unsaved-changes prompt is answered. */
    property var pendingAction: null

    /*! Whether the Server Studio workspace is shown instead of the launcher. */
    property bool serverStudioActive: false

    /*!
        Whether the OPC UA client workspace should be shown. It appears for an
        active project and, in the Server Studio proof of concept, whenever the
        client is connected (for example after "Open in Client") even without a
        project.
    */
    readonly property bool workspaceActive:
        cppProjectManager.hasActiveProject || cppManagerOpcUa.connected

    /*! Whether the collapsible log panel is shown in the workspace. */
    property bool logPanelVisible: false

    /*! Whether the collapsible trend panel is shown in the workspace. */
    property bool trendPanelVisible: false

    /*! Text of the most recent operation outcome, shown in the status bar. */
    property string statusMessage: ""

    /*! Severity of \l statusMessage as a Diagnostics::Level value. */
    property int statusMessageLevel: 1

    /*!
        Identifies which controller produced \l statusMessage ("opcua", "project"
        or "studio"). A live language switch re-translates the status bar from the
        matching controller's statusRetranslated() only, so an older message from
        another controller cannot overwrite the one currently shown.
    */
    property string statusSource: ""

    /*!
        Records the outcome \a message at severity \a level, produced by the
        controller identified by \a source.

        Every outcome lands in the status bar; the banner additionally surfaces
        warnings and errors, which the user has to notice.
    */
    function showNotification(level, message, source) {
        mainWindow.statusMessage = message
        mainWindow.statusMessageLevel = level
        mainWindow.statusSource = source !== undefined ? source : ""
        mainScreen.notificationBanner.show(level, message)
    }

    /*!
        Updates the persistent status bar with \a message at \a level after a live
        language switch, but only when \a source matches the controller that
        produced the message currently shown. Unlike showNotification() this does
        not re-raise the transient banner.
    */
    function refreshStatus(level, message, source) {
        if (mainWindow.statusSource !== source)
            return
        mainWindow.statusMessage = message
        mainWindow.statusMessageLevel = level
    }

    /*!
        Runs \a action immediately, or, when the active project has unsaved
        changes, defers it behind the unsaved-changes prompt.
    */
    function runGuarded(action) {
        if (cppProjectManager.dirty) {
            mainWindow.pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }

    /*! Runs and clears the deferred action stored by runGuarded(). */
    function proceedPending() {
        const action = mainWindow.pendingAction
        mainWindow.pendingAction = null
        if (action)
            action()
    }

    /*! Action deferred until the Server Studio unsaved-changes prompt is answered. */
    property var serverStudioPendingAction: null

    /*!
        Runs \a action immediately, or, when the Server Studio project has unsaved
        changes, defers it behind the Server Studio unsaved-changes prompt. Used
        for operations that replace the server project (clone) and for quit.
    */
    function runServerStudioGuarded(action) {
        if (cppServerStudio.dirty) {
            mainWindow.serverStudioPendingAction = action
            serverStudioUnsavedDialog.open()
        } else {
            action()
        }
    }

    /*! Runs and clears the deferred action stored by runServerStudioGuarded(). */
    function proceedServerStudioPending() {
        const action = mainWindow.serverStudioPendingAction
        mainWindow.serverStudioPendingAction = null
        if (action)
            action()
    }

    /*! Opens the clone options prompt (id-fidelity choice) before cloning. */
    function doCloneToServerStudio() {
        cloneOptionsDialog.open()
    }

    /*!
        Puts the popup overlay into the coordinate space of \c zoomLayer: the same
        scale around the top-left corner and the same logical size. Popups then
        open at the zoomed size and at the right place, and modal dimmers still
        cover exactly the window.

        The overlay resets its own size to the window size whenever the window is
        resized, so this runs again on every overlay and zoom layer geometry change;
        it only writes values that differ, which ends the update cycle.
    */
    function syncOverlayZoom() {
        const overlay = mainWindow.Overlay.overlay
        if (!overlay)
            return
        overlay.transformOrigin = Item.TopLeft
        if (overlay.scale !== zoomLayer.scale)
            overlay.scale = zoomLayer.scale
        if (overlay.width !== zoomLayer.width)
            overlay.width = zoomLayer.width
        if (overlay.height !== zoomLayer.height)
            overlay.height = zoomLayer.height
    }

    /*!
        Records the current geometry as \l normalGeometry while the window is in
        the normal state. Runs debounced through \c normalGeometryTimer: when the
        window is maximized, the size changes may arrive before the visibility
        change, and by the time the timer fires the window already reports
        Window.Maximized, so the maximized size is not taken as the normal one.
    */
    function rememberNormalGeometry() {
        if (mainWindow.visibility === Window.Windowed)
            mainWindow.normalGeometry = Qt.rect(mainWindow.x, mainWindow.y,
                                                mainWindow.width, mainWindow.height)
    }

    /*!
        Applies the saved user layout and shows the window: the normal geometry,
        the sizes of all resizable panes, and finally the saved visibility
        (normal, maximized, or full screen). Without saved state the window opens
        at its default size where the platform places it.
    */
    function restoreUiLayout() {
        const geometry = cppUiLayout.restoredWindowGeometry()
        if (geometry.width > 0 && geometry.height > 0) {
            mainWindow.x = geometry.x
            mainWindow.y = geometry.y
            mainWindow.width = geometry.width
            mainWindow.height = geometry.height
            mainWindow.normalGeometry = geometry
        }

        mainScreen.browser.restoreSplitStates(cppUiLayout.splitStates())

        const visibility = cppUiLayout.restoredWindowVisibility()
        mainWindow.lastVisibility = visibility
        mainWindow.visibility = visibility
    }

    /*!
        Saves the window state and the pane layout. Called when the window is
        closed and again when the application is about to quit, which also
        covers quitting without a close request (for example for an update).
    */
    function saveUiLayout() {
        mainWindow.rememberNormalGeometry()
        cppUiLayout.saveWindowState(mainWindow.normalGeometry, mainWindow.lastVisibility)
        cppUiLayout.saveSplitStates(mainScreen.browser.saveSplitStates())
    }

    /*!
        Enters full screen mode, or leaves it for the state the window had
        before (normal or maximized).
    */
    function toggleFullScreen() {
        if (mainWindow.visibility === Window.FullScreen) {
            mainWindow.visibility = mainWindow.preFullScreenVisibility
        } else {
            mainWindow.preFullScreenVisibility = mainWindow.visibility === Window.Maximized
                                                 ? Window.Maximized : Window.Windowed
            mainWindow.visibility = Window.FullScreen
        }
    }

    // Guard application exit: prompt to save when the active project or the
    // Server Studio project is dirty. Resolve the client project first, then the
    // Server Studio project, then quit.
    onClosing: (close) => {
        // Save while the window still reports its real visibility and geometry.
        mainWindow.saveUiLayout()
        if (cppProjectManager.dirty || cppServerStudio.dirty) {
            close.accepted = false
            runGuarded(() => mainWindow.runServerStudioGuarded(() => Qt.quit()))
        }
    }

    // Track the normal geometry and the last shown visibility for saveUiLayout().
    onXChanged: normalGeometryTimer.restart()
    onYChanged: normalGeometryTimer.restart()
    onWidthChanged: normalGeometryTimer.restart()
    onHeightChanged: normalGeometryTimer.restart()
    onVisibilityChanged: {
        if (visibility === Window.Windowed || visibility === Window.Maximized
                || visibility === Window.FullScreen) {
            mainWindow.lastVisibility = visibility
        }
        normalGeometryTimer.restart()
    }

    Material.theme: darkTheme ? Material.Dark : Material.Light
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    Timer {
        id: normalGeometryTimer

        interval: 300
        onTriggered: mainWindow.rememberNormalGeometry()
    }

    // Also save when quitting without a close request (Qt.quit(), for example
    // before an update); the window is hidden by then, so the tracked values are used.
    Connections {
        target: Qt.application

        function onAboutToQuit() {
            mainWindow.saveUiLayout()
        }
    }

    // The shared chrome palette follows the window theme, including the View
    // menu switch.
    Binding {
        target: Base.BsTheme
        property: "dark"
        value: mainWindow.darkTheme
    }

    // Global UI zoom, like the page zoom of a web browser: the whole window UI is
    // laid out at the logical size window / zoom and scaled back up to the window
    // size. Layouts therefore reflow (more room at 80 %, less at 150 %) instead of
    // being clipped. Popups follow through syncOverlayZoom().
    Item {
        id: zoomLayer

        // The window has no header, footer, or menuBar, so the zoom layer covers
        // the whole window, exactly like the popup overlay.
        width: mainWindow.uiWidth
        height: mainWindow.uiHeight
        scale: mainWindow.uiZoomFactor
        transformOrigin: Item.TopLeft

        onWidthChanged: mainWindow.syncOverlayZoom()
        onHeightChanged: mainWindow.syncOverlayZoom()
        onScaleChanged: mainWindow.syncOverlayZoom()

        // The workspace is present but hidden until a project is active, so its
        // menu and browser bindings stay wired across project open/close.
        MainScreen {
            id: mainScreen

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: statusBar.visible ? statusBar.top : parent.bottom
            darkTheme: mainWindow.darkTheme
            visible: mainWindow.workspaceActive && !mainWindow.serverStudioActive
            logPanelVisible: mainWindow.logPanelVisible
            trendPanelVisible: mainWindow.trendPanelVisible
            zoomPercent: cppUiZoom.zoomPercent
            canZoomIn: cppUiZoom.canZoomIn
            canZoomOut: cppUiZoom.canZoomOut
            fullScreen: mainWindow.visibility === Window.FullScreen
        }

        // The status bar belongs to the workspace; the launcher has nothing to
        // report. It lives inside the zoom layer so it scales with the workspace.
        Base.BsStatusBar {
            id: statusBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: implicitHeight
            visible: mainWindow.workspaceActive && !mainWindow.serverStudioActive
            message: mainWindow.statusMessage
            messageLevel: mainWindow.statusMessageLevel
            logPanelVisible: mainWindow.logPanelVisible
            zoomPercent: cppUiZoom.zoomPercent

            onLogToggleRequested: mainWindow.logPanelVisible = !mainWindow.logPanelVisible
            onZoomResetRequested: cppUiZoom.resetZoom()
        }

        // The launcher is the entry point when no project is active and the
        // client is not connected through Server Studio.
        LauncherScreen {
            id: launcherScreen

            anchors.fill: parent
            darkTheme: mainWindow.darkTheme
            visible: !mainWindow.workspaceActive && !mainWindow.serverStudioActive

            onOpenProjectRequested: openProjectDialog.open()
            onCreateProjectRequested: newProjectDialog.open()
            onOpenServerStudioRequested: mainWindow.serverStudioActive = true
        }

        // Server Studio workspace: runs and controls the local OPC UA server runtime.
        ServerStudioScreen {
            id: serverStudioScreen

            anchors.fill: parent
            darkTheme: mainWindow.darkTheme
            visible: mainWindow.serverStudioActive

            onCloseRequested: mainWindow.serverStudioActive = false
        }
    }

    // Zoom shortcuts work on every screen of the main window. "Ctrl+=" covers
    // layouts where "+" needs Shift; numeric keypad keys match as well.
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        onActivated: cppUiZoom.zoomIn()
    }

    Shortcut {
        sequences: [StandardKey.ZoomOut]
        onActivated: cppUiZoom.zoomOut()
    }

    Shortcut {
        sequence: "Ctrl+0"
        onActivated: cppUiZoom.resetZoom()
    }

    // Full screen works on every screen of the main window, like the zoom keys.
    Shortcut {
        sequence: "F11"
        onActivated: mainWindow.toggleFullScreen()
    }

    // Short-lived zoom badge in the top-right corner, so a zoom change is
    // confirmed on every screen, including those without a status bar.
    ToolTip {
        id: zoomIndicator

        parent: zoomLayer
        x: zoomLayer.width - width - 12
        y: 52
        timeout: 1200
        text: qsTr("Zoom: %1%").arg(cppUiZoom.zoomPercent)
    }

    Connections {
        target: cppUiZoom

        function onZoomChanged() {
            zoomIndicator.open()
        }
    }

    // QQuickOverlay re-applies the window size on every window resize; restore
    // the zoomed logical size right afterwards.
    Connections {
        target: mainWindow.Overlay.overlay

        function onWidthChanged() {
            mainWindow.syncOverlayZoom()
        }

        function onHeightChanged() {
            mainWindow.syncOverlayZoom()
        }
    }

    Connections {
        target: mainScreen.logPanel

        function onCloseRequested() {
            mainWindow.logPanelVisible = false
        }
    }

    Connections {
        target: mainScreen.trendPanel

        function onCloseRequested() {
            mainWindow.trendPanelVisible = false
        }
    }

    Connections {
        target: cppServerStudio

        function onNotification(level, message) {
            mainWindow.showNotification(level, message, "studio")
        }

        function onStatusRetranslated(level, message) {
            mainWindow.refreshStatus(level, message, "studio")
        }

        // After connecting the client to the local runtime, leave Server Studio
        // so the client workspace (shown once connected) becomes visible.
        function onOpenInClientRequested() {
            mainWindow.serverStudioActive = false
        }
    }

    Connections {
        target: mainScreen.menuBar

        function onQuitRequested() {
            mainWindow.close()
        }

        function onApiServerConnectionRequested() {
            if (cppManagerOpcUa.connected)
                cppManagerOpcUa.disconnectFromServer()
            else
                apiServerDialog.open()
        }

        // The menu switch stores an explicit choice, so it persists across starts.
        function onThemeToggleRequested() {
            cppTheme.mode = mainWindow.darkTheme ? "light" : "dark"
        }

        function onFullScreenToggleRequested() {
            mainWindow.toggleFullScreen()
        }

        function onLastConnectionRequested() {
            cppManagerOpcUa.connectToLast()
        }

        function onNewProjectRequested() {
            mainWindow.runGuarded(() => newProjectDialog.open())
        }

        function onSettingsRequested() {
            settingsDialog.open()
        }

        function onAboutRequested() {
            aboutDialog.open()
        }

        function onCheckForUpdatesRequested() {
            cppUpdate.checkNow()
            updateDialog.open()
        }

        function onHelpRequested() {
            const lang = cppLocale ? cppLocale.currentLanguage : "en"
            const url = cppAppInfo.helpIndexUrl(lang, mainWindow.darkTheme)
            if (!url || url.toString() === "") {
                console.warn("Help documentation was not found next to the application.")
                return
            }
            // Create the WebView-backed window lazily on first use, passing the
            // real start URL so the web view never navigates to an empty URL.
            if (!mainWindow.helpViewer)
                mainWindow.helpViewer = helpViewerComponent.createObject(mainWindow, { startUrl: url })
            if (mainWindow.helpViewer)
                mainWindow.helpViewer.openAt(url)
        }

        function onLogPanelToggleRequested() {
            mainWindow.logPanelVisible = !mainWindow.logPanelVisible
        }

        function onTrendPanelToggleRequested() {
            mainWindow.trendPanelVisible = !mainWindow.trendPanelVisible
        }

        function onZoomInRequested() {
            cppUiZoom.zoomIn()
        }

        function onZoomOutRequested() {
            cppUiZoom.zoomOut()
        }

        function onZoomResetRequested() {
            cppUiZoom.resetZoom()
        }

        function onOpenProjectRequested() {
            mainWindow.runGuarded(() => openProjectDialog.open())
        }

        function onOpenRecentRequested(index) {
            mainWindow.runGuarded(() => cppProjectManager.openRecent(index))
        }

        function onCloseProjectRequested() {
            mainWindow.runGuarded(() => cppProjectManager.closeProject())
        }

        function onSaveProjectRequested() {
            cppProjectManager.saveProject()
        }

        function onSaveProjectAsRequested() {
            saveProjectDialog.open()
        }

        function onServerManagerRequested() {
            mainWindow.serverStudioActive = true
        }

        function onCloneToServerStudioRequested() {
            // Cloning replaces the current Server Studio project; guard unsaved work.
            mainWindow.runServerStudioGuarded(() => mainWindow.doCloneToServerStudio())
        }
    }

    Connections {
        target: mainScreen.topActions

        function onConnectToggleRequested() {
            if (cppManagerOpcUa.connected)
                cppManagerOpcUa.disconnectFromServer()
            else
                apiServerDialog.open()
        }

        function onConnectToLastRequested() {
            cppManagerOpcUa.connectToLast()
        }

        function onConnectionSettingsRequested() {
            apiServerDialog.open()
        }

        function onSaveProjectRequested() {
            cppProjectManager.saveProject()
        }
    }

    FileDialog {
        id: openProjectDialog

        title: qsTr("Open Project")
        nameFilters: [qsTr("OPC UA projects (*.uaproj)"), qsTr("All files (*)")]
        onAccepted: cppProjectManager.openProject(selectedFile)
    }

    // Guided create-project form: the user types a name, sees the resulting
    // <name>.uaproj file and full path, and can change the destination folder.
    Dialog {
        id: newProjectDialog

        /*! Local path of the destination folder for the new project. */
        property string projectFolder: ""

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 560)
        title: qsTr("Create New Project")
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        onOpened: {
            newProjectNameField.text = ""
            newProjectDialog.projectFolder = cppProjectManager.defaultProjectsDir
            newProjectNameField.forceActiveFocus()
        }

        onAccepted: {
            const name = newProjectNameField.text.trim()
            if (cppProjectManager.createProject(name, newProjectDialog.projectFolder))
                cppProjectManager.setDefaultProjectsDir(newProjectDialog.projectFolder)
            else
                Qt.callLater(() => newProjectDialog.open()) // failed (e.g. name taken) — reopen to fix
        }

        Component.onCompleted: {
            const okButton = standardButton(Dialog.Ok)
            if (okButton)
                okButton.enabled = Qt.binding(() =>
                    newProjectNameField.text.trim().length > 0
                    && newProjectDialog.projectFolder.length > 0)
        }

        GridLayout {
            width: parent.width
            columns: 3
            columnSpacing: 10
            rowSpacing: 8

            Label { text: qsTr("Name:"); Layout.preferredWidth: 110 }
            TextField {
                id: newProjectNameField
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                placeholderText: qsTr("Project name")
                onAccepted: newProjectDialog.accept()
            }
            Item { Layout.preferredWidth: 100 }

            Label { text: qsTr("File:"); Layout.preferredWidth: 110 }
            Label {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                elide: Text.ElideMiddle
                text: (newProjectNameField.text.trim().length > 0
                       ? newProjectNameField.text.trim() : qsTr("<name>")) + ".uaproj"
            }

            Label { text: qsTr("Location:"); Layout.preferredWidth: 110 }
            TextField {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                readOnly: true
                text: newProjectDialog.projectFolder
            }
            Button {
                text: qsTr("Browse…")
                Layout.preferredWidth: 100
                Layout.preferredHeight: 44
                onClicked: newProjectFolderDialog.open()
            }

            Label { text: qsTr("Full path:"); Layout.preferredWidth: 110 }
            Label {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                elide: Text.ElideMiddle
                opacity: 0.7
                text: newProjectDialog.projectFolder + "/"
                      + (newProjectNameField.text.trim().length > 0
                         ? newProjectNameField.text.trim() : qsTr("<name>")) + ".uaproj"
            }
        }
    }

    FolderDialog {
        id: newProjectFolderDialog

        title: qsTr("Select Project Folder")
        currentFolder: Qt.resolvedUrl(cppProjectManager.defaultProjectsDir)
        onAccepted: newProjectDialog.projectFolder = cppProjectManager.toLocalPath(selectedFolder)
    }

    FileDialog {
        id: saveProjectDialog

        title: qsTr("Save Project As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "uaproj"
        nameFilters: [qsTr("OPC UA projects (*.uaproj)")]
        onAccepted: cppProjectManager.saveProjectAs(selectedFile)
    }

    Dialog {
        id: unsavedDialog

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 460)
        title: qsTr("Unsaved changes")
        modal: true
        focus: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        // Save the project, then continue only if the save succeeded.
        onAccepted: {
            if (cppProjectManager.saveProject())
                mainWindow.proceedPending()
            else
                mainWindow.pendingAction = null
        }
        // Discard changes and continue with the deferred action.
        onDiscarded: {
            unsavedDialog.close()
            mainWindow.proceedPending()
        }
        // Cancel abandons the deferred action.
        onRejected: mainWindow.pendingAction = null

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("The project \"%1\" has unsaved changes. Save them before continuing?")
                      .arg(cppProjectManager.activeProjectName)
        }
    }

    // Guards Server Studio project loss on clone and on application quit.
    Dialog {
        id: serverStudioUnsavedDialog

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 460)
        title: qsTr("Unsaved server changes")
        modal: true
        focus: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        // Save the server project, then continue only if the save succeeded.
        onAccepted: {
            if (cppServerStudio.saveProject())
                mainWindow.proceedServerStudioPending()
            else
                mainWindow.serverStudioPendingAction = null
        }
        // Discard changes and continue with the deferred action.
        onDiscarded: {
            serverStudioUnsavedDialog.close()
            mainWindow.proceedServerStudioPending()
        }
        // Cancel abandons the deferred action.
        onRejected: mainWindow.serverStudioPendingAction = null

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("The server project \"%1\" has unsaved changes. Save them before continuing?")
                      .arg(cppServerStudio.projectName)
        }
    }

    // Clone options: choose whether to keep the server's original node ids and
    // namespaces, then start the (asynchronous) clone and switch to Server Studio.
    Dialog {
        id: cloneOptionsDialog

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 480)
        title: qsTr("Clone server to Server Studio")
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        onAccepted: {
            if (cppServerStudio.cloneFromClient(preserveIdsCheck.checked))
                mainWindow.serverStudioActive = true
        }

        ColumnLayout {
            width: parent.width
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The connected server's whole Objects address space will be browsed "
                           + "and its variable values read into a new server project.")
            }
            CheckBox {
                id: preserveIdsCheck
                text: qsTr("Preserve original NodeIds and namespaces")
                checked: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                font.pixelSize: 12
                text: qsTr("When off, nodes are renamed by browse path under a single clone "
                           + "namespace.")
            }
        }
    }

    Connections {
        target: cppProjectManager

        function onProjectError(message) {
            projectErrorLabel.text = message
            projectErrorDialog.open()
        }

        function onNotification(level, message) {
            mainWindow.showNotification(level, message, "project")
        }

        function onStatusRetranslated(level, message) {
            mainWindow.refreshStatus(level, message, "project")
        }
    }

    Dialog {
        id: projectErrorDialog

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 480)
        title: qsTr("Project error")
        modal: true
        focus: true
        standardButtons: Dialog.Ok
        closePolicy: Popup.CloseOnEscape

        Label {
            id: projectErrorLabel

            width: parent.width
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: settingsDialog

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 620)
        title: qsTr("Settings")
        modal: true
        focus: true
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape

        GridLayout {
            width: parent.width
            columns: 3
            columnSpacing: 10
            rowSpacing: 8

            // Language applies live to the whole UI and is remembered for the next
            // launch; missing translations fall back to English.
            Label { text: qsTr("Interface language:"); Layout.preferredWidth: 180 }
            Base.BsLanguageSelector {
                id: settingsLanguageCombo
                Layout.fillWidth: true
                Layout.preferredHeight: 44
            }
            Item { Layout.preferredWidth: 100 }

            Label { text: qsTr("Default projects folder:"); Layout.preferredWidth: 180 }
            TextField {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                readOnly: true
                text: cppProjectManager.defaultProjectsDir
            }
            Button {
                text: qsTr("Browse…")
                Layout.preferredWidth: 100
                Layout.preferredHeight: 44
                onClicked: settingsFolderDialog.open()
            }

            // Automatic startup update check; disabled until the update feature is
            // enabled in the product configuration.
            Label { text: qsTr("Updates:"); Layout.preferredWidth: 180 }
            CheckBox {
                Layout.fillWidth: true
                Layout.columnSpan: 2
                text: qsTr("Check for updates automatically on startup")
                enabled: cppUpdate.featureEnabled
                checked: cppUpdate.checkAutomatically
                onToggled: cppUpdate.checkAutomatically = checked
            }
        }
    }

    FolderDialog {
        id: settingsFolderDialog

        title: qsTr("Select Default Projects Folder")
        currentFolder: Qt.resolvedUrl(cppProjectManager.defaultProjectsDir)
        onAccepted: cppProjectManager.setDefaultProjectsDir(selectedFolder)
    }

    Dialog {
        id: apiServerDialog

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 940)
        height: Math.min(mainWindow.uiHeight - 80, implicitHeight)
        title: qsTr("Connect to OPC UA server")
        modal: true
        focus: true
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: Base.BsOpcUaConnectionForm {
            implicitWidth: 900
        }
    }

    Connections {
        target: cppManagerOpcUa

        function onNotification(level, message) {
            mainWindow.showNotification(level, message, "opcua")
        }

        function onStatusRetranslated(level, message) {
            mainWindow.refreshStatus(level, message, "opcua")
        }

        function onConnectedChanged() {
            if (cppManagerOpcUa.connected && apiServerDialog.opened)
                apiServerDialog.close()
        }

        function onPasswordRequired(userName) {
            reconnectPasswordField.text = ""
            reconnectPasswordDialog.userName = userName
            reconnectPasswordDialog.open()
        }
    }

    Dialog {
        id: reconnectPasswordDialog

        /*! User name the stored connection authenticates as. */
        property string userName: ""

        anchors.centerIn: Overlay.overlay
        width: Math.min(mainWindow.uiWidth - 80, 420)
        title: qsTr("Password required")
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        onAccepted: cppManagerOpcUa.provideReconnectPassword(reconnectPasswordField.text)

        Column {
            width: parent.width
            spacing: 8

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("Enter the password for user \"%1\".")
                          .arg(reconnectPasswordDialog.userName)
            }

            TextField {
                id: reconnectPasswordField

                width: parent.width
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
                onAccepted: reconnectPasswordDialog.accept()
            }
        }
    }

    // AboutDialog and UpdateDialog size themselves from their parent; the zoom
    // layer provides the logical (zoom-adjusted) window size.
    AboutDialog {
        id: aboutDialog

        parent: zoomLayer
        darkTheme: mainWindow.darkTheme
    }

    UpdateDialog {
        id: updateDialog

        parent: zoomLayer
        darkTheme: mainWindow.darkTheme

        // Resolve unsaved client and Server Studio changes first (the user may
        // cancel), then start the Maintenance Tool; quitting follows through
        // onQuitRequested below.
        onInstallUpdateRequested: {
            mainWindow.runGuarded(() => mainWindow.runServerStudioGuarded(() => cppUpdate.installUpdate()))
        }
    }

    // An automatic startup check surfaces the dialog only when a newer version is
    // found, so it never interrupts the user when the application is up to date.
    // Quitting happens only after the user explicitly started the Maintenance
    // Tool, so the update can replace the application files.
    Connections {
        target: cppUpdate

        function onStatusChanged() {
            if (cppUpdate.updateAvailable)
                updateDialog.open()
        }

        function onQuitRequested() {
            Qt.quit()
        }
    }

    Component.onCompleted: {
        mainWindow.restoreUiLayout()
        mainWindow.syncOverlayZoom()
        if (cppUpdate.featureEnabled && cppUpdate.checkAutomatically)
            cppUpdate.checkNow()
    }

    // The offline documentation window is created on demand (see onHelpRequested)
    // so the WebView backend is only started when the user opens Help.
    property var helpViewer: null

    Component {
        id: helpViewerComponent

        HelpViewerWindow {
            darkTheme: mainWindow.darkTheme
        }
    }

}
