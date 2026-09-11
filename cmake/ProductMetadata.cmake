# ProductMetadata.cmake
#
# Loads the single product-metadata source (packaging/product.json) and exposes
# its fields as PRODUCT_* CMake variables in the including scope. Include this
# BEFORE project() so that project(... VERSION ${PRODUCT_VERSION} ...) and the
# generated productinfo.h / app.rc all draw from the same file.
#
# The JSON is intentionally the only place the product name and version are
# defined; both the CMake build and the PowerShell packaging scripts read it, so
# the two never drift apart.

# Resolve packaging/product.json relative to this module (cmake/ -> project root).
get_filename_component(_opcua_project_root "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
set(PRODUCT_METADATA_FILE "${_opcua_project_root}/packaging/product.json"
    CACHE FILEPATH "Path to the product-metadata JSON single source")

if(NOT EXISTS "${PRODUCT_METADATA_FILE}")
    message(FATAL_ERROR "ProductMetadata: metadata file not found: ${PRODUCT_METADATA_FILE}")
endif()

file(READ "${PRODUCT_METADATA_FILE}" _opcua_product_json)

# Read one string field from the JSON into the named parent-scope variable.
function(_opcua_json_get out_var)
    string(JSON _value ERROR_VARIABLE _err GET "${_opcua_product_json}" ${ARGN})
    if(_err)
        message(FATAL_ERROR "ProductMetadata: missing field '${ARGN}' in ${PRODUCT_METADATA_FILE}: ${_err}")
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

_opcua_json_get(PRODUCT_DISPLAY_NAME          displayName)
_opcua_json_get(PRODUCT_IDENTIFIER            identifier)
_opcua_json_get(PRODUCT_ORG_DOMAIN            orgDomain)
_opcua_json_get(PRODUCT_VERSION               version)
_opcua_json_get(PRODUCT_PUBLISHER             publisher)
_opcua_json_get(PRODUCT_COPYRIGHT             copyright)
_opcua_json_get(PRODUCT_HOMEPAGE              homepage)
_opcua_json_get(PRODUCT_EXE_NAME              exeName)
_opcua_json_get(PRODUCT_INSTALL_DIR_NAME      installDirName)
_opcua_json_get(PRODUCT_COMPONENT_ID          componentId)
_opcua_json_get(PRODUCT_MAINTENANCE_TOOL_NAME maintenanceToolName)
_opcua_json_get(PRODUCT_ARTIFACT_BASE         artifactBase)

# Split the semantic version into numeric parts for the Windows VERSIONINFO
# resource (major,minor,patch,build). Missing components default to 0.
if(NOT PRODUCT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "ProductMetadata: version '${PRODUCT_VERSION}' is not MAJOR.MINOR.PATCH")
endif()
set(PRODUCT_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PRODUCT_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PRODUCT_VERSION_PATCH "${CMAKE_MATCH_3}")
set(PRODUCT_VERSION_BUILD "0")

# Absolute, forward-slash path to the .exe icon for the generated app.rc.
# rc.exe accepts forward slashes; this avoids backslash escaping in the .rc.
set(PRODUCT_ICON_PATH "${_opcua_project_root}/resources/images/app/OpcUaManager.ico")
