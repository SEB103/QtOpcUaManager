# =============================================================================
# ConfigureWindowsRuntimeCopy.cmake
#
# Purpose:
#   Configure a Windows POST_BUILD step that prepares the application output
#   directory for local execution after a normal build.
#
# What it configures:
#   - the Qt OPC UA open62541 backend plugin is copied to <app>/opcua/;
#   - optional OpenSSL 3 runtime DLLs are copied next to the executable.
#
# How it works:
#   This file discovers the required source directories and adds a POST_BUILD
#   command to the specified target. The actual file copying is performed by
#   CopyWindowsRuntime.cmake in CMake script mode (cmake -P).
#
# Usage:
#   include(cmake/ConfigureWindowsRuntimeCopy.cmake)
#   opcuamanager_configure_windows_runtime_copy(appOpcUaManager)
#
# Configuration:
#   OPCUAMANAGER_OPENSSL_BIN_DIR may be set in the CMake cache when OpenSSL
#   cannot be detected automatically.
#
# Scope:
#   Windows only. This helper is intended for the local build output and does
#   not replace the application's normal install/deployment process.
# =============================================================================

function(opcuamanager_configure_windows_runtime_copy target_name)
    if(NOT WIN32)
        return()
    endif()

    # User-overridable path containing the OpenSSL 3 runtime DLLs required by
    # the Qt OPC UA backend when security features are used.
    set(OPCUAMANAGER_OPENSSL_BIN_DIR "" CACHE PATH
        "Directory containing the OpenSSL 3 runtime DLLs used by Qt OPC UA")

    # Query the plugin directory of the active Qt kit. This avoids hard-coded
    # paths and ensures that the backend comes from the same Qt installation.
    set(_qt_plugin_dir "")
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
        endif()
    endif()

    # Try common OpenSSL locations only when the user has not supplied a path.
    if(NOT OPCUAMANAGER_OPENSSL_BIN_DIR)
        set(_openssl_hints)
        if(DEFINED ENV{OPENSSL_ROOT_DIR})
            list(APPEND _openssl_hints "$ENV{OPENSSL_ROOT_DIR}/bin")
        endif()
        list(APPEND _openssl_hints
            "C:/Qt/Tools/OpenSSLv3/Win_x64/bin"
            "C:/Program Files/OpenSSL-Win64/bin"
        )

        find_path(_detected_openssl_bin_dir
            NAMES libssl-3-x64.dll libssl-3.dll
            HINTS ${_openssl_hints}
            NO_DEFAULT_PATH
        )

        # Store an automatically detected path in the cache so that it remains
        # visible and editable in Qt Creator's CMake configuration.
        if(_detected_openssl_bin_dir)
            set(OPCUAMANAGER_OPENSSL_BIN_DIR
                "${_detected_openssl_bin_dir}"
                CACHE PATH
                "Directory containing the OpenSSL 3 runtime DLLs used by Qt OPC UA"
                FORCE
            )
        endif()
    endif()

    # Run the dedicated copy script after the executable has been built.
    # Generator expressions select the actual output directory and build type.
    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DOPCUA_PLUGIN_DIR=${_qt_plugin_dir}/opcua"
            "-DOPENSSL_BIN_DIR=${OPCUAMANAGER_OPENSSL_BIN_DIR}"
            "-DDESTINATION_DIR=$<TARGET_FILE_DIR:${target_name}>"
            "-DBUILD_CONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopyWindowsRuntime.cmake"
        COMMENT "Copying optional Qt OPC UA/OpenSSL runtime files"
        VERBATIM
    )
endfunction()
