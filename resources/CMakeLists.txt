# Application configuration and image resources.
#
# SVG icons are based on Google Material Symbols Outlined and are stored
# locally so that the application has no runtime dependency on online assets.
qt_add_resources(appOpcUaManager "application_resources"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"

    FILES
        qtquickcontrols2.conf

        # Application branding: multi-size window/taskbar icon and the wordmark
        # logo shown on the launcher start page. Source PNGs and generated
        # variants live under images/app/ (the .exe icon comes from
        # images/app/app.rc, which is not a runtime resource).
        images/app/OpcUaManager.ico
        images/app/OpcUaManagerLogo.png

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
        images/svg/save.svg
        images/svg/search.svg
        images/svg/settings.svg
        images/svg/stop.svg

        # Address-space node/type icons for the OPC UA tree.
        images/svg/folder.svg
        images/svg/deployed_code.svg
        images/svg/function.svg
        images/svg/category.svg
        images/svg/data_object.svg
        images/svg/data_array.svg
        images/svg/share.svg
        images/svg/visibility.svg
        images/svg/variable.svg
        images/svg/toggle_on.svg
        images/svg/tag.svg
        images/svg/tag_indigo.svg
        images/svg/tag_cyan.svg
        images/svg/text_fields.svg
        images/svg/schedule.svg
        images/svg/category_cyan.svg
)

# License and notice documents shown by the Help > About dialog. The canonical
# copies live at the project root (single source of truth for both source
# distribution and REUSE); they are embedded here so the About dialog has no
# runtime dependency on any deployed folder. BASE is the project root, so the
# files keep their LICENSES/ path under the /licenses resource prefix.
qt_add_resources(appOpcUaManager "license_resources"
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}"

    FILES
        "${CMAKE_SOURCE_DIR}/NOTICE"
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "${CMAKE_SOURCE_DIR}/LICENSES/GPL-3.0-or-later.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-3.0-only.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/GPL-2.0-only.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/MPL-2.0.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/Apache-2.0.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/BSD-3-Clause.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/MIT.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/CC0-1.0.txt"
        "${CMAKE_SOURCE_DIR}/LICENSES/CC-BY-SA-4.0.txt"
)
