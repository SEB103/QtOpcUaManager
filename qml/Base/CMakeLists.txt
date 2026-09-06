# Reusable application controls and OPC UA UI components.
qt_add_library(Base STATIC)

qt_add_qml_module(Base
    URI Base
    CLASS_NAME BasePlugin
    QML_FILES
        BsAddressSpaceTree.qml
        BsAddressSpaceTreePane.qml
        BsLogPanel.qml
        BsMenuBar.qml
        BsNodeAttributes.qml
        BsNodeDataView.qml
        BsNotificationBanner.qml
        BsOpcUaBrowser.qml
        BsOpcUaConnectionForm.qml
        BsStatusBar.qml
        BsTopBarActions.qml
        BsTrendPanel.qml
        BsTrendSeries.qml
)

target_link_libraries(Base
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
)
