QT += core gui widgets network sql qml quick texttospeech multimedia pdf

CONFIG += c++17
TEMPLATE = app
TARGET = deepseek-chat
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc

RESOURCES += resources.qrc

macx {
    QMAKE_INFO_PLIST = mac/Info.plist
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
}

win32 {
    LIBS += -lcrypt32
}

unix:!macx {
    LIBS += -lutil
}

SOURCES += \
    src/main.cpp \
    src/config.cpp \
    src/theme.cpp \
    src/assets.cpp \
    src/qmltheme.cpp \
    src/apiclient.cpp \
    src/streamclient.cpp \
    src/appcontroller.cpp

HEADERS += \
    src/chattypes.h \
    src/config.h \
    src/pathpolicy.h \
    src/toolprotocol.h \
    src/boundedoutput.h \
    src/automationstore.h \
    src/theme.h \
    src/assets.h \
    src/qmltheme.h \
    src/apiclient.h \
    src/streamclient.h \
    src/appcontroller.h
