QT += widgets
CONFIG += c++17 release
CONFIG -= app_bundle
TEMPLATE = app
TARGET = XorFileProcessor

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/fileprocessor.cpp

HEADERS += \
    src/mainwindow.h \
    src/fileprocessor.h

win32:QMAKE_LFLAGS += -Wl,--subsystem,windows
