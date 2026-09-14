# =============================================================================
# InstallWindowsRuntime.cmake
#
# Purpose:
#   Register install-time rules that copy the Windows runtime files needed by
#   the canonical deployment which the Qt deploy step may not place itself:
#     - the OpenSSL 3 runtime DLLs (libssl-3*/libcrypto-3*) next to the
#       executable; open62541 loads these directly for OPC UA security; and
#     - defensively, the Qt OPC UA open62541 backend plugin under
#       <bindir>/plugins/opcua/ (a no-op copy_if_different when windeployqt
#       already deployed it, which Qt 6.11 does).
#
#   Unlike ConfigureWindowsRuntimeCopy.cmake (a POST_BUILD helper for the local
#   build tree), this operates during `cmake --install` so the files land in
#   the canonical deployment used for the installer and portable package. The
#   install layout follows windeployqt's qt.conf (plugins under plugins/), which
#   differs from the flat build-tree layout, so the copies are written here
#   rather than reusing CopyWindowsRuntime.cmake.
#
# Usage:
#   include(cmake/InstallWindowsRuntime.cmake)
#   opcuamanager_install_windows_runtime()
# =============================================================================

function(opcuamanager_install_windows_runtime)
    if(NOT WIN32)
        return()
    endif()

    # Resolve the plugin, QML and binary directories of the active Qt kit so the
    # runtime files come from this exact Qt installation.
    set(_qt_plugin_dir "")
    set(_qt_qml_dir "")
    set(_qt_bin_dir "")
    if(TARGET Qt6::qmake)
        get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        if(_qt_qmake_executable)
            execute_process(
                COMMAND "${_qt_qmake_executable}" -query QT_INSTALL_PLUGINS
                OUTPUT_VARIABLE _qt_plugin_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _qt_plugin_query_result
            )
            if(NOT _qt_plugin_query_result EQUAL 0)
                set(_qt_plugin_dir "")
            endif()
            execute_process(
                COMMAND "${_qt_qmake_executable}" -query QT_INSTALL_QML
                OUTPUT_VARIABLE _qt_qml_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _qt_qml_query_result
            )
            if(NOT _qt_qml_query_result EQUAL 0)
                set(_qt_qml_dir "")
            endif()
            execute_process(
                COMMAND "${_qt_qmake_executable}" -query QT_INSTALL_BINS
                OUTPUT_VARIABLE _qt_bin_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _qt_bins_query_result
            )
            if(NOT _qt_bins_query_result EQUAL 0)
                set(_qt_bin_dir "")
            endif()
        endif()
    endif()

    # Resolve the OpenSSL runtime directory. Honor an explicit cache override,
    # otherwise probe the usual Qt/Windows locations.
    set(_openssl_bin_dir "${OPCUAMANAGER_OPENSSL_BIN_DIR}")
    if(NOT _openssl_bin_dir)
        set(_openssl_hints)
        if(DEFINED ENV{OPENSSL_ROOT_DIR})
            list(APPEND _openssl_hints "$ENV{OPENSSL_ROOT_DIR}/bin")
        endif()
        list(APPEND _openssl_hints
            "C:/Qt/Tools/OpenSSLv3/Win_x64/bin"
            "C:/Program Files/OpenSSL-Win64/bin"
        )
        find_path(_install_openssl_bin_dir
            NAMES libssl-3-x64.dll libssl-3.dll
            HINTS ${_openssl_hints}
            NO_DEFAULT_PATH
        )
        if(_install_openssl_bin_dir)
            set(_openssl_bin_dir "${_install_openssl_bin_dir}")
        endif()
    endif()

    set(_bindir "${CMAKE_INSTALL_BINDIR}")

    # Defer CMAKE_INSTALL_PREFIX/CONFIG to install time (escaped); the resolved
    # source directories are expanded now at configure time.
    install(CODE "
        set(_dest \"\${CMAKE_INSTALL_PREFIX}/${_bindir}\")

        # 1. OpenSSL 3 runtime DLLs, next to the executable.
        set(_openssl_src \"${_openssl_bin_dir}\")
        set(_openssl_names libssl-3-x64.dll libcrypto-3-x64.dll libssl-3.dll libcrypto-3.dll)
        if(_openssl_src)
            foreach(_dll IN LISTS _openssl_names)
                if(EXISTS \"\${_openssl_src}/\${_dll}\")
                    file(COPY_FILE \"\${_openssl_src}/\${_dll}\" \"\${_dest}/\${_dll}\" ONLY_IF_DIFFERENT)
                    message(STATUS \"Installed OpenSSL runtime: \${_dll}\")
                endif()
            endforeach()
        else()
            message(WARNING \"OpenSSL runtime directory not found; secure OPC UA may not work.\")
        endif()

        # 2. Qt OPC UA open62541 backend plugin (defensive; usually already
        #    deployed by windeployqt under plugins/opcua/).
        set(_backend_name open62541_backend.dll)
        if(\"\${CMAKE_INSTALL_CONFIG_NAME}\" STREQUAL \"Debug\")
            set(_backend_name open62541_backendd.dll)
        endif()
        set(_backend_src \"${_qt_plugin_dir}/opcua/\${_backend_name}\")
        if(EXISTS \"\${_backend_src}\")
            file(MAKE_DIRECTORY \"\${_dest}/plugins/opcua\")
            file(COPY_FILE \"\${_backend_src}\" \"\${_dest}/plugins/opcua/\${_backend_name}\" ONLY_IF_DIFFERENT)
            message(STATUS \"Ensured OPC UA backend plugin: plugins/opcua/\${_backend_name}\")
        endif()

        # 3. Qt WebView runtime for the offline documentation viewer. windeployqt
        #    ships Qt6WebView.dll but not the QML module, its Quick frontend, or
        #    the Windows (WebView2) backend plugin, because the application QML is
        #    compiled into resources and its 'import QtWebView' is not scanned.
        set(_debug_suffix \"\")
        if(\"\${CMAKE_INSTALL_CONFIG_NAME}\" STREQUAL \"Debug\")
            set(_debug_suffix \"d\")
        endif()

        # 3a. Quick frontend library next to the executable.
        set(_wv_quick_src \"${_qt_bin_dir}/Qt6WebViewQuick\${_debug_suffix}.dll\")
        if(EXISTS \"\${_wv_quick_src}\")
            file(COPY_FILE \"\${_wv_quick_src}\" \"\${_dest}/Qt6WebViewQuick\${_debug_suffix}.dll\" ONLY_IF_DIFFERENT)
            message(STATUS \"Installed Qt WebView Quick library: Qt6WebViewQuick\${_debug_suffix}.dll\")
        endif()

        # 3b. QML module (qmldir + type info + the QML plugin).
        set(_wv_qml_src \"${_qt_qml_dir}/QtWebView\")
        if(EXISTS \"\${_wv_qml_src}/qmldir\")
            file(MAKE_DIRECTORY \"\${_dest}/qml/QtWebView\")
            file(COPY_FILE \"\${_wv_qml_src}/qmldir\" \"\${_dest}/qml/QtWebView/qmldir\" ONLY_IF_DIFFERENT)
            if(EXISTS \"\${_wv_qml_src}/plugins.qmltypes\")
                file(COPY_FILE \"\${_wv_qml_src}/plugins.qmltypes\" \"\${_dest}/qml/QtWebView/plugins.qmltypes\" ONLY_IF_DIFFERENT)
            endif()
            set(_wv_qml_plugin \"\${_wv_qml_src}/qtwebviewquickplugin\${_debug_suffix}.dll\")
            if(EXISTS \"\${_wv_qml_plugin}\")
                file(COPY_FILE \"\${_wv_qml_plugin}\" \"\${_dest}/qml/QtWebView/qtwebviewquickplugin\${_debug_suffix}.dll\" ONLY_IF_DIFFERENT)
            endif()
            message(STATUS \"Installed Qt WebView QML module: qml/QtWebView\")
        endif()

        # 3c. Windows (WebView2) backend plugin. WebView2Loader.dll is provided by
        #     the system Edge WebView2 runtime and is not shipped by the kit.
        set(_wv_backend \"${_qt_plugin_dir}/webview/qtwebview_webview2\${_debug_suffix}.dll\")
        if(EXISTS \"\${_wv_backend}\")
            file(MAKE_DIRECTORY \"\${_dest}/plugins/webview\")
            file(COPY_FILE \"\${_wv_backend}\" \"\${_dest}/plugins/webview/qtwebview_webview2\${_debug_suffix}.dll\" ONLY_IF_DIFFERENT)
            message(STATUS \"Installed Qt WebView backend plugin: plugins/webview/qtwebview_webview2\${_debug_suffix}.dll\")
        endif()
    ")
endfunction()
