import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsNodeAttributes
    \inqmlmodule Base
    \brief Right-side panel showing the attributes and structured value of the selected node.

    The top section reads \c cppManagerOpcUa.attributesModel and lists the main
    attributes of the node currently selected in \l BsAddressSpaceTree as
    attribute/value pairs, mirroring the UaExpert Attributes view. The bottom
    section renders the decoded structured value (\c cppManagerOpcUa.structuredValueText)
    as JSON or XML, following the format chosen in the View menu.

    Attribute names, attribute values, and the structured value are read-only
    but can be selected with the mouse and copied with the standard Copy
    shortcut (Ctrl+C); Select All (Ctrl+A) selects the whole focused field.
*/
Rectangle {
    id: root

    /*! Height of a single attribute row. */
    property int rowHeight: 28

    /*! Read-only, mouse-selectable single-line text used for attribute rows. */
    component SelectableText: TextInput {
        readOnly: true
        selectByMouse: true
        clip: true
        // Plain TextInput uses the application font, while Label follows the
        // Controls theme font; match the Label rows this component replaces.
        font.family: labelFontReference.font.family
        font.pixelSize: labelFontReference.font.pixelSize
        color: Material.foreground
        selectionColor: Material.textSelectionColor
        selectedTextColor: Material.foreground

        // Invisible font source carrying the theme font of a Label.
        Label {
            id: labelFontReference

            visible: false
        }
    }

    color: Material.background
    border.color: BsTheme.dividerColor
    border.width: 1
    clip: true

    // Read-only TextInput/TextEdit ignore ShortcutOverride, so the window-wide Copy
    // shortcut of BsAddressSpaceTreePane would win and copy the selected node id
    // instead of the selected text. The ignored event propagates up from the focused
    // field to this panel; claiming it here lets that field handle the key press
    // itself. Ctrl+C without a selection still falls through to the tree shortcut.
    Keys.onShortcutOverride: (event) => {
        const input = root.Window.activeFocusItem as TextInput
        const edit = root.Window.activeFocusItem as TextEdit
        if (!input && !edit)
            return
        const selection = input ? input.selectedText : edit.selectedText
        event.accepted = (event.matches(StandardKey.Copy) && selection.length > 0)
                         || event.matches(StandardKey.SelectAll)
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        // Top section: attribute/value list of the selected node.
        Item {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 120

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: BsTheme.headerColor

                    Label {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        text: qsTr("ATTRIBUTES")
                        font.pixelSize: 12
                        font.bold: true
                        font.letterSpacing: 1.2
                        color: BsTheme.accentTextColor
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: BsTheme.dividerColor
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 32
                        visible: attributesView.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Select a node to view its attributes.")
                        color: Material.foreground
                        opacity: 0.6
                    }

                    ListView {
                        id: attributesView

                        objectName: "attributesView"
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: cppManagerOpcUa.attributesModel
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.vertical: ScrollBar {}

                        delegate: Item {
                            id: attributeDelegate

                            required property string attribute
                            required property string value

                            width: attributesView.width
                            height: root.rowHeight

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8

                                SelectableText {
                                    objectName: "attributeNameText"
                                    Layout.preferredWidth: parent.width * 0.4
                                    Layout.alignment: Qt.AlignVCenter
                                    text: attributeDelegate.attribute
                                    font.bold: true
                                }

                                SelectableText {
                                    objectName: "attributeValueText"
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: attributeDelegate.value
                                }
                            }
                        }
                    }
                }
            }
        }

        // Bottom section: structured value rendered as JSON or XML.
        Item {
            SplitView.preferredHeight: 240
            SplitView.minimumHeight: 100

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: BsTheme.headerColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 6
                        spacing: 8

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            // valueFormat mirrors OpcUaManager::ValueFormat (1 = FormatXml).
                            text: qsTr("VALUE") + " ("
                                  + (cppManagerOpcUa.valueFormat === 1
                                     ? qsTr("XML") : qsTr("JSON")) + ")"
                            font.pixelSize: 12
                            font.bold: true
                            font.letterSpacing: 1.2
                            color: BsTheme.accentTextColor
                        }

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            Layout.alignment: Qt.AlignVCenter
                            text: qsTr("Refresh")
                            enabled: cppManagerOpcUa.connected
                            onClicked: cppManagerOpcUa.refreshStructuredValue()
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: BsTheme.dividerColor
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 32
                        visible: !cppManagerOpcUa.structuredValueAvailable
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Select a variable node to view its structured value.")
                        color: Material.foreground
                        opacity: 0.6
                    }

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: cppManagerOpcUa.structuredValueAvailable
                        clip: true

                        TextArea {
                            id: structuredValueText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            text: cppManagerOpcUa.structuredValueText
                            font.family: "Consolas"
                            font.pixelSize: 13
                            // Base text color; the C++ QSyntaxHighlighter overrides
                            // only the matched JSON/XML token ranges.
                            color: Material.foreground

                            // Flat, borderless background so the Value view matches the
                            // other panels; the default Material TextArea draws a rounded
                            // outlined container that no other panel has.
                            background: Rectangle { color: "transparent" }

                            // Tracks the active Material theme so the highlighter
                            // palette can follow light/dark theme toggles.
                            property bool appDarkTheme: Material.theme === Material.Dark
                            onAppDarkThemeChanged: {
                                if (cppManagerOpcUa)
                                    cppManagerOpcUa.setStructuredValueDarkTheme(appDarkTheme)
                            }

                            // Attach the structured-value syntax highlighter to this
                            // TextArea's document via the existing context property, so
                            // the Base module needs no Cpp.* import, and seed its palette
                            // with the current theme.
                            Component.onCompleted: {
                                if (cppManagerOpcUa) {
                                    cppManagerOpcUa.installStructuredValueHighlighter(
                                        structuredValueText.textDocument)
                                    cppManagerOpcUa.setStructuredValueDarkTheme(
                                        Material.theme === Material.Dark)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
