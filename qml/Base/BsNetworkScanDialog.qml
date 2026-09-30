pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype BsNetworkScanDialog
    \inqmlmodule Base
    \brief Scans IPv4 ranges for OPC UA servers and hands a chosen server to the caller.

    Uses the \c cppNetworkScanner context object. Scan states are 0 idle,
    1 scanning, 2 finished, 3 cancelled; row statuses are 0 port open,
    1 OPC UA server, 2 no OPC UA response. Closing the dialog cancels a running scan.
*/
Dialog {
    id: root

    /*! Validation error of the range field; empty when valid. */
    property string rangeError: ""
    /*! Validation error of the ports field; empty when valid. */
    property string portsError: ""
    /*! Whether a scan is running. */
    readonly property bool scanning: cppNetworkScanner.state === 1
    /*!
        The selected result delegate, or \c null. Held as \c var because the
        delegate roles are not visible through the \c Item type of \c currentItem.
    */
    readonly property var currentRow: resultList.currentItem
    /*! Whether using a server is blocked because the client is connected. */
    readonly property bool useBlocked: cppManagerOpcUa.connected

    /*! Emitted with the discovery \a url of the server the user chose. */
    signal serverChosen(string url)

    /*! Revalidates both input fields. */
    function validate() {
        root.rangeError = cppNetworkScanner.validateRange(rangeBox.editText);
        root.portsError = cppNetworkScanner.validatePorts(portsField.text);
    }

    /*! Starts a scan with the current input, or stops the running scan. */
    function startScan() {
        if (root.scanning) {
            cppNetworkScanner.cancel();
            return;
        }
        root.validate();
        if (root.rangeError.length === 0 && root.portsError.length === 0) {
            resultList.currentIndex = -1;
            cppNetworkScanner.start(rangeBox.editText, portsField.text);
        }
    }

    /*! Emits serverChosen() for the selected OPC UA row and closes the dialog. */
    function useCurrent() {
        const item = root.currentRow;
        if (!item || item.status !== 1 || root.useBlocked)
            return;
        root.serverChosen(item.url);
        root.close();
    }

    /*! Returns the server column text for a row with \a status and \a name. */
    function serverText(status, name) {
        switch (status) {
        case 1:
            return name.length > 0 ? name : qsTr("OPC UA server");
        case 2:
            return qsTr("Port open, no OPC UA response");
        default:
            return qsTr("Port open, querying…");
        }
    }

    /*! Returns the progress line for the current scan state. */
    function progressText() {
        switch (cppNetworkScanner.state) {
        case 1:
            return qsTr("Probed %1/%2 · found %3")
                .arg(cppNetworkScanner.probedTargets)
                .arg(cppNetworkScanner.totalTargets)
                .arg(cppNetworkScanner.opcUaServers);
        case 2:
            return qsTr("Done: %1 checks in %2 s, OPC UA servers: %3")
                .arg(cppNetworkScanner.totalTargets)
                .arg((cppNetworkScanner.elapsedMs / 1000).toFixed(1))
                .arg(cppNetworkScanner.opcUaServers);
        case 3:
            return qsTr("Scan stopped.");
        default:
            return "";
        }
    }

    title: qsTr("Scan network")
    modal: true
    width: 900
    height: 600
    closePolicy: Popup.CloseOnEscape

    onAboutToShow: {
        rangeBox.model = cppNetworkScanner.localSubnets();
        rangeBox.editText = cppNetworkScanner.lastRange.length > 0
                ? cppNetworkScanner.lastRange
                : (rangeBox.count > 0 ? rangeBox.textAt(0) : "");
        portsField.text = cppNetworkScanner.lastPorts;
        root.validate();
    }
    onClosed: cppNetworkScanner.cancel()

    contentItem: ColumnLayout {
        spacing: 8

        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 10

            Label {
                text: qsTr("Range:")
            }

            ComboBox {
                id: rangeBox

                Layout.fillWidth: true
                editable: true
                enabled: !root.scanning
                onEditTextChanged: root.validate()
            }

            Label {
                text: qsTr("Ports:")
            }

            TextField {
                id: portsField

                Layout.preferredWidth: 160
                enabled: !root.scanning
                onTextChanged: root.validate()
            }

            Button {
                Layout.preferredWidth: 140
                text: root.scanning ? qsTr("Stop") : qsTr("Scan")
                enabled: root.scanning
                         || (root.rangeError.length === 0 && root.portsError.length === 0)
                onClicked: root.startScan()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: rangeBox.count === 0
            text: qsTr("No network interface found; enter a range manually, e.g. 10.10.1.0/24.")
            wrapMode: Text.Wrap
        }

        Label {
            Layout.fillWidth: true
            visible: root.rangeError.length > 0 || root.portsError.length > 0
            text: root.rangeError.length > 0 ? root.rangeError : root.portsError
            color: Material.color(Material.Red)
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }

        ProgressBar {
            Layout.fillWidth: true
            visible: cppNetworkScanner.state !== 0
            from: 0
            to: Math.max(1, cppNetworkScanner.totalTargets)
            value: cppNetworkScanner.probedTargets
            indeterminate: root.scanning
                           && cppNetworkScanner.probedTargets >= cppNetworkScanner.totalTargets
        }

        Label {
            Layout.fillWidth: true
            text: root.progressText()
        }

        ListView {
            id: resultList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: cppNetworkScanner.model
            currentIndex: -1
            ScrollBar.vertical: ScrollBar {}

            header: RowLayout {
                width: resultList.width
                spacing: 8

                Label {
                    text: qsTr("Address")
                    font.bold: true
                    Layout.preferredWidth: 130
                }

                Label {
                    text: qsTr("Port")
                    font.bold: true
                    Layout.preferredWidth: 60
                }

                Label {
                    text: qsTr("Server")
                    font.bold: true
                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("Security")
                    font.bold: true
                    Layout.preferredWidth: 160
                }

                Label {
                    text: qsTr("Login")
                    font.bold: true
                    Layout.preferredWidth: 160
                }

                Label {
                    text: qsTr("ms")
                    font.bold: true
                    Layout.preferredWidth: 50
                }
            }

            delegate: ItemDelegate {
                id: rowDelegate

                required property int index
                required property string address
                required property int port
                required property string url
                required property string applicationName
                required property string securitySummary
                required property string authSummary
                required property int responseMs
                required property int status
                required property string errorText

                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                opacity: rowDelegate.status === 2 ? 0.55 : 1.0
                ToolTip.visible: hovered && rowDelegate.errorText.length > 0
                ToolTip.text: rowDelegate.errorText
                onClicked: resultList.currentIndex = rowDelegate.index
                onDoubleClicked: {
                    resultList.currentIndex = rowDelegate.index;
                    root.useCurrent();
                }

                contentItem: RowLayout {
                    spacing: 8

                    Label {
                        text: rowDelegate.address
                        Layout.preferredWidth: 130
                    }

                    Label {
                        text: rowDelegate.port
                        Layout.preferredWidth: 60
                    }

                    Label {
                        text: root.serverText(rowDelegate.status, rowDelegate.applicationName)
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: rowDelegate.securitySummary
                        elide: Text.ElideRight
                        Layout.preferredWidth: 160
                    }

                    Label {
                        text: rowDelegate.authSummary
                        elide: Text.ElideRight
                        Layout.preferredWidth: 160
                    }

                    Label {
                        text: rowDelegate.responseMs >= 0 ? rowDelegate.responseMs : ""
                        Layout.preferredWidth: 50
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                visible: root.useBlocked
                text: qsTr("Disconnect first to use a scanned server.")
                wrapMode: Text.Wrap
            }

            Item {
                Layout.fillWidth: !root.useBlocked
            }

            Button {
                text: qsTr("Use")
                enabled: !root.useBlocked && root.currentRow !== null
                         && root.currentRow.status === 1
                onClicked: root.useCurrent()
            }

            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
