import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root

    implicitWidth: 900
    implicitHeight: gridLayout.implicitHeight + gridLayout.anchors.margins * 2

    color: Material.background
    radius: 6
    border.color: Material.primary
    border.width: 2

    GridLayout {
        id: gridLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        columns: 3
        rows: 4

        Label { text: qsTr("Select OPC UA Backend:"); Layout.preferredWidth: 170 }

        ComboBox {
            id: comboBoxBackend
            Layout.preferredHeight: 50
            Layout.fillWidth: true
            model: cppManagerOpcUa.opcUaBackend
            onActivated: cppManagerOpcUa.backend = currentText
        }

        Item {
            id: placeHolder
            width: 140
            height: 50
            Layout.preferredHeight: 50
            Layout.preferredWidth: 150
        }

        Label { text: qsTr("Select host to discover:"); Layout.preferredWidth: 170 }

        TextField {
            id: textFieldHost
            Layout.preferredHeight: 50
            Layout.fillWidth: true
            placeholderText: "opc.tcp://127.0.0.1:4840"
        }

        Button {
            id: buttonFindServers
            Layout.preferredWidth: 150
            Layout.preferredHeight: 50
            text: qsTr("Find Servers")
            onClicked: cppManagerOpcUa.discoverServers(textFieldHost.text)
        }

        Label {
            id: labelServer
            text: qsTr("Select OPC UA Server")
            Layout.preferredWidth: 170
        }

        ComboBox {
            id: comboBoxServers
            Layout.preferredHeight: 50
            Layout.fillWidth: true
            model: cppManagerOpcUa.servers
        }

        Button {
            id: buttonEndpoints
            Layout.preferredWidth: 150
            Layout.preferredHeight: 50
            text: qsTr("Get Endpoints")
            enabled: comboBoxServers.currentIndex >= 0
            onClicked: cppManagerOpcUa.requestEndpointsForServer(comboBoxServers.currentIndex)
        }

        Label {
            id: labelEndpoint
            text: qsTr("Select OPC UA Endpoint:")
            Layout.preferredWidth: 170
        }

        ComboBox {
            id: comboBoxEndpoint
            Layout.preferredHeight: 50
            Layout.fillWidth: true
            model: cppManagerOpcUa.endpoints
        }

        Button {
            id: buttonConnect
            Layout.preferredWidth: 150
            Layout.preferredHeight: 50
            text: cppManagerOpcUa.connected ? qsTr("Disconnect") : qsTr("Connect")
            enabled: cppManagerOpcUa.connected || comboBoxEndpoint.currentIndex >= 0
            onClicked: {
                if (cppManagerOpcUa.connected)
                    cppManagerOpcUa.disconnectFromServer()
                else
                    cppManagerOpcUa.connectToEndpoint(comboBoxEndpoint.currentIndex)
            }
        }
    }
}
