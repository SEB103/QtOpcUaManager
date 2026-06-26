# Application configuration and image resources.
#
# SVG icons are based on Google Material Symbols Outlined and are stored
# locally so that the application has no runtime dependency on online assets.
qt_add_resources(appOpcUaManager "application_resources"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"

    FILES
        qtquickcontrols2.conf

        images/svg/dark_mode.svg
        images/svg/help.svg
        images/svg/info.svg
        images/svg/light_mode.svg
        images/svg/link.svg
        images/svg/link_off.svg
        images/svg/login.svg
        images/svg/logout.svg
        images/svg/play_arrow.svg
        images/svg/power_settings_new.svg
        images/svg/search.svg
        images/svg/settings.svg
        images/svg/stop.svg
)
