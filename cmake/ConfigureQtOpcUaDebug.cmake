# =============================================================================
# ConfigureQtOpcUaDebug.cmake
#
# Purpose:
#   Correct the Debug configuration of the imported Qt6::OpcUa target on
#   Windows. Some Qt installations provide Qt6OpcUad.lib and Qt6OpcUad.dll,
#   but do not map them correctly to the DEBUG configuration in CMake.
#
# When to use:
#   Include this file immediately after find_package(Qt6 ... OpcUa), then call
#   opcuamanager_configure_qt_opcua_debug_target().
#
# What it does:
#   - obtains the Qt installation prefix from the qmake executable belonging
#     to the selected Qt kit;
#   - verifies that the Qt OPC UA Debug import library and DLL exist;
#   - assigns them to the DEBUG configuration of Qt6::OpcUa.
#
# Scope:
#   Windows only. On other platforms the function returns without changes.
#   This file does not copy runtime files; it only fixes the CMake target.
# =============================================================================

function(opcuamanager_configure_qt_opcua_debug_target)
    if(NOT WIN32)
        return()
    endif()

    # find_package(Qt6 ... OpcUa) must have created this imported target first.
    if(NOT TARGET Qt6::OpcUa)
        message(FATAL_ERROR
            "Qt6::OpcUa must exist before "
            "opcuamanager_configure_qt_opcua_debug_target() is called."
        )
    endif()

    # Use qmake from the active Qt kit so that no Qt installation path is
    # hard-coded in the project.
    if(TARGET Qt6::qmake)
        get_target_property(
            qt_qmake_executable
            Qt6::qmake
            IMPORTED_LOCATION
        )
    elseif(DEFINED QT_QMAKE_EXECUTABLE)
        set(qt_qmake_executable "${QT_QMAKE_EXECUTABLE}")
    endif()

    if(NOT qt_qmake_executable)
        message(WARNING
            "Unable to determine the qmake executable of the selected Qt kit. "
            "The Qt OPC UA Debug target was not adjusted."
        )
        return()
    endif()

    # Query the installation root of the Qt kit selected by CMake/Qt Creator.
    execute_process(
        COMMAND
            "${qt_qmake_executable}"
            -query
            QT_INSTALL_PREFIX

        OUTPUT_VARIABLE qt_install_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE qmake_query_result
    )

    if(NOT qmake_query_result EQUAL 0 OR NOT qt_install_prefix)
        message(WARNING
            "Unable to determine the Qt installation prefix. "
            "The Qt OPC UA Debug target was not adjusted."
        )
        return()
    endif()

    set(qt_opcua_debug_implib
        "${qt_install_prefix}/lib/Qt6OpcUad.lib"
    )

    set(qt_opcua_debug_dll
        "${qt_install_prefix}/bin/Qt6OpcUad.dll"
    )

    # Do not modify Qt6::OpcUa unless both Debug files are available.
    if(NOT EXISTS "${qt_opcua_debug_implib}"
       OR NOT EXISTS "${qt_opcua_debug_dll}")

        message(WARNING
            "Qt OPC UA Debug libraries were not found under "
            "${qt_install_prefix}."
        )
        return()
    endif()

    # Explicitly map the Qt OPC UA Debug files to the DEBUG configuration.
    set_property(
        TARGET Qt6::OpcUa
        APPEND PROPERTY
        IMPORTED_CONFIGURATIONS DEBUG
    )

    set_target_properties(
        Qt6::OpcUa
        PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${qt_opcua_debug_implib}"
            IMPORTED_LOCATION_DEBUG "${qt_opcua_debug_dll}"
    )

    message(STATUS
        "Qt OPC UA Debug import library: ${qt_opcua_debug_implib}"
    )

    message(STATUS
        "Qt OPC UA Debug runtime library: ${qt_opcua_debug_dll}"
    )
endfunction()
