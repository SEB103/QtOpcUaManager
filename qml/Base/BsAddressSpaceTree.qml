import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsAddressSpaceTree
    \inqmlmodule Base
    \brief Displays the OPC UA server address space as a titled, flexible tree.

    The panel reads \c cppManagerOpcUa.treeModel from the QML context and shows a
    lazy-loading TreeView only while the OPC UA session is connected. Each row is
    rendered as a node-class color badge followed by the localized display name.
*/
Rectangle {
    id: root

    /*! Height of a single tree row. */
    property int rowHeight: 32

    /*!
        Returns the icon resource for the model \a key (the \c iconName role).
        The key encodes the node kind and, for variables, the data-type category.
        Each SVG already carries its own VS Code Material-style fill color, so no
        runtime tinting is needed. Unknown keys fall back to the generic glyph.
    */
    function iconSource(key) {
        switch (key) {
        case "folder": return "qrc:/images/svg/folder.svg"
        case "object": return "qrc:/images/svg/deployed_code.svg"
        case "method": return "qrc:/images/svg/function.svg"
        case "objectType": return "qrc:/images/svg/category.svg"
        case "variableType": return "qrc:/images/svg/category_cyan.svg"
        case "dataType":
        case "var-struct": return "qrc:/images/svg/data_object.svg"
        case "referenceType": return "qrc:/images/svg/share.svg"
        case "view": return "qrc:/images/svg/visibility.svg"
        case "array": return "qrc:/images/svg/data_array.svg"
        case "var-bool": return "qrc:/images/svg/toggle_on.svg"
        case "var-int": return "qrc:/images/svg/tag.svg"
        case "var-uint": return "qrc:/images/svg/tag_indigo.svg"
        case "var-real": return "qrc:/images/svg/tag_cyan.svg"
        case "var-string": return "qrc:/images/svg/text_fields.svg"
        case "var-time": return "qrc:/images/svg/schedule.svg"
        default: return "qrc:/images/svg/variable.svg"
        }
    }

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    /*!
        Expands the ancestors of the node with \a nodeId and scrolls it to the
        vertical center of the tree. Best-effort: it only reveals nodes that are
        already materialized in the lazy tree; when the node is not loaded (for
        example its branch is collapsed) the call is a no-op.
    */
    function revealNode(nodeId) {
        if (!nodeId)
            return
        const idx = cppManagerOpcUa.treeModel.indexForNodeId(nodeId)
        if (!idx || !idx.valid)
            return
        treeView.expandToIndex(idx)
        Qt.callLater(function () {
            treeView.positionViewAtIndex(idx, TableView.AlignCenter)
        })
    }

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
                text: qsTr("ADDRESS SPACE")
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
                visible: !cppManagerOpcUa.connected
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Connect to an OPC UA endpoint to browse the address space.")
                color: Material.foreground
                opacity: 0.6
            }

            TreeView {
                id: treeView

                anchors.fill: parent
                anchors.margins: 4
                visible: cppManagerOpcUa.connected
                clip: true
                model: cppManagerOpcUa.treeModel
                boundsBehavior: Flickable.StopAtBounds

                // The tree model exposes four logical columns (Name/Value/Type/
                // NodeId). The address space only needs the name column, so give
                // column 0 the full width and hide the rest by returning 0.
                columnWidthProvider: function (column) {
                    return column === 0 ? width : 0
                }
                onWidthChanged: Qt.callLater(forceLayout)

                // Slim, fully rounded handle instead of the wide, square-ish
                // Material default. The narrow hit area keeps the pill visually
                // half the default width while the track stays interactive.
                ScrollBar.vertical: ScrollBar {
                    id: vScrollBar

                    implicitWidth: 8

                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: width / 2
                        color: vScrollBar.pressed
                               ? Material.accent
                               : Qt.rgba(Material.foreground.r,
                                         Material.foreground.g,
                                         Material.foreground.b, 0.4)
                        opacity: vScrollBar.active ? 1.0 : 0.0

                        Behavior on opacity {
                            NumberAnimation { duration: 150 }
                        }
                    }
                }

                delegate: TreeViewDelegate {
                    id: treeDelegate

                    implicitHeight: root.rowHeight

                    // Plain panel background instead of the Material default, which
                    // paints alternating light/dark rows. The currently selected node
                    // (shared across panels) gets a lightened highlight.
                    background: Rectangle {
                        color: nodeId === cppManagerOpcUa.selectedNodeId
                               ? Qt.lighter(Material.background, 1.5)
                               : Material.background
                    }

                    // Selecting a node loads its attributes into the Attributes panel.
                    onClicked: cppManagerOpcUa.requestAttributes(treeView.index(row, 0))

                    contentItem: RowLayout {
                        spacing: 8

                        // Node/type icon: a colored SVG picked from the icon key.
                        // The fill color is baked into each SVG (VS Code Material style).
                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            source: root.iconSource(iconName)
                            sourceSize.width: 18
                            sourceSize.height: 18
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: displayName
                            color: Material.foreground
                            font.bold: nodeId === cppManagerOpcUa.selectedNodeId
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        // Checking a variable node adds it to the Data Access View and
                        // the database; unchecking removes it again.
                        CheckBox {
                            Layout.alignment: Qt.AlignVCenter
                            visible: canMonitor
                            checked: monitoringEnabled
                            onToggled: cppManagerOpcUa.setNodeMonitored(treeView.index(row, 0),
                                                                        checked)
                        }
                    }
                }
            }
        }
    }
}
