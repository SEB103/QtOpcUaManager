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
           ? qsTr("OPC UA Manager — %1%2")
                 .arg(cppProjectManager.activeProjectName)
                 .arg(cppProjectManager.dirty ? "*" : "")
           : qsTr("OPC UA Manager")

    /*! Whether the application currently uses the dark Material theme. */
    property bool darkTheme: Application.styleHints.colorScheme === Qt.Dark

    /*! Pending action deferred until the unsaved-changes prompt is answered. */
    property var pendingAction: null

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

    // Guard application exit: prompt to save when the active project is dirty.
    onClosing: (close) => {
        if (cppProjectManager.dirty) {
            close.accepted = false
            runGuarded(() => Qt.quit())
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
        visible: cppProjectManager.hasActiveProject
    }

    // The launcher is the entry point when no project is active.
    LauncherScreen {
        id: launcherScreen
        anchors.fill: parent
        darkTheme: mainWindow.darkTheme
        visible: !cppProjectManager.hasActiveProject

        onOpenProjectRequested: openProjectDialog.open()
        onCreateProjectRequested: newProjectDialog.open()
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

    Connections {
        target: cppProjectManager

        function onProjectError(message) {
            projectErrorLabel.text = message
            projectErrorDialog.open()
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

}
