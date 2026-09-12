# OPC UA worker/service layer and the reusable tree model.
qt_add_library(OpcUaCore STATIC)

target_sources(OpcUaCore
    PRIVATE
        core/apppaths.cpp
        core/apppaths.h
        core/diagnosticslevel.h
        core/opcuaaccesslevel.h
        core/opcuanodedata.h
        core/opcuaservice.cpp
        core/opcuaservice.h
        core/opcuastatushint.cpp
        core/opcuastatushint.h
        core/opcuavaluedata.h
        core/opcuavaluetree.h
        core/structurednodereader.cpp
        core/structurednodereader.h
        models/attributesmodel.cpp
        models/attributesmodel.h
        models/dataaccessmodel.cpp
        models/dataaccessmodel.h
        models/dataviewfiltermodel.cpp
        models/dataviewfiltermodel.h
        models/logfiltermodel.cpp
        models/logfiltermodel.h
        models/logmodel.cpp
        models/logmodel.h
        models/opcuamodel.cpp
        models/opcuamodel.h
        models/structuredvalueformatter.cpp
        models/structuredvalueformatter.h
        models/trendmodel.cpp
        models/trendmodel.h
        models/treeitem.cpp
        models/treeitem.h
        persistence/nodedatabase.cpp
        persistence/nodedatabase.h
        project/projectdata.h
        project/projectserializer.cpp
        project/projectserializer.h
)

target_include_directories(OpcUaCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(OpcUaCore
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::OpcUa
        Qt6::Sql
)

# Server Studio domain model shared by the GUI editor and the headless runtime.
# Qt6::Core only: no GUI, Quick, Qt OPC UA or open62541 dependency, so the
# runtime can link it without pulling in the client stack.
qt_add_library(ServerProjectCore STATIC)

target_sources(ServerProjectCore
    PRIVATE
        serverproject/serverprojectdata.cpp
        serverproject/serverprojectdata.h
        serverproject/serverprojectserializer.cpp
        serverproject/serverprojectserializer.h
        serverproject/serverprojectvalidator.cpp
        serverproject/serverprojectvalidator.h
)

target_include_directories(ServerProjectCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ServerProjectCore
    PUBLIC
        Qt6::Core
)

# C++ facade exposed to QML as the cppManagerOpcUa context property.
qt_add_library(OpcUaQmlApi STATIC)

target_sources(OpcUaQmlApi
    PRIVATE
        qmlapi/opcuamanager.cpp
        qmlapi/opcuamanager.h
        qmlapi/projectmanager.cpp
        qmlapi/projectmanager.h
        qmlapi/servernodemodel.cpp
        qmlapi/servernodemodel.h
        qmlapi/serverruntimecontroller.cpp
        qmlapi/serverruntimecontroller.h
        qmlapi/serverstudio.cpp
        qmlapi/serverstudio.h
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
        ServerProjectCore
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
)
