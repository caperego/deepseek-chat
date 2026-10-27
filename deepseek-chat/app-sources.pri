# Application implementation shared by the app and the test harnesses.
# The harnesses compile these sources themselves, so they build with any
# toolchain (GCC/Clang objects are .o, MSVC objects are .obj).
QT += core gui widgets network sql qml quick texttospeech multimedia pdf webenginequick
CONFIG += c++17
INCLUDEPATH += $$PWD/src

RESOURCES += $$PWD/resources.qrc

win32 {
    LIBS += -lcrypt32
}

unix:!macx {
    LIBS += -lutil
}

SOURCES += \
    $$PWD/src/backgroundtasks.cpp \
    $$PWD/src/terminal.cpp \
    $$PWD/src/workspace.cpp \
    $$PWD/src/agentbrowser.cpp \
    $$PWD/src/config.cpp \
    $$PWD/src/theme.cpp \
    $$PWD/src/assets.cpp \
    $$PWD/src/qmltheme.cpp \
    $$PWD/src/apiclient.cpp \
    $$PWD/src/streamclient.cpp \
    $$PWD/src/appcontroller.cpp

HEADERS += \
    $$PWD/src/chattypes.h \
    $$PWD/src/backgroundtasks.h \
    $$PWD/src/terminaltext.h \
    $$PWD/src/config.h \
    $$PWD/src/pathpolicy.h \
    $$PWD/src/toolprotocol.h \
    $$PWD/src/boundedoutput.h \
    $$PWD/src/automationstore.h \
    $$PWD/src/theme.h \
    $$PWD/src/assets.h \
    $$PWD/src/qmltheme.h \
    $$PWD/src/apiclient.h \
    $$PWD/src/streamclient.h \
    $$PWD/src/appcontroller.h
