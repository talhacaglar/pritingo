QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
DEFINES += SRCDIR=\\\"$$PWD\\\"
TARGET = baski_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/filepicker.h ../src/planner.h ../src/portalfilepicker.h \
    ../src/theme.h ../src/translator.h
SOURCES += baski_tests.cpp ../src/backend.cpp ../src/planner.cpp ../src/portalfilepicker.cpp \
    ../src/theme.cpp ../src/translator.cpp
