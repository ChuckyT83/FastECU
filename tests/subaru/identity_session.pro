QT += core gui widgets serialport xml testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = sh7058-identity-session
INCLUDEPATH += ../..
SOURCES += identity_session.cpp ../../serial_port/sh7058_identification_session.cpp \
    ../../serial_port/serial_port_actions_direct.cpp ../../serial_port/J2534_win.cpp \
    ../../modules/ecu/subaru_sh7058_preflight.cpp ../../modules/checksum/checksum_ecu_subaru_denso_sh7xxx.cpp
HEADERS += ../../serial_port/serial_port_actions_direct.h ../../serial_port/J2534_win.h \
    ../../serial_port/sh7058_identification_session.h ../../modules/ecu/subaru_sh7058_preflight.h
LIBS += -lsetupapi -lwintrust -lversion -lcfgmgr32 -lcrypt32
