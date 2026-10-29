QT += core
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = fastecu-j2534-diagnostics
SOURCES += main.cpp
HEADERS += reviewed_openport.h ../../serial_port/j2534_diagnostics.h ../../serial_port/j2534_trace.h passive_metadata.h operation_policy.h report_summary.h
REVISION = $$system(git -C $$shell_quote($$PWD/../..) rev-parse HEAD)
isEmpty(REVISION): REVISION = unrecorded
DEFINES += FASTECU_DIAGNOSTIC_COMMIT=\\\"$$REVISION\\\"
win32 {
    HEADERS += windows_device_api.h
    LIBS += -lwintrust -lversion -lsetupapi -lcfgmgr32 -lcrypt32 -ladvapi32
}

contains(CONFIG, diagnostic_test): DEFINES += FASTECU_DIAGNOSTICS_TEST_BUILD

# Build-time pin for a separately built, source-manifest-checked driver artifact.
# No runtime environment override exists in production builds.
!isEmpty(REVIEWED_OPENPORT_SHA256) {
    !contains(REVIEWED_OPENPORT_SHA256, ^[a-fA-F0-9]{64}$): error(Invalid reviewed driver SHA-256)
    DEFINES += FASTECU_REVIEWED_OPENPORT_SHA256=\\\"$$REVIEWED_OPENPORT_SHA256\\\"
}

DIRTY_TREE = $$system(git -C $$shell_quote($$PWD/../..) status --porcelain)
!isEmpty(DIRTY_TREE): DEFINES += FASTECU_DIAGNOSTIC_DIRTY_TREE
