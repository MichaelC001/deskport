QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = deskport-display
SOURCES += display-helper.cpp
LIBS += -luser32 -lsetupapi -luuid -lshell32
win32-g++: QMAKE_LFLAGS += -static
