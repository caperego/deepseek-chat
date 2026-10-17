QT += core gui widgets network sql qml quick texttospeech multimedia pdf testlib
CONFIG += c++17 console testcase
TEMPLATE = app
TARGET = agent-regression
INCLUDEPATH += ../src
SOURCES += agent-regression.cpp
# Build the application first; reuse its actual compiled implementation.
APP_BUILD = $$PWD/../build
LIBS += $$APP_BUILD/obj/config.o $$APP_BUILD/obj/theme.o $$APP_BUILD/obj/assets.o \
        $$APP_BUILD/obj/qmltheme.o $$APP_BUILD/obj/apiclient.o \
        $$APP_BUILD/obj/streamclient.o $$APP_BUILD/obj/appcontroller.o \
        $$APP_BUILD/obj/qrc_resources.o $$APP_BUILD/obj/moc_qmltheme.o \
        $$APP_BUILD/obj/moc_apiclient.o $$APP_BUILD/obj/moc_streamclient.o \
        $$APP_BUILD/obj/moc_appcontroller.o
win32: LIBS += -lcrypt32
unix:!macx: LIBS += -lutil
