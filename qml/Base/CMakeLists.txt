# Reusable application controls and OPC UA UI components.
qt_add_library(Base STATIC)

qt_add_qml_module(Base
    URI Base
    CLASS_NAME BasePlugin
    QML_FILES
        BsMenuBar.qml
        BsOpcUaBrowser.qml
        BsOpcUaConnectionForm.qml
)

target_link_libraries(Base
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
)
