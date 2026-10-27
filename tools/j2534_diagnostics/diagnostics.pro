QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = fastecu-j2534-diagnostics
SOURCES += main.cpp
HEADERS += ../../serial_port/j2534_diagnostics.h ../../serial_port/j2534_trace.h passive_metadata.h operation_policy.h report_summary.h
REVISION = $$system(git -C $$shell_quote($$PWD/../..) rev-parse HEAD)
isEmpty(REVISION): REVISION = unrecorded
DEFINES += FASTECU_DIAGNOSTIC_COMMIT=\\\"$$REVISION\\\"
win32 {
    HEADERS += windows_device_api.h
    LIBS += -lwintrust -lversion -lsetupapi -lcfgmgr32 -lcrypt32 -ladvapi32
}

contains(CONFIG, diagnostic_test): DEFINES += FASTECU_DIAGNOSTICS_TEST_BUILD
