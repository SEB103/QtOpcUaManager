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

qt_add_executable(tst_opcuaintegration
    integration/tst_opcuaintegration.cpp
)

target_link_libraries(tst_opcuaintegration
    PRIVATE
        Qt6::OpcUa
        Qt6::Test
)

add_test(NAME OpcUa.ExternalIntegration COMMAND tst_opcuaintegration)

qt_add_executable(tst_qml
    qml/tst_qml.cpp
)

target_compile_definitions(tst_qml
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)

target_link_libraries(tst_qml
    PRIVATE
        Baseplugin
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickTest
)

add_test(NAME OpcUaManager.QmlSmoke COMMAND tst_qml)
set_tests_properties(OpcUaManager.QmlSmoke PROPERTIES
    ENVIRONMENT "QML2_IMPORT_PATH=${QT_QML_OUTPUT_DIRECTORY}"
)
