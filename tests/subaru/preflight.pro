QT += core gui widgets xml testlib
CONFIG += console testcase c++11
CONFIG -= app_bundle
TARGET = subaru-preflight
INCLUDEPATH += ../..
SOURCES += preflight.cpp ../../modules/ecu/subaru_sh7058_preflight.cpp ../../modules/checksum/checksum_ecu_subaru_denso_sh7xxx.cpp
HEADERS += ../../modules/ecu/subaru_sh7058_preflight.h
