QT += core gui widgets testlib
CONFIG += console testcase c++11
TARGET = adapter-gui-tests
SOURCES += adapter_gui.cpp ../../gui/j2534_adapter_manager.cpp
HEADERS += ../../gui/j2534_adapter_manager.h
win32:LIBS += -lwintrust -lversion -lsetupapi -lcfgmgr32 -lcrypt32 -ladvapi32
