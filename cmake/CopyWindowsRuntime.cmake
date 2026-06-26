# =============================================================================
# CopyWindowsRuntime.cmake
#
# Purpose:
#   Copy the optional Windows runtime files required by Qt OPC UA into the
#   application output directory after the executable has been built.
#
# Execution:
#   This file is not included by the main CMakeLists.txt. It is launched in
#   CMake script mode (cmake -P) by ConfigureWindowsRuntimeCopy.cmake.
#
# Input variables:
#   DESTINATION_DIR  Required. Directory containing the built executable.
#   BUILD_CONFIG     Build configuration, for example Debug or Release.
#   OPCUA_PLUGIN_DIR Directory containing Qt OPC UA backend plugins.
#   OPENSSL_BIN_DIR  Optional directory containing OpenSSL 3 runtime DLLs.
#
# Output layout:
#   <DESTINATION_DIR>/opcua/open62541_backend[d].dll
#   <DESTINATION_DIR>/libssl-3[-x64].dll
#   <DESTINATION_DIR>/libcrypto-3[-x64].dll
#
# Missing optional files are reported but do not fail the build. A copy
# operation itself is treated as fatal if the selected source file exists but
# cannot be copied.
# =============================================================================

# The destination is the only mandatory input because all copied files are
# written relative to this directory.
if(NOT DEFINED DESTINATION_DIR OR DESTINATION_DIR STREQUAL "")
    message(FATAL_ERROR "DESTINATION_DIR was not provided.")
endif()

file(MAKE_DIRECTORY "${DESTINATION_DIR}")
file(MAKE_DIRECTORY "${DESTINATION_DIR}/opcua")

# Select the plugin name that matches the active build configuration.
set(_backend_name "open62541_backend.dll")
if(BUILD_CONFIG STREQUAL "Debug")
    set(_backend_name "open62541_backendd.dll")
endif()

# Qt discovers OPC UA backend plugins by category, therefore the backend is
# kept in the dedicated opcua/ subdirectory rather than beside the executable.
if(DEFINED OPCUA_PLUGIN_DIR AND EXISTS "${OPCUA_PLUGIN_DIR}/${_backend_name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${OPCUA_PLUGIN_DIR}/${_backend_name}"
            "${DESTINATION_DIR}/opcua/${_backend_name}"
        COMMAND_ERROR_IS_FATAL ANY
    )
else()
    message(STATUS
        "Qt OPC UA backend was not copied: ${OPCUA_PLUGIN_DIR}/${_backend_name} was not found. "
        "Qt Creator may still resolve it through the active Qt installation.")
endif()

# OpenSSL is optional for basic OPC UA operation but is required for security
# policies that depend on cryptographic functionality.
if(DEFINED OPENSSL_BIN_DIR AND NOT OPENSSL_BIN_DIR STREQUAL "")
    set(_openssl_candidates
        libssl-3-x64.dll
        libcrypto-3-x64.dll
        libssl-3.dll
        libcrypto-3.dll
    )

    # Support the common OpenSSL 3 naming variants used by Qt installations.
    foreach(_dll IN LISTS _openssl_candidates)
        if(EXISTS "${OPENSSL_BIN_DIR}/${_dll}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${OPENSSL_BIN_DIR}/${_dll}"
                    "${DESTINATION_DIR}/${_dll}"
                COMMAND_ERROR_IS_FATAL ANY
            )
        endif()
    endforeach()
else()
    message(STATUS
        "OPCUAMANAGER_OPENSSL_BIN_DIR is empty; OpenSSL DLLs were not copied. "
        "Set this CMake cache path if the DLLs are not already available through PATH.")
endif()
