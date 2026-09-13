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
    visible: true
    title: cppProjectManager.hasActiveProject
           ? qsTr("%1 — %2%3")
                 .arg(cppAppInfo.appName)
                 .arg(cppProjectManager.activeProjectName)
                 .arg(cppProjectManager.dirty ? "*" : "")
           : cppAppInfo.appName

    /*! Whether the application currently uses the dark Material theme. */
    property bool darkTheme: Application.styleHints.colorScheme === Qt.Dark

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
        Records the outcome \a message at severity \a level.

        Every outcome lands in the status bar; the banner additionally surfaces
        warnings and errors, which the user has to notice.
    */
    function showNotification(level, message) {
        mainWindow.statusMessage = message
        mainWindow.statusMessageLevel = level
        mainScreen.notificationBanner.show(level, message)
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

    /*! Clones the browsed client address space into Server Studio and shows it. */
    function doCloneToServerStudio() {
        if (cppServerStudio.cloneFromClient())
            mainWindow.serverStudioActive = true
    }

    // Guard application exit: prompt to save when the active project or the
    // Server Studio project is dirty. Resolve the client project first, then the
    // Server Studio project, then quit.
    onClosing: (close) => {
        if (cppProjectManager.dirty || cppServerStudio.dirty) {
            close.accepted = false
            runGuarded(() => mainWindow.runServerStudioGuarded(() => Qt.quit()))
        }
    }

    Material.theme: darkTheme ? Material.Dark : Material.Light
    Material.accent: Material.Teal
    Material.primary: Material.BlueGrey

    // The workspace is present but hidden until a project is active, so its menu
    // and browser bindings stay wired across project open/close.
    MainScreen {
        id: mainScreen
        anchors.fill: parent
        darkTheme: mainWindow.darkTheme
        visible: mainWindow.workspaceActive && !mainWindow.serverStudioActive
        logPanelVisible: mainWindow.logPanelVisible
        trendPanelVisible: mainWindow.trendPanelVisible
    }

    // The status bar belongs to the workspace; the launcher has nothing to report.
    footer: Base.BsStatusBar {
        visible: mainWindow.workspaceActive && !mainWindow.serverStudioActive
        message: mainWindow.statusMessage
        messageLevel: mainWindow.statusMessageLevel
        logPanelVisible: mainWindow.logPanelVisible

        onLogToggleRequested: mainWindow.logPanelVisible = !mainWindow.logPanelVisible
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

    // The launcher is the entry point when no project is active and the client
    // is not connected through Server Studio.
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

    Connections {
        target: cppServerStudio

        function onNotification(level, message) {
            mainWindow.showNotification(level, message)
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

        function onThemeToggleRequested() {
            mainWindow.darkTheme = !mainWindow.darkTheme
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

        function onLogPanelToggleRequested() {
            mainWindow.logPanelVisible = !mainWindow.logPanelVisible
        }

        function onTrendPanelToggleRequested() {
            mainWindow.trendPanelVisible = !mainWindow.trendPanelVisible
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

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 560)
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

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 460)
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

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 460)
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

    Connections {
        target: cppProjectManager

        function onProjectError(message) {
            projectErrorLabel.text = message
            projectErrorDialog.open()
        }

        function onNotification(level, message) {
            mainWindow.showNotification(level, message)
        }
    }

    Dialog {
        id: projectErrorDialog

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 480)
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

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 620)
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
            ComboBox {
                id: settingsLanguageCombo
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                model: cppLocale.availableLanguages
                textRole: "name"
                valueRole: "code"
                currentIndex: indexOfValue(cppLocale.currentLanguage)
                onActivated: cppLocale.setLanguage(currentValue)
                Accessible.name: qsTr("Interface language")
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

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 940)
        height: Math.min(mainWindow.height - 80, implicitHeight)
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
            mainWindow.showNotification(level, message)
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

        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        width: Math.min(mainWindow.width - 80, 420)
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

    AboutDialog {
        id: aboutDialog

        darkTheme: mainWindow.darkTheme
    }

}
