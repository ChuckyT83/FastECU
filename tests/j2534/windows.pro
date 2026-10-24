QT += core gui widgets serialport testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = j2534-windows-reliability
SOURCES += windows_reliability.cpp ../../serial_port/serial_port_actions_direct.cpp ../../serial_port/J2534_win.cpp
HEADERS += ../../serial_port/serial_port_actions_direct.h ../../serial_port/J2534_win.h
