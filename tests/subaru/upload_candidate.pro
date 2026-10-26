QT += core gui widgets serialport remoteobjects websockets network testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = sh7058-upload-candidate
INCLUDEPATH += ../.. ../../serial_port
SOURCES += upload_candidate.cpp ../../modules/ecu/subaru_sh7058_upload_candidate.cpp ../../serial_port/sh7058_candidate_link.cpp ../../serial_port/serial_port_actions.cpp \
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

HEADERS += ../../modules/ecu/subaru_sh7058_upload_candidate.h ../../serial_port/sh7058_candidate_link.h
