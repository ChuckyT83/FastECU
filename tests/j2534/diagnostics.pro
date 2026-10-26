QT += core testlib
QT -= gui
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = j2534-diagnostics-tests
SOURCES += diagnostics.cpp
HEADERS += ../../serial_port/j2534_diagnostics.h ../../serial_port/j2534_trace.h
win32 {
    SOURCES += ../../serial_port/J2534_win.cpp
    HEADERS += ../../serial_port/J2534_win.h ../../tools/j2534_diagnostics/windows_device_api.h
}
