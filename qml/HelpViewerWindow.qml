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
    \c cppAppInfo.helpIndexUrl(), so the help works without a network connection,
    and follows in-page links (including the online Qt reference) in the same view.
    Call \l openAt() to show it at a given start URL.

    Navigation is driven imperatively rather than by a \c{url:} binding: a binding
    would navigate to an empty URL while \c startUrl is still unset (WebView2 then
    shows an access-denied page) and would also fight the user's own navigation.
*/
ApplicationWindow {
    id: helpWindow

    /*! The documentation home URL; the target of the Home button. */
    property url startUrl

    /*! Follows the application theme so the chrome matches the main window. */
    property bool darkTheme: false

    /*! Set once a page has loaded successfully; gates Reload so it is never
        called before the web view is in a valid state (which asserts on the
        Windows WebView2 backend). */
    property bool loadedOnce: false

    width: 1040
    height: 760
    minimumWidth: 520
    minimumHeight: 380
    title: qsTr("OpcUaManager Documentation")
    visible: false

    Material.theme: darkTheme ? Material.Dark : Material.Light

    // Follow a live application-theme change: update the server's stylesheet and
    // reload the current page so the docs switch between light and dark too.
    onDarkThemeChanged: {
        cppAppInfo.setHelpDarkTheme(darkTheme)
        if (loadedOnce && !webView.loading)
            webView.reload()
    }

    // Navigate to the start URL passed at creation. Doing it here (once, with a
    // real URL) avoids any empty-URL navigation during WebView2 initialization.
    Component.onCompleted: if (startUrl != "") webView.url = startUrl

    /*!
        Shows the window and navigates to \a url. Ignores an empty URL so the web
        view is never sent to a blank/denied page.
    */
    function openAt(url) {
        if (!url || String(url) === "")
            return
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
                enabled: helpWindow.loadedOnce && !webView.loading
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

        onLoadingChanged: function(request) {
            if (request.status === WebView.LoadSucceededStatus)
                helpWindow.loadedOnce = true
        }
    }
}
