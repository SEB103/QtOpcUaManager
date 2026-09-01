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
*/
Rectangle {
    id: root

    /*! Height of a single attribute row. */
    property int rowHeight: 28

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

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
                    color: Qt.lighter(Material.background, 1.3)

                    Label {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        text: qsTr("ATTRIBUTES")
                        font.pixelSize: 12
                        font.bold: true
                        font.letterSpacing: 1.2
                        color: Material.accent
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Material.dividerColor
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

                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: cppManagerOpcUa.attributesModel
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            id: attributeDelegate

                            required property int index
                            required property string attribute
                            required property string value

                            width: attributesView.width
                            height: root.rowHeight
                            color: index % 2 === 0
                                   ? "transparent"
                                   : Qt.darker(Material.background, 1.05)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8

                                Label {
                                    Layout.preferredWidth: parent.width * 0.4
                                    Layout.alignment: Qt.AlignVCenter
                                    text: attributeDelegate.attribute
                                    font.bold: true
                                    elide: Text.ElideRight
                                    color: Material.foreground
                                }

                                Label {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: attributeDelegate.value
                                    elide: Text.ElideRight
                                    color: Material.foreground
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
                    color: Qt.lighter(Material.background, 1.3)

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
                            color: Material.accent
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
                        color: Material.dividerColor
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
