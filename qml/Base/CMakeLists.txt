# Reusable application controls and OPC UA UI components.
qt_add_library(Base STATIC)

# Shared chrome palette, bound to the window theme by Main.qml.
set_source_files_properties(BsTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Base
    URI Base
    CLASS_NAME BasePlugin
    QML_FILES
        BsAddressSpaceTree.qml
        BsAddressSpaceTreePane.qml
        BsLanguageSelector.qml
        BsLogPanel.qml
        BsMenuBar.qml
        BsMenuItem.qml
        BsNetworkScanDialog.qml
        BsNodeAttributes.qml
        BsNodeDataView.qml
        BsNotificationBanner.qml
        BsOpcUaBrowser.qml
        BsOpcUaConnectionForm.qml
        BsStatusBar.qml
        BsTheme.qml
        BsTopBarActions.qml
        BsTrendPanel.qml
        BsTrendSeries.qml
)

target_link_libraries(Base
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
)
