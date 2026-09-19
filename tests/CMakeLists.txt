find_package(Qt6 6.11 REQUIRED COMPONENTS QuickTest Test)

qt_add_executable(tst_opcuamodel
    unit/tst_opcuamodel.cpp
)

target_link_libraries(tst_opcuamodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME OpcUaModel.Unit COMMAND tst_opcuamodel)

# HelpServer lives in the application layer (app/); compile it into the test.
qt_add_executable(tst_helpserver
    unit/tst_helpserver.cpp
    "${CMAKE_SOURCE_DIR}/app/helpserver.cpp"
    "${CMAKE_SOURCE_DIR}/app/helpserver.h"
)

target_include_directories(tst_helpserver
    PRIVATE "${CMAKE_SOURCE_DIR}/app"
)

target_link_libraries(tst_helpserver
    PRIVATE
        Qt6::Network
        Qt6::Test
)

add_test(NAME HelpServer.Unit COMMAND tst_helpserver)

# UpdateController lives in the application layer (app/); compile it into the test.
# It includes the generated productinfo.h, so add the generated directory too.
qt_add_executable(tst_updatecontroller
    unit/tst_updatecontroller.cpp
    "${CMAKE_SOURCE_DIR}/app/updatecontroller.cpp"
    "${CMAKE_SOURCE_DIR}/app/updatecontroller.h"
)

target_include_directories(tst_updatecontroller
    PRIVATE
        "${CMAKE_SOURCE_DIR}/app"
        "${OPCUA_GENERATED_DIR}"
)

target_link_libraries(tst_updatecontroller
    PRIVATE
        Qt6::Network
        Qt6::Test
)

add_test(NAME UpdateController.Unit COMMAND tst_updatecontroller)

qt_add_executable(tst_nodedatabase
    unit/tst_nodedatabase.cpp
)

target_link_libraries(tst_nodedatabase
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME NodeDatabase.Unit COMMAND tst_nodedatabase)

qt_add_executable(tst_dataaccessmodel
    unit/tst_dataaccessmodel.cpp
)

target_link_libraries(tst_dataaccessmodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME DataAccessModel.Unit COMMAND tst_dataaccessmodel)

qt_add_executable(tst_dataviewfiltermodel
    unit/tst_dataviewfiltermodel.cpp
)

target_link_libraries(tst_dataviewfiltermodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME DataViewFilterModel.Unit COMMAND tst_dataviewfiltermodel)

qt_add_executable(tst_attributesmodel
    unit/tst_attributesmodel.cpp
)

target_link_libraries(tst_attributesmodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME AttributesModel.Unit COMMAND tst_attributesmodel)

qt_add_executable(tst_logmodel
    unit/tst_logmodel.cpp
)

target_link_libraries(tst_logmodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME LogModel.Unit COMMAND tst_logmodel)

qt_add_executable(tst_opcuastatushint
    unit/tst_opcuastatushint.cpp
)

target_link_libraries(tst_opcuastatushint
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME OpcUaStatusHint.Unit COMMAND tst_opcuastatushint)

qt_add_executable(tst_trendmodel
    unit/tst_trendmodel.cpp
)

target_link_libraries(tst_trendmodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME TrendModel.Unit COMMAND tst_trendmodel)

qt_add_executable(tst_projectserializer
    unit/tst_projectserializer.cpp
)

target_link_libraries(tst_projectserializer
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME ProjectSerializer.Unit COMMAND tst_projectserializer)

qt_add_executable(tst_opcuamanager
    unit/tst_opcuamanager.cpp
)

target_link_libraries(tst_opcuamanager
    PRIVATE
        OpcUaQmlApi
        Qt6::Test
)

add_test(NAME OpcUaManager.Unit COMMAND tst_opcuamanager)

qt_add_executable(tst_projectmanager
    unit/tst_projectmanager.cpp
)

target_link_libraries(tst_projectmanager
    PRIVATE
        OpcUaQmlApi
        Qt6::Test
)

add_test(NAME ProjectManager.Unit COMMAND tst_projectmanager)

qt_add_executable(tst_structuredvalueformatter
    unit/tst_structuredvalueformatter.cpp
)

target_link_libraries(tst_structuredvalueformatter
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME StructuredValueFormatter.Unit COMMAND tst_structuredvalueformatter)

qt_add_executable(tst_serverprojectserializer
    unit/tst_serverprojectserializer.cpp
)

target_link_libraries(tst_serverprojectserializer
    PRIVATE
        ServerProjectCore
        Qt6::Test
)

add_test(NAME ServerProjectSerializer.Unit COMMAND tst_serverprojectserializer)

qt_add_executable(tst_serverprojectvalidator
    unit/tst_serverprojectvalidator.cpp
)

target_link_libraries(tst_serverprojectvalidator
    PRIVATE
        ServerProjectCore
        Qt6::Test
)

add_test(NAME ServerProjectValidator.Unit COMMAND tst_serverprojectvalidator)

qt_add_executable(tst_serverprojectnodeset
    unit/tst_serverprojectnodeset.cpp
)

target_link_libraries(tst_serverprojectnodeset
    PRIVATE
        ServerProjectCore
        Qt6::Test
)

