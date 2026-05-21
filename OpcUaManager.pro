QT += qml quick virtualkeyboard opcua

CONFIG += lrelease c++17
CONFIG += embed_translations
CONFIG -= debug_and_release

MOC_DIR = $$OUT_PWD/common/moc
RCC_DIR = $$OUT_PWD/common/rcc
OBJECTS_DIR = $$OUT_PWD/common/obj


DEFINES	+= QMAKE_PROJECT

SOURCES += \
    src/core/appcore.cpp \
    src/core/opcuamanager.cpp \
    src/core/opcuaservice.cpp \
    src/gui/appengine.cpp \
    src/main.cpp \
    src/models/opcuamodel.cpp \
    src/models/treeitem.cpp

INCLUDEPATH += $$PWD/src

HEADERS += \
    src/core/appcore.h \
    src/core/opcuamanager.h \
    src/core/opcuanodedata.h \
    src/core/opcuaservice.h \
    src/gui/appengine.h \
    src/models/opcuamodel.h \
    src/models/treeitem.h

qml.files += \
    qml/Main.qml \
    qml/MainScreen.ui.qml \
    qml/Base/qmldir \
    qml/Base/BsOpcUaBrowser.qml \
    qml/Base/BsMenuBar.qml
qml.prefix = /

style.files += \
    qtquickcontrols2.conf \
    font/fontello.ttf
style.prefix = /

RESOURCES += \
    qml \
    style

TRANSLATIONS += \
    translations/OpcUaManager_de_DE.ts \
    translations/OpcUaManager_en_GB.ts \
    translations/OpcUaManager_ru_RU.ts \
    translations/OpcUaManager_uk_UA.ts

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH += \
    $$PWD/qml
QML2_IMPORT_PATH += \
    $$PWD/qml

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH = $$PWD/qml

include(additional_functions.prf)

win32 {

    message(QT_INSTALL_PREFIX: $$[QT_INSTALL_PREFIX])

    CONFIG(debug, debug|release) {
        OPCUA_BACKEND = $$[QT_INSTALL_PREFIX]/plugins/opcua/open62541_backendd.dll
    } else {
        OPCUA_BACKEND = $$[QT_INSTALL_PREFIX]/plugins/opcua/open62541_backend.dll
    }

    copy_files += \
        $$OPCUA_BACKEND \
        C:/Qt/Tools/OpenSSLv3/Win_x64/bin/libssl-3-x64.dll \
        C:/Qt/Tools/OpenSSLv3/Win_x64/bin/libcrypto-3-x64.dll

    copyFilesToDir($$copy_files, $$OUT_PWD)
    copyDirToDir($$PWD/pki, $$OUT_PWD/pki)
}

export(QMAKE_POST_LINK)

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
