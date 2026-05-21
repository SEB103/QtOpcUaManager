cmake_minimum_required(VERSION 3.16)

# Configure MSVC debug information format before project targets are created.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

project(OpcUaManager VERSION 0.1 LANGUAGES CXX)

# Configure the C++ language baseline used by all targets in this project.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Remove conflicting MSVC Program Database flags after selecting embedded debug info.
if(MSVC)
    foreach(config DEBUG RELWITHDEBINFO)
        string(REPLACE "/Zi" "" CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
        string(REGEX REPLACE " +" " " CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}")
        string(STRIP "${CMAKE_CXX_FLAGS_${config}}" CMAKE_CXX_FLAGS_${config})
        set(CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_${config}}" CACHE STRING "" FORCE)
    endforeach()
endif()

# Find all Qt modules required by the application and QML modules.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml QmlIntegration Quick QuickControls2 LinguistTools OpcUa VirtualKeyboard)

# Patch the Qt OPC UA imported target when the selected Windows kit provides a debug library.
if(WIN32)
    if(TARGET Qt6::qmake)
        get_target_property(QT_QMAKE_EXECUTABLE_FROM_KIT Qt6::qmake IMPORTED_LOCATION)
    elseif(DEFINED QT_QMAKE_EXECUTABLE)
        set(QT_QMAKE_EXECUTABLE_FROM_KIT "${QT_QMAKE_EXECUTABLE}")
    endif()

    if(QT_QMAKE_EXECUTABLE_FROM_KIT)
        execute_process(
            COMMAND "${QT_QMAKE_EXECUTABLE_FROM_KIT}" -query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE QT_INSTALL_PREFIX_FROM_KIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE QT_QMAKE_QUERY_RESULT
        )

        if(QT_QMAKE_QUERY_RESULT EQUAL 0 AND QT_INSTALL_PREFIX_FROM_KIT)
            set(QT_OPCUA_MODULE_DEBUG_IMPLIB "${QT_INSTALL_PREFIX_FROM_KIT}/lib/Qt6OpcUad.lib")
            set(QT_OPCUA_MODULE_DEBUG_DLL "${QT_INSTALL_PREFIX_FROM_KIT}/bin/Qt6OpcUad.dll")

            if(EXISTS "${QT_OPCUA_MODULE_DEBUG_IMPLIB}" AND EXISTS "${QT_OPCUA_MODULE_DEBUG_DLL}")
                set_property(TARGET Qt6::OpcUa APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
                set_target_properties(Qt6::OpcUa PROPERTIES
                    IMPORTED_IMPLIB_DEBUG "${QT_OPCUA_MODULE_DEBUG_IMPLIB}"
                    IMPORTED_LOCATION_DEBUG "${QT_OPCUA_MODULE_DEBUG_DLL}"
                )
                message(STATUS "Qt OPC UA debug module: ${QT_OPCUA_MODULE_DEBUG_IMPLIB}")
            endif()
        endif()
    endif()
endif()

# Apply Qt's common project defaults and translation language configuration.
qt_standard_project_setup(REQUIRES I18N_TRANSLATED_LANGUAGES de_DE en_GB ru_RU uk_UA )

# Make source-tree QML modules visible to Qt Creator's code model.
set(QML_IMPORT_PATH "${CMAKE_SOURCE_DIR}/qml" CACHE STRING "" FORCE)
set(QML2_IMPORT_PATH "${CMAKE_SOURCE_DIR}/qml" CACHE STRING "" FORCE)

# Use the modern Qt QML module layout and resource path behavior.
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# C++ types registered as the QML-facing C++ API module.
set(CPP_QML_API_SOURCES
    src/core/opcuamanager.cpp
    src/core/opcuamanager.h
    src/models/opcuamodel.cpp
    src/models/opcuamodel.h
    src/models/treeitem.cpp
    src/models/treeitem.h
)

# C++ sources compiled directly into the application executable.
set(APP_SOURCES
    src/main.cpp
    src/core/appcore.cpp
    src/core/appcore.h
    src/core/opcuaservice.cpp
    src/core/opcuaservice.h
    src/core/opcuanodedata.h
    src/gui/appengine.cpp
    src/gui/appengine.h
)

# Build the static C++ QML API module that exposes selected C++ types to QML metadata.
qt_add_library(CppQmlApi STATIC)

target_include_directories(CppQmlApi PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/models
)

target_link_libraries(CppQmlApi PUBLIC
    Qt6::Core
    Qt6::Qml
    Qt6::QmlIntegration
    Qt6::OpcUa
)

qt_add_qml_module(CppQmlApi
    URI CppQmlApi
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/CppQmlApi"
    NO_PLUGIN
    SOURCES ${CPP_QML_API_SOURCES}
)

# Build the executable and attach application resources.
qt_add_executable(appOpcUaManager ${APP_SOURCES})

qt_add_resources(appOpcUaManager "style"
    PREFIX "/"
    FILES
        font/fontello.ttf
        qtquickcontrols2.conf
)

target_include_directories(appOpcUaManager
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/models
    # ${CMAKE_SOURCE_DIR}/src/gui
)

# Build the application main QML module. The target name describes the file role,
# while the URI remains the public QML namespace used by loadFromModule().
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_library(MainQml STATIC)
qt_add_qml_module(MainQml
    URI OpcUaManager
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/OpcUaManager"
    NO_GENERATE_EXTRA_QMLDIRS
    QML_FILES
        qml/Main.qml
        qml/MainScreen.ui.qml
)

# Build the remaining QML component modules under qml/.
add_subdirectory(qml)

# Configure executable metadata and platform windowing behavior.
# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appOpcUaManager PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appOpcUaManager
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Link the executable to Qt, C++ API, and generated QML plugin targets.
target_link_libraries(appOpcUaManager
    PRIVATE Qt6::Core
    PRIVATE Qt6::Gui
    PRIVATE Qt6::OpcUa
    PRIVATE Qt6::Qml
    PRIVATE Qt6::QmlIntegration
    PRIVATE Qt6::Quick
    PRIVATE Qt6::QuickControls2
    PRIVATE Qt6::VirtualKeyboard
    PRIVATE CppQmlApi
    PRIVATE MainQmlplugin
    PRIVATE Baseplugin
)

# Normalize PDB output names for selected Qt-generated helper targets on MSVC.
function(set_msvc_compile_pdb_name target_name)
    if(MSVC AND TARGET "${target_name}")
        set_target_properties("${target_name}" PROPERTIES
            COMPILE_PDB_NAME "${target_name}"
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pdb"
        )
    endif()
endfunction()

foreach(qt_generated_target IN ITEMS
    CppQmlApi_resources_1
    MainQml_resources_1
    MainQml_resources_2
    MainQml_resources_3
    MainQmlplugin_init
    Base_resources_1
    Base_resources_2
    Baseplugin_init
)
    set_msvc_compile_pdb_name("${qt_generated_target}")
endforeach()

# Copy Windows runtime dependencies next to the built executable.
if(WIN32)
    if(QT_INSTALL_PREFIX_FROM_KIT)
        set(qt_opcua_plugin_dir "${QT_INSTALL_PREFIX_FROM_KIT}/plugins/opcua")
    else()
        set(qt_opcua_plugin_dir "$ENV{QTDIR}/plugins/opcua")
    endif()

    add_custom_command(TARGET appOpcUaManager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${qt_opcua_plugin_dir}/open62541_backendd.dll,${qt_opcua_plugin_dir}/open62541_backend.dll>"
            "$<TARGET_FILE_DIR:appOpcUaManager>"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/Qt/Tools/OpenSSLv3/Win_x64/bin/libssl-3-x64.dll"
            "$<TARGET_FILE_DIR:appOpcUaManager>"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "C:/Qt/Tools/OpenSSLv3/Win_x64/bin/libcrypto-3-x64.dll"
            "$<TARGET_FILE_DIR:appOpcUaManager>"

        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/pki"
            "$<TARGET_FILE_DIR:appOpcUaManager>/pki"
    )
endif()

# Add Qt Linguist translation targets and resources for the executable.
qt_add_translations(appOpcUaManager
    # RESOURCE_PREFIX /translations # qrc:/translations/
    TS_FILE_BASE OpcUaManager
    TS_FILE_DIR translations
)

# Install the application target using standard GNU install directories.
include(GNUInstallDirs)

install(TARGETS appOpcUaManager
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
