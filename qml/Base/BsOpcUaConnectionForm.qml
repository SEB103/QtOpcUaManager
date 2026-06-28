import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsOpcUaConnectionForm
    \inqmlmodule Base
    \brief Provides OPC UA discovery, authentication, endpoint, and connect controls.

    The form sends commands to the \c cppManagerOpcUa context object and mirrors
    its busy, connected, endpoint, and error state in the UI.
*/
Rectangle {
    id: root

    implicitWidth: 900
    implicitHeight: contentColumn.implicitHeight + 20

    /*! Local validation error shown before sending invalid authentication input. */
    property string validationError: ""

    /*!
        Applies the currently selected authentication mode to \c cppManagerOpcUa.
        Returns \c true when the selected input is valid and the request was
        forwarded; otherwise stores a validation message and returns \c false.
    */
    function applyAuthentication() {
        root.validationError = "";
        switch (authenticationComboBox.currentIndex) {
        case 0:
            cppManagerOpcUa.setAnonymousAuthentication();
            return true;
        case 1:
            if (usernameField.text.trim().length === 0) {
                root.validationError = qsTr("Enter a username before requesting endpoints or connecting.");
                return false;
            }
            cppManagerOpcUa.setUsernameAuthentication(usernameField.text, passwordField.text);
            return true;
        case 2:
            cppManagerOpcUa.setCertificatePrivateKeyPassword(privateKeyPasswordField.text);
            cppManagerOpcUa.setCertificateAuthentication();
            return true;
        default:
            return false;
        }
    }

    /*! Returns the status text that matches the current manager operation state. */
    function operationText() {
        switch (cppManagerOpcUa.operationState) {
        case 1:
            return qsTr("Discovering servers…");
        case 2:
            return qsTr("Requesting endpoints…");
        case 3:
            return qsTr("Connecting…");
        case 4:
            return qsTr("Disconnecting…");
        default:
            return cppManagerOpcUa.connected ? qsTr("Connected") : qsTr("Disconnected");
        }
    }

    color: Material.background
    radius: 6
    border.color: Material.primary
    border.width: 2

    ColumnLayout {
        id: contentColumn

        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            BusyIndicator {
                running: cppManagerOpcUa.busy
                visible: running
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }

            Label {
                text: root.operationText()
                font.bold: cppManagerOpcUa.connected
                Layout.fillWidth: true
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 10
            rowSpacing: 8

            Label {
                text: qsTr("OPC UA backend:")
                Layout.preferredWidth: 180
            }

            ComboBox {
                id: backendComboBox

                Layout.fillWidth: true
                Layout.preferredHeight: 44
                model: cppManagerOpcUa.opcUaBackend
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
                currentIndex: Math.max(0, cppManagerOpcUa.opcUaBackend.indexOf(cppManagerOpcUa.backend))
                onActivated: cppManagerOpcUa.backend = currentText
            }

            Item {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
            }

            Label {
                text: qsTr("Discovery URL:")
                Layout.preferredWidth: 180
            }

            TextField {
                id: hostField

                Layout.fillWidth: true
                Layout.preferredHeight: 44
                placeholderText: "opc.tcp://127.0.0.1:4840"
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
            }

            Button {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
                text: qsTr("Find Servers")
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
                onClicked: cppManagerOpcUa.discoverServers(hostField.text)
            }

            Label {
                text: qsTr("OPC UA server:")
                Layout.preferredWidth: 180
            }

            ComboBox {
                id: serverComboBox

                Layout.fillWidth: true
                Layout.preferredHeight: 44
                model: cppManagerOpcUa.servers
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
            }

            Button {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
                text: qsTr("Get Endpoints")
                enabled: !cppManagerOpcUa.busy
                         && !cppManagerOpcUa.connected
                         && serverComboBox.currentIndex >= 0
                onClicked: {
                    if (root.applyAuthentication())
                        cppManagerOpcUa.requestEndpointsForServer(serverComboBox.currentIndex);
                }
            }

            Label {
                text: qsTr("Authentication:")
                Layout.preferredWidth: 180
            }

            ComboBox {
                id: authenticationComboBox

                Layout.fillWidth: true
                Layout.preferredHeight: 44
                model: [qsTr("Anonymous"), qsTr("Username / Password"), qsTr("Certificate")]
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
                currentIndex: Math.max(0, Math.min(2, cppManagerOpcUa.authMode))
                onActivated: root.applyAuthentication()
            }

            Item {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
            }

            Label {
                visible: authenticationComboBox.currentIndex === 1
                text: qsTr("Username:")
                Layout.preferredWidth: 180
            }

            TextField {
                id: usernameField

                visible: authenticationComboBox.currentIndex === 1
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
                placeholderText: qsTr("Username")
                onTextChanged: root.validationError = ""
            }

            Item {
                visible: authenticationComboBox.currentIndex === 1
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
            }

            Label {
                visible: authenticationComboBox.currentIndex === 1
                text: qsTr("Password:")
                Layout.preferredWidth: 180
            }

            TextField {
                id: passwordField

                visible: authenticationComboBox.currentIndex === 1
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
            }

            Item {
                visible: authenticationComboBox.currentIndex === 1
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
            }

            Label {
                visible: authenticationComboBox.currentIndex === 2
                text: qsTr("Private-key password:")
                Layout.preferredWidth: 180
            }

            TextField {
                id: privateKeyPasswordField

                visible: authenticationComboBox.currentIndex === 2
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
                echoMode: TextInput.Password
                placeholderText: qsTr("Leave empty for an unencrypted key")
            }

            Label {
                visible: authenticationComboBox.currentIndex === 2
                text: qsTr("Files: pki/own/certs/client.der and pki/own/private/client.pem")
                wrapMode: Text.Wrap
                Layout.preferredWidth: 160
            }

            Label {
                text: qsTr("OPC UA endpoint:")
                Layout.preferredWidth: 180
            }

            ComboBox {
                id: endpointComboBox

                Layout.fillWidth: true
                Layout.preferredHeight: 44
                model: cppManagerOpcUa.endpoints
                enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
            }

            Button {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 44
                text: cppManagerOpcUa.connected ? qsTr("Disconnect") : qsTr("Connect")
                enabled: !cppManagerOpcUa.busy
                         && (cppManagerOpcUa.connected || endpointComboBox.currentIndex >= 0)
                onClicked: {
                    if (cppManagerOpcUa.connected) {
                        cppManagerOpcUa.disconnectFromServer();
                    } else if (root.applyAuthentication()) {
                        cppManagerOpcUa.connectToEndpoint(endpointComboBox.currentIndex);
                    }
                }
            }
        }

        CheckBox {
            text: qsTr("Rewrite advertised endpoint host and port to the discovery URL")
            checked: cppManagerOpcUa.endpointUrlRewriteEnabled
            enabled: !cppManagerOpcUa.busy && !cppManagerOpcUa.connected
            onToggled: cppManagerOpcUa.endpointUrlRewriteEnabled = checked
        }

        Label {
            Layout.fillWidth: true
            visible: root.validationError.length > 0 || cppManagerOpcUa.lastError.length > 0
            text: root.validationError.length > 0 ? root.validationError : cppManagerOpcUa.lastError
            color: Material.color(Material.Red)
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
