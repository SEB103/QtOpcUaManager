find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

qt_add_executable(tst_opcuamodel
    unit/tst_opcuamodel.cpp
)

target_link_libraries(tst_opcuamodel
    PRIVATE
        OpcUaCore
        Qt6::Test
)

add_test(NAME OpcUaModel.Unit COMMAND tst_opcuamodel)
