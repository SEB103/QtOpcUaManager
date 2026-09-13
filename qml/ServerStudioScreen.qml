import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts

/*!
    \qmltype ServerStudioScreen
    \inqmlmodule OpcUaManager
    \brief Server Studio workspace: design, run and test a local OPC UA server.

    Uses \c cppServerStudio to create/open/save a .uaserver project, edit its
    address space (folders, objects and variables) in a tree with a property
    editor, run the headless runtime built from the project, and open the
    running endpoint in the existing OPC UA client.
*/
Pane {
    id: studio

    padding: 0

    /*! Whether child controls should follow the dark theme state. */
    property bool darkTheme: false

    /*!
        \qmlsignal ServerStudioScreen::closeRequested()
        Emitted when the user leaves Server Studio and returns to the launcher.
    */
    signal closeRequested()

    /*! Node id used as the parent for newly added nodes (empty = Objects root). */
    readonly property string addParentId: cppServerStudio.selectedNodeId

    background: Rectangle {
        color: Material.background
    }

    // Reloads the property editor fields whenever the selection changes.
    Connections {
        target: cppServerStudio

        function onSelectedNodeChanged() {
            studio.loadSelectedIntoEditor()
        }
    }

    /*! Copies the selected node's fields into the editable property controls. */
    function loadSelectedIntoEditor() {
        const node = cppServerStudio.selectedNode
        displayNameField.text = node.displayName !== undefined ? node.displayName : ""
        descriptionField.text = node.description !== undefined ? node.description : ""
        if (node.isVariable) {
            const typeIndex = cppServerStudio.dataTypeNames.indexOf(node.dataType)
            dataTypeCombo.currentIndex = typeIndex >= 0 ? typeIndex : 0
            arrayCheck.checked = node.valueRank === 1
            writableCheck.checked = node.writable === true
            initialValueField.text = node.initialValue !== undefined ? node.initialValue : ""

            const simIndex = cppServerStudio.simulationKindNames.indexOf(node.simulationKind)
            simKindCombo.currentIndex = simIndex >= 0 ? simIndex : 0
            simIntervalField.text = node.simInterval !== undefined ? node.simInterval : "1000"
            simMinField.text = node.simMin !== undefined ? node.simMin : "0"
            simMaxField.text = node.simMax !== undefined ? node.simMax : "100"
            simStepField.text = node.simStep !== undefined ? node.simStep : "1"
            simPeriodField.text = node.simPeriod !== undefined ? node.simPeriod : "10000"

            const enumName = node.enumTypeName !== undefined ? node.enumTypeName : ""
            enumTypeCombo.currentIndex = enumName === ""
                ? 0 : (cppServerStudio.enumTypeNames.indexOf(enumName) + 1)
        }
    }

    /*! Formats an enum type's entries as "value=name" pairs for display. */
    function enumEntriesText(entries) {
        if (!entries || entries.length === 0)
            return qsTr("(no values)")
        let parts = []
        for (let i = 0; i < entries.length; ++i)
            parts.push(entries[i].value + "=" + entries[i].name)
        return parts.join(", ")
    }

    /*! Applies the editable property controls back to the selected node. */
    function applyEditor() {
        const fields = {
            "displayName": displayNameField.text,
            "description": descriptionField.text
        }
        if (cppServerStudio.selectedNode.isVariable) {
            fields["dataType"] = dataTypeCombo.currentText
            fields["valueRank"] = arrayCheck.checked ? 1 : -1
            fields["writable"] = writableCheck.checked
            fields["initialValue"] = initialValueField.text
            fields["simulationKind"] = simKindCombo.currentText
            fields["simInterval"] = parseFloat(simIntervalField.text) || 1000
            fields["simMin"] = parseFloat(simMinField.text) || 0
            fields["simMax"] = parseFloat(simMaxField.text) || 0
            fields["simStep"] = parseFloat(simStepField.text) || 0
            fields["simPeriod"] = parseFloat(simPeriodField.text) || 10000
            fields["enumTypeName"] = enumTypeCombo.currentText === "(none)"
                ? "" : enumTypeCombo.currentText
        }
        cppServerStudio.updateNode(cppServerStudio.selectedNodeId, fields)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // Header: navigation and project actions.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: qsTr("← Back")
                flat: true
                onClicked: studio.closeRequested()
            }

            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                font.pixelSize: 20
                font.bold: true
                color: Material.foreground
                text: cppServerStudio.hasProject
                      ? (cppServerStudio.projectName + (cppServerStudio.dirty ? " *" : ""))
                      : qsTr("Server Studio")
            }

            Button {
                text: qsTr("New")
                onClicked: newProjectDialog.open()
            }
            Button {
                text: qsTr("Open…")
                onClicked: openDialog.open()
            }
            Button {
                text: qsTr("Save")
                enabled: cppServerStudio.hasProject
                onClicked: {
                    if (!cppServerStudio.saveProject())
                        saveAsDialog.open()
                }
            }
            Button {
                text: qsTr("Save As…")
                enabled: cppServerStudio.hasProject
                onClicked: saveAsDialog.open()
            }
            Button {
                text: qsTr("Import NodeSet2…")
                onClicked: importNodeSetDialog.open()
            }
            Button {
                text: qsTr("Export NodeSet2…")
                enabled: cppServerStudio.hasProject
                onClicked: exportNodeSetDialog.open()
            }
        }

        // Placeholder shown until a project exists.
        ColumnLayout {
            visible: !cppServerStudio.hasProject
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Item { Layout.fillHeight: true }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Create a new server project or open an existing one.")
                color: Material.foreground
                opacity: 0.7
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Button {
                    text: qsTr("New Server Project")
                    highlighted: true
                    onClicked: newProjectDialog.open()
                }
                Button {
                    text: qsTr("Open…")
                    onClicked: openDialog.open()
                }
            }
            Item { Layout.fillHeight: true }
        }

        // Designer: address-space tree on the left, property editor on the right.
        RowLayout {
            visible: cppServerStudio.hasProject
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        text: qsTr("+ Folder")
                        onClicked: cppServerStudio.addFolder(studio.addParentId, qsTr("Folder"))
                    }
                    Button {
                        text: qsTr("+ Variable")
                        onClicked: cppServerStudio.addVariable(studio.addParentId, qsTr("Variable"),
                                                               "Double", -1, true)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Remove")
                        enabled: cppServerStudio.selectedNodeId.length > 0
                        onClicked: cppServerStudio.removeNode(cppServerStudio.selectedNodeId)
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 2

                    TreeView {
                        id: treeView
                        anchors.fill: parent
                        clip: true
                        model: cppServerStudio.nodeModel
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: TreeViewDelegate {
                            id: treeDelegate

                            required property string nodeId
                            required property string displayName
                            required property string kindName

                            implicitHeight: 30
                            contentItem: Label {
                                text: treeDelegate.displayName
                                      + " (" + treeDelegate.kindName + ")"
                                color: Material.foreground
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: treeDelegate.nodeId === cppServerStudio.selectedNodeId
                                       ? Qt.lighter(Material.background, 1.5)
                                       : Material.background
                            }

                            onClicked: cppServerStudio.selectedNodeId = treeDelegate.nodeId
                        }
                    }
                }
            }

            // Property editor for the selected node.
            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        visible: cppServerStudio.selectedNodeId.length === 0
                        text: qsTr("Select a node to edit its properties.")
                        color: Material.foreground
                        opacity: 0.6
                    }

                    GridLayout {
                        visible: cppServerStudio.selectedNodeId.length > 0
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 8

                        Label { text: qsTr("Node id:") }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: cppServerStudio.selectedNode.nodeId !== undefined
                                  ? cppServerStudio.selectedNode.nodeId : ""
                            color: Material.accent
                        }

                        Label { text: qsTr("Kind:") }
                        Label {
                            text: cppServerStudio.selectedNode.kindName !== undefined
                                  ? cppServerStudio.selectedNode.kindName : ""
                            color: Material.foreground
                        }

                        Label { text: qsTr("Display name:") }
                        TextField {
                            id: displayNameField
                            Layout.fillWidth: true
                        }

                        Label { text: qsTr("Description:") }
                        TextField {
                            id: descriptionField
                            Layout.fillWidth: true
                        }

                        Label {
                            text: qsTr("Data type:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }
                        ComboBox {
                            id: dataTypeCombo
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                            enabled: enumTypeCombo.currentText === "(none)"
                            model: cppServerStudio.dataTypeNames
                        }

                        Label {
                            text: qsTr("Enum type:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }
                        ComboBox {
                            id: enumTypeCombo
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                            model: ["(none)"].concat(cppServerStudio.enumTypeNames)
                        }

                        Label {
                            text: qsTr("Array:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }
                        CheckBox {
                            id: arrayCheck
                            visible: cppServerStudio.selectedNode.isVariable === true
                            text: qsTr("One-dimensional array")
                        }

                        Label {
                            text: qsTr("Writable:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }
                        CheckBox {
                            id: writableCheck
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }

                        Label {
                            text: qsTr("Initial value:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }
                        TextField {
                            id: initialValueField
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                            placeholderText: arrayCheck.checked ? qsTr("comma-separated values")
                                                                : qsTr("value")
                        }

                        Label {
                            text: qsTr("Simulation:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                        }
                        ComboBox {
                            id: simKindCombo
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                            model: cppServerStudio.simulationKindNames
                        }

                        Label {
                            text: qsTr("Interval (ms):")
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && simKindCombo.currentText !== "Manual"
                        }
                        TextField {
                            id: simIntervalField
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && simKindCombo.currentText !== "Manual"
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }

                        Label {
                            text: qsTr("Min / Max:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && simKindCombo.currentText !== "Manual"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && simKindCombo.currentText !== "Manual"
                            spacing: 6
                            TextField {
                                id: simMinField
                                Layout.fillWidth: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                            TextField {
                                id: simMaxField
                                Layout.fillWidth: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                        }

                        Label {
                            text: qsTr("Step:")
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && simKindCombo.currentText === "Counter"
                        }
                        TextField {
                            id: simStepField
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && simKindCombo.currentText === "Counter"
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }

                        Label {
                            text: qsTr("Period (ms):")
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && (simKindCombo.currentText === "Sine"
                                         || simKindCombo.currentText === "Ramp")
                        }
                        TextField {
                            id: simPeriodField
                            Layout.fillWidth: true
                            visible: cppServerStudio.selectedNode.isVariable === true
                                     && (simKindCombo.currentText === "Sine"
                                         || simKindCombo.currentText === "Ramp")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                        }
                    }

                    RowLayout {
                        visible: cppServerStudio.selectedNodeId.length > 0
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Button {
                            text: qsTr("Apply")
                            highlighted: true
                            onClicked: studio.applyEditor()
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // Security configuration.
        Frame {
            visible: cppServerStudio.hasProject
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label {
                    text: qsTr("Security")
                    font.bold: true
                    color: Material.foreground
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    CheckBox {
                        id: anonCheck
                        text: qsTr("Allow anonymous")
                        checked: cppServerStudio.security.allowAnonymous
                        onToggled: cppServerStudio.setSecurityFlags(
                                       checked, noneCheck.checked, encCheck.checked)
                    }
                    CheckBox {
                        id: noneCheck
                        text: qsTr("Offer None endpoint")
                        checked: cppServerStudio.security.allowNone
                        onToggled: cppServerStudio.setSecurityFlags(
                                       anonCheck.checked, checked, encCheck.checked)
                    }
                    CheckBox {
                        id: encCheck
                        text: qsTr("Enable encryption")
                        checked: cppServerStudio.security.enableSecurity
                        onToggled: cppServerStudio.setSecurityFlags(
                                       anonCheck.checked, noneCheck.checked, checked)
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: userField
                        Layout.preferredWidth: 160
                        placeholderText: qsTr("user name")
                    }
                    TextField {
                        id: passField
                        Layout.preferredWidth: 160
                        placeholderText: qsTr("password")
                    }
                    Button {
                        text: qsTr("Add user")
                        enabled: userField.text.trim().length > 0
                        onClicked: {
                            cppServerStudio.addUser(userField.text, passField.text)
                            userField.text = ""
                            passField.text = ""
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: cppServerStudio.security.users

                        delegate: Frame {
                            id: userChip
                            required property var modelData
                            padding: 4

                            RowLayout {
                                spacing: 6
                                Label {
                                    text: userChip.modelData.username
                                    color: Material.foreground
                                }
                                ToolButton {
                                    text: "✕"
                                    flat: true
                                    onClicked: cppServerStudio.removeUser(userChip.modelData.username)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Enumeration types.
        Frame {
            visible: cppServerStudio.hasProject
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Label {
                    text: qsTr("Enumeration types")
                    font.bold: true
                    color: Material.foreground
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: newEnumField
                        Layout.preferredWidth: 200
                        placeholderText: qsTr("new enum type name")
                    }
                    Button {
                        text: qsTr("Add enum type")
                        enabled: newEnumField.text.trim().length > 0
                        onClicked: {
                            cppServerStudio.addEnumType(newEnumField.text)
                            newEnumField.text = ""
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                Repeater {
                    model: cppServerStudio.enumTypes

                    delegate: Frame {
                        id: enumRow
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 6

                        RowLayout {
                            anchors.fill: parent
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: enumRow.modelData.name
                                    font.bold: true
                                    color: Material.foreground
                                }
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.pixelSize: 12
                                    opacity: 0.8
                                    color: Material.foreground
                                    text: studio.enumEntriesText(enumRow.modelData.entries)
                                }
                            }

                            TextField {
                                id: enumEntryValue
                                Layout.preferredWidth: 60
                                placeholderText: qsTr("value")
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                            TextField {
                                id: enumEntryName
                                Layout.preferredWidth: 120
                                placeholderText: qsTr("name")
                            }
                            Button {
                                text: qsTr("Add value")
                                enabled: enumEntryName.text.trim().length > 0
                                onClicked: {
                                    cppServerStudio.addEnumEntry(enumRow.modelData.name,
                                                                 parseInt(enumEntryValue.text) || 0,
                                                                 enumEntryName.text)
                                    enumEntryValue.text = ""
                                    enumEntryName.text = ""
                                }
                            }
                            Button {
                                text: qsTr("Remove")
                                onClicked: cppServerStudio.removeEnumType(enumRow.modelData.name)
                            }
                        }
                    }
                }
            }
        }

        // Runtime control bar.
        Frame {
            visible: cppServerStudio.hasProject
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        radius: 6
                        color: cppServerStudio.running
                               ? Material.color(Material.Green)
                               : (cppServerStudio.crashed
                                  ? Material.color(Material.Red)
                                  : Material.color(Material.Grey))
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: cppServerStudio.stateText
                        color: Material.foreground
                    }
                    Button {
                        text: qsTr("Start")
                        highlighted: true
                        enabled: !cppServerStudio.running && !cppServerStudio.busy
                        onClicked: cppServerStudio.startServer()
                    }
                    Button {
                        text: qsTr("Stop")
                        enabled: cppServerStudio.running || cppServerStudio.busy
                        onClicked: cppServerStudio.stop()
                    }
                    Button {
                        text: qsTr("Restart")
                        enabled: cppServerStudio.running || cppServerStudio.busy
                        onClicked: cppServerStudio.restart()
                    }
                    Button {
                        text: qsTr("Kill")
                        enabled: cppServerStudio.running || cppServerStudio.busy
                        onClicked: cppServerStudio.kill()
                    }
                    Button {
                        text: qsTr("Open in Client")
                        enabled: cppServerStudio.running
                        onClicked: cppServerStudio.openInClient()
                    }
                }

                // Live diagnostics reported by the running runtime.
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    opacity: 0.8
                    color: Material.foreground
                    text: cppServerStudio.diagnosticsText
                }
            }
        }
    }

    // Create-project dialog.
    Dialog {
        id: newProjectDialog

        anchors.centerIn: parent
        width: Math.min(studio.width - 80, 420)
        title: qsTr("New Server Project")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            newProjectNameField.text = ""
            newProjectNameField.forceActiveFocus()
        }
        onAccepted: cppServerStudio.newProject(newProjectNameField.text)

        ColumnLayout {
            width: parent.width
            Label { text: qsTr("Project name:") }
            TextField {
                id: newProjectNameField
                Layout.fillWidth: true
                placeholderText: qsTr("My Test Server")
                onAccepted: newProjectDialog.accept()
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open Server Project")
        nameFilters: [qsTr("Server projects (*.uaserver)"), qsTr("All files (*)")]
        onAccepted: cppServerStudio.openProject(selectedFile)
    }

    FileDialog {
        id: saveAsDialog
        title: qsTr("Save Server Project As")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "uaserver"
        nameFilters: [qsTr("Server projects (*.uaserver)")]
        onAccepted: cppServerStudio.saveProjectAs(selectedFile)
    }

    FileDialog {
        id: importNodeSetDialog
        title: qsTr("Import NodeSet2")
        nameFilters: [qsTr("NodeSet2 files (*.xml)"), qsTr("All files (*)")]
        onAccepted: cppServerStudio.importNodeSet(selectedFile)
    }

    FileDialog {
        id: exportNodeSetDialog
        title: qsTr("Export NodeSet2")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xml"
        nameFilters: [qsTr("NodeSet2 files (*.xml)")]
        onAccepted: cppServerStudio.exportNodeSet(selectedFile)
    }
}