add_test(NAME ServerProjectNodeSet.Unit COMMAND tst_serverprojectnodeset)

qt_add_executable(tst_serverclone
    unit/tst_serverclone.cpp
)

target_link_libraries(tst_serverclone
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME ServerClone.Unit COMMAND tst_serverclone)

qt_add_executable(tst_opcuaintegration
    integration/tst_opcuaintegration.cpp
)

target_link_libraries(tst_opcuaintegration
    PRIVATE
        Qt6::OpcUa
        Qt6::Test
)

add_test(NAME OpcUa.ExternalIntegration COMMAND tst_opcuaintegration)

# Integration test that launches the headless OpcUaServerRuntime as a fixture
# and drives a real client against it. The runtime path is injected at compile
# time so the test can locate the freshly built executable.
qt_add_executable(tst_serverruntime
    integration/tst_serverruntime.cpp
)

target_link_libraries(tst_serverruntime
    PRIVATE
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serverruntime
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serverruntime OpcUaServerRuntime)

add_test(NAME ServerRuntime.Integration COMMAND tst_serverruntime)

# Integration test that the runtime builds its address space from a .uaserver
# project and a client can browse the designed nodes.
qt_add_executable(tst_serverprojectruntime
    integration/tst_serverprojectruntime.cpp
)

target_link_libraries(tst_serverprojectruntime
    PRIVATE
        ServerProjectCore
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serverprojectruntime
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serverprojectruntime OpcUaServerRuntime)

add_test(NAME ServerProjectRuntime.Integration COMMAND tst_serverprojectruntime)

# Integration test for the Security Test Lab: authentication enforcement and
# encrypted-endpoint advertisement.
qt_add_executable(tst_serversecurity
    integration/tst_serversecurity.cpp
)

target_link_libraries(tst_serversecurity
    PRIVATE
        ServerProjectCore
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serversecurity
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serversecurity OpcUaServerRuntime appOpcUaManager)

add_test(NAME ServerSecurity.Integration COMMAND tst_serversecurity)

# Integration test for live diagnostics and the kill/reconnect workflow.
qt_add_executable(tst_serverdiagnostics
    integration/tst_serverdiagnostics.cpp
)

target_link_libraries(tst_serverdiagnostics
    PRIVATE
        Qt6::Network
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serverdiagnostics
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serverdiagnostics OpcUaServerRuntime appOpcUaManager)

add_test(NAME ServerDiagnostics.Integration COMMAND tst_serverdiagnostics)

# Integration test that a simulated variable's value changes propagate through
# an ordinary subscription.
qt_add_executable(tst_serversimulation
    integration/tst_serversimulation.cpp
)

target_link_libraries(tst_serversimulation
    PRIVATE
        ServerProjectCore
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serversimulation
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serversimulation OpcUaServerRuntime appOpcUaManager)

add_test(NAME ServerSimulation.Integration COMMAND tst_serversimulation)

# Integration test that behavior rules write their targets on client writes.
qt_add_executable(tst_serverrules
    integration/tst_serverrules.cpp
)

target_link_libraries(tst_serverrules
    PRIVATE
        ServerProjectCore
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serverrules
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serverrules OpcUaServerRuntime appOpcUaManager)

add_test(NAME ServerRules.Integration COMMAND tst_serverrules)

# Integration test that the recursive clone browser captures the full subtree.
qt_add_executable(tst_serverclonebrowse
    integration/tst_serverclonebrowse.cpp
)

target_link_libraries(tst_serverclonebrowse
    PRIVATE
        OpcUaCore
        ServerProjectCore
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serverclonebrowse
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serverclonebrowse OpcUaServerRuntime appOpcUaManager)

add_test(NAME ServerCloneBrowse.Integration COMMAND tst_serverclonebrowse)

# Integration test for custom enumeration data types.
qt_add_executable(tst_serverenum
    integration/tst_serverenum.cpp
)

target_link_libraries(tst_serverenum
    PRIVATE
        ServerProjectCore
        Qt6::OpcUa
        Qt6::Test
)

target_compile_definitions(tst_serverenum
    PRIVATE OPCUA_SERVER_RUNTIME_PATH="$<TARGET_FILE:OpcUaServerRuntime>"
)

add_dependencies(tst_serverenum OpcUaServerRuntime appOpcUaManager)

add_test(NAME ServerEnum.Integration COMMAND tst_serverenum)

qt_add_executable(tst_qml
    qml/tst_qml.cpp
)

target_sources(tst_qml
    PRIVATE
        qml/tst_dataview.qml
        qml/tst_diagnostics.qml
        qml/tst_trend.qml
        qml/tst_menubar.qml
        qml/README.md
)

target_compile_definitions(tst_qml
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

target_link_libraries(tst_qml
    PRIVATE
        Baseplugin
        OpcUaCore
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickTest
)

add_test(NAME OpcUaManager.QmlSmoke COMMAND tst_qml)
set_tests_properties(OpcUaManager.QmlSmoke PROPERTIES
    ENVIRONMENT "QML2_IMPORT_PATH=${QT_QML_OUTPUT_DIRECTORY}"
)
