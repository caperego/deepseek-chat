TEMPLATE = app
TARGET = deepseek-chat
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc

include(app-sources.pri)

macx {
    QMAKE_INFO_PLIST = mac/Info.plist
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
}

SOURCES += \
    src/main.cpp
