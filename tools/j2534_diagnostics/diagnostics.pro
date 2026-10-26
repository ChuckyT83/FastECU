QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = fastecu-j2534-diagnostics
SOURCES += main.cpp
HEADERS += ../../serial_port/j2534_diagnostics.h ../../serial_port/j2534_trace.h
win32 {
    SOURCES += ../../serial_port/J2534_win.cpp
    HEADERS += ../../serial_port/J2534_win.h
}
