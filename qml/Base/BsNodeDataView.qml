import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsNodeDataView
    \inqmlmodule Base
    \brief Center Data Access View table for monitored OPC UA nodes.

    The panel lists every node the user added from the address-space tree. Each
    row shows the node identity together with the live value, timestamps, and
    status reported by the server. A row can be removed, and the value cell can be
    double-clicked to write a new value.
*/
Rectangle {
    id: root

    /*! Column layout shared by the header and the data rows. */
    readonly property var columnModel: [
        { title: qsTr("#"),                role: "rowNumber",       width: 40  },
        { title: qsTr("Server"),           role: "server",          width: 150 },
        { title: qsTr("Node Id"),          role: "nodeId",          width: 220 },
        { title: qsTr("Node Path"),        role: "nodePath",        width: 170 },
        { title: qsTr("Display Name"),     role: "displayName",     width: 150 },
        { title: qsTr("Value"),            role: "value",           width: 130 },
        { title: qsTr("Data Type"),        role: "dataType",        width: 100 },
        { title: qsTr("Source Timestamp"), role: "sourceTimestamp", width: 160 },
        { title: qsTr("Server Timestamp"), role: "serverTimestamp", width: 160 },
        { title: qsTr("Status Code"),      role: "statusCode",      width: 130 }
    ]

    /*! Width reserved for the per-row delete action. */
    readonly property int actionWidth: 48

    /*! Total pixel width of all columns plus the action column. */
    readonly property int totalWidth: {
        let sum = actionWidth
        for (let i = 0; i < columnModel.length; ++i)
            sum += columnModel[i].width
        return sum
    }

    /*! Height of the header and each data row. */
    readonly property int rowHeight: 30

    /*! Emitted with the node id and browse path when a row is selected, so the tree can reveal it. */
    signal nodeSelected(string nodeId, string nodePath)

    color: Material.background
    border.color: Material.dividerColor
    border.width: 1
    clip: true

    /*!
        Opens the value editor for the Data Access View row \a row, pre-filling
        \a currentValue.
    */
    function editValue(row, currentValue) {
        valueEditor.editRow = row
        valueEditor.editField.text = currentValue
        valueEditor.open()
    }

    /*!
        Selects the Data Access View \a row (identified by \a nodeId, located at
        \a nodePath): loads its attributes and value into the panels, highlights
        it, and asks the address space to reveal the same node.
    */
    function selectRow(row, nodeId, nodePath) {
        cppManagerOpcUa.selectDataRow(row)
        root.nodeSelected(nodeId, nodePath)
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
                text: qsTr("DATA VIEW")
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
                visible: rowsView.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Check nodes in the address space to add them to the Data Access View.")
                color: Material.foreground
                opacity: 0.6
            }

            Flickable {
                id: horizontalScroll

                anchors.fill: parent
                anchors.margins: 4
                contentWidth: root.totalWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                clip: true

                ScrollBar.horizontal: ScrollBar {}

                Column {
                    width: root.totalWidth
                    height: horizontalScroll.height

                    // Column header row.
                    Rectangle {
                        width: root.totalWidth
                        height: root.rowHeight
                        color: Qt.darker(Material.background, 1.1)

                        Row {
                            anchors.fill: parent

                            Repeater {
                                model: root.columnModel

                                Item {
                                    required property var modelData
                                    width: modelData.width
                                    height: root.rowHeight

                                    Label {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        verticalAlignment: Text.AlignVCenter
                                        text: modelData.title
                                        font.bold: true
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        color: Material.accent
                                    }
                                }
                            }

                            Item {
                                width: root.actionWidth
                                height: root.rowHeight
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

                    ListView {
                        id: rowsView

                        width: root.totalWidth
                        height: horizontalScroll.height - root.rowHeight
                        clip: true
                        model: cppManagerOpcUa.dataModel
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            id: rowDelegate

                            required property int index
                            required property var model

                            /*! Whether this row is the shared selected node. */
                            readonly property bool selected:
                                model.nodeId === cppManagerOpcUa.selectedNodeId

                            width: root.totalWidth
                            height: root.rowHeight
                            color: rowDelegate.selected
                                   ? Qt.lighter(Material.background, 1.5)
                                   : (index % 2 === 0
                                      ? "transparent"
                                      : Qt.darker(Material.background, 1.05))

                            // Selects the row on a click anywhere outside the cells
                            // that carry their own pointer handling (the value cell).
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: root.selectRow(rowDelegate.index,
                                                          rowDelegate.model.nodeId,
                                                          rowDelegate.model.nodePath)
                            }

                            Row {
                                anchors.fill: parent

                                Repeater {
                                    model: root.columnModel

                                    Item {
                                        required property var modelData
                                        width: modelData.width
                                        height: root.rowHeight

                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            verticalAlignment: Text.AlignVCenter
                                            text: rowDelegate.model[modelData.role] !== undefined
                                                  ? rowDelegate.model[modelData.role]
                                                  : ""
                                            font.bold: rowDelegate.selected
                                            elide: Text.ElideRight
                                            color: Material.foreground
                                        }

                                        // The value cell selects on a single click and
                                        // opens the editor on a double click; other cells
                                        // are handled by the row-level MouseArea.
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: modelData.role === "value"
                                            acceptedButtons: Qt.LeftButton
                                            onClicked: root.selectRow(rowDelegate.index,
                                                                      rowDelegate.model.nodeId,
                                                                      rowDelegate.model.nodePath)
                                            onDoubleClicked: root.editValue(rowDelegate.index,
                                                                            rowDelegate.model.value)
                                        }
                                    }
                                }

                                Item {
                                    width: root.actionWidth
                                    height: root.rowHeight

                                    ToolButton {
                                        anchors.centerIn: parent
                                        text: "✕"
                                        font.pixelSize: 14
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Remove from Data Access View")
                                        onClicked: cppManagerOpcUa.removeNode(rowDelegate.index)
                                    }
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Material.dividerColor
                                opacity: 0.4
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: valueEditor

        /*! Data Access View row currently being edited. */
        property int editRow: -1

        /*! Convenience alias to the value input field. */
        property alias editField: valueField

        anchors.centerIn: parent
        width: 360
        modal: true
        title: qsTr("Write value")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: {
            if (editRow >= 0)
                cppManagerOpcUa.writeValue(editRow, valueField.text)
            editRow = -1
        }
        onRejected: editRow = -1

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: qsTr("New value")
                color: Material.foreground
            }

            TextField {
                id: valueField
                Layout.fillWidth: true
                selectByMouse: true
            }
        }
    }
}
