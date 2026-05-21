# Build the Base QML module used by qml/MainScreen.ui.qml.
# Only components reached from the active reference UI remain here; older
# prototypes such as drawers, toolbars, message labels, and button panels are
# preserved under Archive/reference_qmake_working.
qt_add_library(Base STATIC)
qt6_add_qml_module(Base
    URI "Base"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        "BsMenuBar.qml"
        "BsOpcUaBrowser.qml"
)


