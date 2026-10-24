QT += core gui widgets serialport testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = j2534-reliability
SOURCES += reliability.cpp mock_j2534.cpp ../../serial_port/serial_port_actions_direct.cpp
HEADERS += ../../serial_port/serial_port_actions_direct.h ../../serial_port/J2534_linux.h mock_j2534.h
