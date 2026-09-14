import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtWebView

/*!
    \qmltype HelpViewerWindow
    \inqmlmodule OpcUaManager
    \brief A standalone window that renders the bundled offline HTML documentation.

    The window hosts a \c WebView (QtWebView) with a small navigation toolbar
    (back, forward, home, reload). It loads a local \c file:// URL resolved by
    \c cppAppInfo.helpIndexUrl(), so the help works without a network connection.
    Call \l openAt() to show it at a given start URL.
*/
ApplicationWindow {
    id: helpWindow

    /*! The initial documentation URL; also the target of the Home button. */
    property url startUrl

    /*! Follows the application theme so the chrome matches the main window. */
    property bool darkTheme: false

    width: 1040
    height: 760
    minimumWidth: 520
    minimumHeight: 380
    title: qsTr("OpcUaManager Documentation")
    visible: false

    Material.theme: darkTheme ? Material.Dark : Material.Light

    /*!
        Shows the window at \a url (loading it when it differs from the current
        page) and brings it to the front.
    */
    function openAt(url) {
        startUrl = url
        if (webView.url !== url)
            webView.url = url
        show()
        raise()
        requestActivate()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 4

            ToolButton {
                text: "←"
                ToolTip.text: qsTr("Back")
                ToolTip.visible: hovered
                enabled: webView.canGoBack
                onClicked: webView.goBack()
            }
            ToolButton {
                text: "→"
                ToolTip.text: qsTr("Forward")
                ToolTip.visible: hovered
                enabled: webView.canGoForward
                onClicked: webView.goForward()
            }
            ToolButton {
                text: qsTr("Home")
                enabled: helpWindow.startUrl != ""
                onClicked: webView.url = helpWindow.startUrl
            }
            ToolButton {
                text: qsTr("Reload")
                onClicked: webView.reload()
            }

            Item { Layout.fillWidth: true }

            BusyIndicator {
                running: webView.loading
                visible: webView.loading
                implicitWidth: 22
                implicitHeight: 22
            }
        }
    }

    WebView {
        id: webView

        anchors.fill: parent
        url: helpWindow.startUrl
    }
}
