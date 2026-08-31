# OPC UA worker/service layer and the reusable tree model.
qt_add_library(OpcUaCore STATIC)

target_sources(OpcUaCore
    PRIVATE
        core/opcuanodedata.h
        core/opcuaservice.cpp
        core/opcuaservice.h
        core/opcuavaluedata.h
        core/opcuavaluetree.h
        core/structurednodereader.cpp
        core/structurednodereader.h
        models/attributesmodel.cpp
        models/attributesmodel.h
        models/dataaccessmodel.cpp
        models/dataaccessmodel.h
        models/opcuamodel.cpp
        models/opcuamodel.h
        models/structuredvalueformatter.cpp
        models/structuredvalueformatter.h
        models/treeitem.cpp
        models/treeitem.h
        persistence/nodedatabase.cpp
        persistence/nodedatabase.h
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
        Qt6::Sql
)

# C++ facade exposed to QML as the cppManagerOpcUa context property.
qt_add_library(OpcUaQmlApi STATIC)

target_sources(OpcUaQmlApi
    PRIVATE
        qmlapi/opcuamanager.cpp
        qmlapi/opcuamanager.h
        qmlapi/structuredvaluehighlighter.cpp
        qmlapi/structuredvaluehighlighter.h
)

target_include_directories(OpcUaQmlApi
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(OpcUaQmlApi
    PUBLIC
        OpcUaCore
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
)
