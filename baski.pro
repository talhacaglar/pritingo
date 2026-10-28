QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = baski
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/filepicker.h \
    src/planner.h \
    src/portalfilepicker.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/planner.cpp \
    src/portalfilepicker.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
