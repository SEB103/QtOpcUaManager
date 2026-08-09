import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsNodeAttributes
    \inqmlmodule Base
    \brief Right-side panel showing the attributes of the selected node.

    The panel reads \c cppManagerOpcUa.attributesModel and lists the main
    attributes of the node currently selected in \l BsAddressSpaceTree as
    attribute/value pairs, mirroring the UaExpert Attributes view.
*/
Rectangle {
    id: root

    /*! Height of a single attribute row. */
    property int rowHeight: 28

    color: Material.background
    radius: 6
    border.color: Material.primary
    border.width: 1
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "transparent"

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
