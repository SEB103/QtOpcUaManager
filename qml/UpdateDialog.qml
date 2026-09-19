// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/*!
    \qmltype UpdateDialog
    \inqmlmodule OpcUaManager
    \brief Shows the result of an application update check.

    The dialog reflects the \c cppUpdate controller: it shows a busy indicator
    while a check runs, reports whether the running version is current, and offers
    a link to the download page when a newer version is available. When the update
    feature is disabled in the product configuration it explains that the check is
    not yet available.
*/
Dialog {
    id: root

    /*! Whether the surrounding window uses the dark Material theme. */
    property bool darkTheme: false

    title: qsTr("Check for updates")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(root.parent ? root.parent.width - 80 : 460, 460)
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    Material.accent: Material.Teal

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: qsTr("Current version: %1").arg(cppUpdate.currentVersion)
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            BusyIndicator {
                running: cppUpdate.checking
                visible: running
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }

            Label {
                Layout.fillWidth: true
                text: cppUpdate.statusMessage
                wrapMode: Text.Wrap
            }
        }

        // Offer the download page only when a newer version was found.
        Button {
            Layout.alignment: Qt.AlignLeft
            visible: cppUpdate.updateAvailable
            text: qsTr("Open download page")
            onClicked: Qt.openUrlExternally(cppUpdate.releaseUrl)
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Check again")
            enabled: cppUpdate.featureEnabled && !cppUpdate.checking
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: cppUpdate.checkNow()
        }

        Button {
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.reject()
        }
    }
}
