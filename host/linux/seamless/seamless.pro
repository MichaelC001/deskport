QT += core gui waylandcompositor waylandcompositorxdgshell

CONFIG += console c++17
CONFIG -= app_bundle

!linux: error(deskport-seamless-host is supported only on Linux)

TARGET = deskport-seamless-host

SOURCES += \
    main.cpp \
    seamlesshost.cpp

HEADERS += seamlesshost.h

target.path = $$PREFIX/libexec
INSTALLS += target
