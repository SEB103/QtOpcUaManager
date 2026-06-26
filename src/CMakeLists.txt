# OPC UA worker/service layer and the reusable tree model.
qt_add_library(OpcUaCore STATIC)

target_sources(OpcUaCore
    PRIVATE
        core/opcuanodedata.h
        core/opcuaservice.cpp
        core/opcuaservice.h
        models/opcuamodel.cpp
        models/opcuamodel.h
        models/treeitem.cpp
        models/treeitem.h
)

target_include_directories(OpcUaCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(OpcUaCore
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::OpcUa
)

# C++ facade exposed to QML as the cppManagerOpcUa context property.
qt_add_library(OpcUaQmlApi STATIC)

target_sources(OpcUaQmlApi
    PRIVATE
        qmlapi/opcuamanager.cpp
        qmlapi/opcuamanager.h
)

target_include_directories(OpcUaQmlApi
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(OpcUaQmlApi
    PUBLIC
        OpcUaCore
        Qt6::Core
)
