QT += core gui widgets serialport testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = j2534-transport
SOURCES += transport.cpp ../../serial_port/serial_port_actions_direct.cpp
HEADERS += ../../serial_port/serial_port_actions_direct.h
win32 {
    SOURCES += ../../serial_port/J2534_win.cpp
    HEADERS += ../../serial_port/J2534_win.h
} else {
    SOURCES += mock_j2534.cpp
    HEADERS += ../../serial_port/J2534_linux.h
}
