import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    width: 1600
    height: 900

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        anchors.margins: 10

        BsOpcUaConnectionForm {
            id: connectionForm
            Layout.preferredHeight: implicitHeight
            Layout.fillWidth: true
        }

        Rectangle {
            id: rectView
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredHeight: connectionForm.height
            color: Material.background
            radius: 6
            border.color: Material.primary
            border.width: 2

            TreeView {
                id: treeView
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                model: cppManagerOpcUa.treeModel

                delegate: TreeViewDelegate {

                    contentItem: Row {
                        spacing: 8

                        Label {
                            text: displayName
                            width: 260
                            elide: Text.ElideRight
                        }

                        Label {
                            text: value
                            width: 220
                            elide: Text.ElideRight
                        }

                        Label {
                            text: nodeClassName
                            width: 120
                            elide: Text.ElideRight
                        }

                        Label {
                            text: nodeId
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // Rectangle {
        //     id: rectLog
        //     Layout.preferredHeight: 180
        //     Layout.fillWidth: true
        //     color: Material.background
        //     radius: 6
        //     border.color: Material.primary
        //     border.width: 2

        //     ScrollView {
        //         anchors.fill: parent
        //         anchors.margins: 10

        //         ListView {
        //             id: logView
        //             clip: true
        //             model: cppManagerOpcUa.logModel
        //             delegate: Text {
        //                 width: ListView.view.width
        //                 wrapMode: Text.Wrap
        //                 text: Qt.formatDateTime(ts, "hh:mm:ss") + "  " + model.text
        //                 color: model.color
        //             }
        //         }
        //     }
        // }
    }
}

/*

Button {
    text: "Anonymous"
    onClicked: cppManagerOpcUa.setAnonymousAuthentication()
}

Button {
    text: "Username"
    onClicked: cppManagerOpcUa.setUsernameAuthentication(userField.text, passwordField.text)
}

Button {
    text: "Certificate"
    onClicked: cppManagerOpcUa.setCertificateAuthentication()
}

*/
