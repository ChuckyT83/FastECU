QT += core gui widgets serialport remoteobjects websockets network testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = subaru-facade
INCLUDEPATH += ../.. ../../serial_port
SOURCES += facade.cpp ../../serial_port/serial_port_actions.cpp \
    ../../serial_port/serial_port_actions_direct.cpp ../../serial_port/websocketiodevice.cpp
HEADERS += ../../serial_port/serial_port_actions.h ../../serial_port/serial_port_actions_direct.h \
    ../../serial_port/websocketiodevice.h
REPC_REPLICA += ../../serial_port/serial_port_actions.rep
win32 {
    SOURCES += ../../serial_port/J2534_win.cpp
    HEADERS += ../../serial_port/J2534_win.h
} else {
    SOURCES += ../j2534/mock_j2534.cpp
    HEADERS += ../../serial_port/J2534_linux.h
}

HEADERS += ../../serial_port/j2534_trace.h ../../serial_port/j2534_trace_message.h ../../serial_port/j2534_trace_calls.inc
