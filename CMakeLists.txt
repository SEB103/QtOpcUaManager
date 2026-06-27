cmake_minimum_required(VERSION 3.21)

project(OpcUaManager
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS
    Core
    Gui
    LinguistTools
    Network
    OpcUa
    Qml
    Quick
    QuickControls2
)

# Fix the Qt6::OpcUa target so that Windows Debug builds use
# Qt6OpcUad.lib and Qt6OpcUad.dll from the selected Qt kit.
include(cmake/ConfigureQtOpcUaDebug.cmake)
opcuamanager_configure_qt_opcua_debug_target()

qt_standard_project_setup(
    REQUIRES 6.11
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES
        de_DE
        en_GB
        ru_RU
        uk_UA
)

include(CTest)
include(GNUInstallDirs)

# Keep all generated QML module metadata under one predictable build-tree root.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

option(
    OPCUAMANAGER_COPY_WINDOWS_RUNTIME
    "Copy the open62541 backend and optional OpenSSL DLLs next to the Windows executable"
    ON
)

# Build the reusable C++ layers and both QML modules.
add_subdirectory(src)
add_subdirectory(qml)

qt_add_executable(appOpcUaManager
    app/main.cpp
    app/appcore.cpp
    app/appcore.h
    app/appengine.cpp
    app/appengine.h
)

# Application-level style configuration and bundled SVG resources.
add_subdirectory(resources)

target_include_directories(appOpcUaManager
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/app"
)

target_link_libraries(appOpcUaManager
    PRIVATE
        OpcUaQmlApi
        MainQmlplugin
        Baseplugin
        Qt6::Core
        Qt6::Gui
        Qt6::OpcUa
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

# Ensure statically linked QML plugins are retained and imported correctly.
qt_import_qml_plugins(appOpcUaManager)

target_compile_definitions(appOpcUaManager
    PRIVATE OPCUAMANAGER_VERSION="${PROJECT_VERSION}"
)

set_target_properties(appOpcUaManager PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

qt_add_translations(appOpcUaManager
    TS_FILES
        translations/OpcUaManager_de_DE.ts
        translations/OpcUaManager_en_GB.ts
        translations/OpcUaManager_ru_RU.ts
        translations/OpcUaManager_uk_UA.ts
    RESOURCE_PREFIX "/translations"
)

# Optional integration point for externally generated or imported UI content.
#
# The imported content may contain QML components, assets, or complete screens
# exported by design tools. It must provide its own CMakeLists.txt file at:
#
#     importedcontent/CMakeLists.txt
#
# If the file does not exist, this block has no effect. The manually maintained
# application QML modules remain under qml/ and are independent of this hook.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(importedcontent)
endif()

add_custom_command(TARGET appOpcUaManager POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/pki"
        "$<TARGET_FILE_DIR:appOpcUaManager>/pki"
    COMMENT "Copying the OPC UA PKI directory"
)

if(WIN32 AND OPCUAMANAGER_COPY_WINDOWS_RUNTIME)
    # Configure copying of the Qt OPC UA backend and OpenSSL runtime
    # libraries to the application output directory on Windows.
    include(cmake/ConfigureWindowsRuntimeCopy.cmake)
    opcuamanager_configure_windows_runtime_copy(appOpcUaManager)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS appOpcUaManager
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/pki/"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/pki"
)

qt_generate_deploy_qml_app_script(
    TARGET appOpcUaManager
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT "${deploy_script}")
