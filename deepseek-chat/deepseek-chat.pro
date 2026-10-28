TEMPLATE = app
TARGET = deepseek-chat
DESTDIR = $$PWD/bin
# Inside the build folder, so Linux (build/) and Windows (build-windows/) builds
# never share objects or the qmake stash.
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc

include(app-sources.pri)

# Icon embedded in the .exe (Explorer, shortcuts, taskbar pins); the window icon
# itself still comes from the PNGs at run time.
win32 {
    RC_ICONS = resources/deepseek.ico
}

macx {
    QMAKE_INFO_PLIST = mac/Info.plist
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
}

SOURCES += \
    src/main.cpp
