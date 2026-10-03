pragma Singleton

import QtQuick

/*!
    \qmltype BsTheme
    \inqmlmodule Base
    \brief Shared chrome colors for the light and dark application themes.

    The workspace chrome (pane header strips, the menu band, dividers, the
    selected-row tint, and the status bar) takes its colors from this singleton
    instead of deriving them from \c Material.background. Lightening the
    near-white light background has no visible effect, so the derived colors
    made every header and the status bar the same white as the content.

    The palette is a teal-tinted neutral built around the application accent
    (Material Teal). The accent itself appears only in thin lines, light tints,
    and title text. Content colors are not part of this palette: value and
    JSON/XML highlighting, node-type icons, and status colors keep their own
    definitions.

    The window binds \l dark to its Material theme. Components that are created
    without a window (for example in tests) see the light palette.
*/
QtObject {
    id: theme

    /*! Whether the dark palette is active. */
    property bool dark: false

    /*! Background of the pane header strips. */
    readonly property color headerColor: theme.dark ? "#22282A" : "#EFF5F4"

    /*!
        Text color of pane titles and table column titles.

        The light theme uses Teal 700 because Teal 500 does not reach the 4.5:1
        contrast required for 12 px text. The dark theme keeps Teal 200, the
        Material dark accent.
    */
    readonly property color accentTextColor: theme.dark ? "#80CBC4" : "#00796B"

    /*! Background of the Data View column header row. */
    readonly property color tableHeaderColor: theme.dark ? "#1F2426" : "#F1F6F5"

    /*! Background of the selected tree node and the highlighted Data View row. */
    readonly property color selectedRowColor: theme.dark ? Qt.rgba(0.502, 0.796, 0.769, 0.16)
                                                         : Qt.rgba(0.0, 0.588, 0.533, 0.14)

    /*! Color of pane borders and separator lines. */
    readonly property color dividerColor: theme.dark ? "#33393B" : "#DCE4E2"

    /*! Background of the top band that holds the menu bar and the quick actions. */
    readonly property color menuBandColor: theme.dark ? "#202325" : "#F4F8F7"

    /*! Background of a highlighted or open menu bar title. */
    readonly property color menuHighlightColor: theme.dark ? "#24383A" : "#E0F1EE"

    /*! Background of a highlighted item in a dropdown or context menu. */
    readonly property color menuItemHighlightColor: theme.dark ? Qt.rgba(0.502, 0.796, 0.769, 0.14)
                                                               : Qt.rgba(0.0, 0.588, 0.533, 0.10)

    /*! Thin accent line under the open menu bar title and on the connected status bar. */
    readonly property color accentLineColor: theme.dark ? "#4DB6AC" : "#009688"

    /*! Color of a split handle while the pointer hovers over it. */
    readonly property color splitHandleHoverColor: theme.dark ? Qt.rgba(0.502, 0.796, 0.769, 0.6)
                                                              : Qt.rgba(0.0, 0.588, 0.533, 0.55)

    /*!
        Returns the status bar background for the connection state.

        \a connected and \a connecting describe the client session. A connected
        session tints the bar teal and a pending transition tints it amber; an
        offline session uses the neutral menu band color.
    */
    function statusBarColor(connected, connecting) {
        if (connected)
            return theme.dark ? "#253335" : "#E6F3F1"
        if (connecting)
            return theme.dark ? "#33301F" : "#FEF5E2"
        return theme.menuBandColor
    }

    /*!
        Returns the color of the line along the top edge of the status bar.

        \a connected and \a connecting describe the client session. The line is
        teal when connected, amber while connecting, and a plain divider when
        offline. Use \l statusLineWidth() for its thickness.
    */
    function statusLineColor(connected, connecting) {
        if (connected)
            return theme.accentLineColor
        if (connecting)
            return theme.dark ? "#FFB300" : "#FFA000"
        return theme.dividerColor
    }

    /*!
        Returns the thickness in pixels of the status bar top line.

        \a active is true while the session is connected or connecting; the
        state line is then 2 px, otherwise the plain 1 px divider is drawn.
    */
    function statusLineWidth(active) {
        return active ? 2 : 1
    }

    /*!
        Returns the color of the connection state text in the status bar.

        \a connected is true while the session is connected; the text then takes
        the dark accent shade so it reads against the teal tint. \a fallback is
        returned for every other state, normally \c Material.foreground.
    */
    function statusTextColor(connected, fallback) {
        if (connected)
            return theme.dark ? "#80CBC4" : "#00695C"
        return fallback
    }
}
