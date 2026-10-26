QT += core testlib
QT -= gui
CONFIG += console testcase c++11
CONFIG -= app_bundle
DEFINES += FASTECU_OFFLINE_KERNEL_TESTS
TARGET = subaru-kernel-offline
SOURCES += kernel_transfer.cpp
HEADERS += experimental_kernel_transfer.h ../../modules/ecu/subaru_sh7058_kernel_layout.h
