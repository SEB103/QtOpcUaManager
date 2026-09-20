# Headless OPC UA server runtime.
#
# This is a standalone executable, deliberately separate from the GUI
# application: the existing OPC UA client must reach it over a real opc.tcp
# connection so discovery, sessions, subscriptions, connection loss, restart
# and crash can all be exercised realistically. It embeds open62541 directly
# and links only Qt6::Core -- never Qt6::Gui/Quick or Qt6::OpcUa.

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Network)

# Acquire open62541 as the embedded server stack. A separate process means its
# symbols never clash with the open62541 build inside Qt's OPC UA client
# backend. Encryption uses the OpenSSL backend, reusing the OpenSSL that ships
# with the Qt installation (the same one the client backend already deploys).
include(FetchContent)

if(WIN32 AND NOT DEFINED OPENSSL_ROOT_DIR)
    set(_opcua_qt_openssl "C:/Qt/Tools/OpenSSLv3/Win_x64")
    if(EXISTS "${_opcua_qt_openssl}/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "${_opcua_qt_openssl}" CACHE PATH "OpenSSL root for open62541")
    endif()
endif()

set(UA_ENABLE_AMALGAMATION OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_ENCRYPTION "OPENSSL" CACHE STRING "" FORCE)
set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(UA_FORCE_WERROR OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# EXCLUDE_FROM_ALL keeps open62541's own install() rules (headers, static
# library, CMake package files) out of the canonical deployment produced by
# cmake --install; the library itself is still built as a dependency of
# OpcUaServerRuntime.
FetchContent_Declare(open62541
    GIT_REPOSITORY https://github.com/open62541/open62541.git
    GIT_TAG v1.4.14
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(open62541)

qt_add_executable(OpcUaServerRuntime
    main.cpp
    diagnosticsserver.cpp
    diagnosticsserver.h
    projectbuilder.cpp
    projectbuilder.h
    ruleengine.cpp
    ruleengine.h
    securitysetup.cpp
    securitysetup.h
    simulationengine.cpp
    simulationengine.h
)

target_link_libraries(OpcUaServerRuntime
    PRIVATE
        Qt6::Core
        Qt6::Network
        ServerProjectCore
        open62541::open62541
)

# The GUI controller resolves the runtime next to the application executable
# (QCoreApplication::applicationDirPath()). In the build tree the app lands in
# the top-level binary dir, so place the runtime there too.
set_target_properties(OpcUaServerRuntime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE FALSE
)

install(TARGETS OpcUaServerRuntime
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
