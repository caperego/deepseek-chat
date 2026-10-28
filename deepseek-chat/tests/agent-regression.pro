QT += testlib
CONFIG += console testcase
TEMPLATE = app
TARGET = agent-regression
SOURCES += agent-regression.cpp
# Reuse the application's actual implementation, compiled for this harness.
include(../app-sources.pri)
