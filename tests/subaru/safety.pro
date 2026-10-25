QT += core gui widgets xml serialport testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = subaru-safety
INCLUDEPATH += stubs ../.. ../../modules/ecu
SOURCES += safety.cpp ../../modules/ecu/flash_ecu_subaru_denso_sh7058_can.cpp \
    ../../modules/checksum/checksum_ecu_subaru_denso_sh7xxx.cpp
HEADERS += ../../modules/ecu/flash_ecu_subaru_denso_sh7058_can.h \
    ../../modules/ecu/subaru_sh7058_safety.h
FORMS += ../../ecu_operations.ui
