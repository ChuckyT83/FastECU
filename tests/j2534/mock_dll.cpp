// Exports use the production J2534 signatures, including PT_CALL on Windows.
#include "../../serial_port/J2534_tactrix_win.h"
#include <cstring>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <chrono>
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
#include "mock_transport.inc"
static unsigned long device = 41;
static unsigned long channel = 99;
static long failure = 0;
static std::string failingOperation;
static std::string trace;
static int metadata = 0;
static long record(const char *op, unsigned long id) {
    trace += std::string(op) + ":" + std::to_string(id) + ";";
    if(const char *path=std::getenv("FASTECU_MOCK_TRACE_FILE")){if(FILE *out=std::fopen(path,"ab")){std::fprintf(out,"%s:%lu;",op,id);std::fclose(out);}}
    return failingOperation == op ? ERR_FAILED : failure;
}
EXPORT void PT_CALL MockReset() { failure = 0; failingOperation.clear(); trace.clear(); metadata = 0; device = 41; channel = 99; MockTransportReset(); }
EXPORT void PT_CALL MockSetFailure(const char *op) { failingOperation = op; }
EXPORT void PT_CALL MockSetHandles(unsigned long d, unsigned long c) { device = d; channel = c; }
EXPORT void PT_CALL MockSetMetadata(int mode) { metadata = mode; }
EXPORT const char * PT_CALL MockTrace() { return trace.c_str(); }
EXPORT void PT_CALL MockFail(long code) { failure = code; }
EXPORT long PT_CALL PassThruOpen(const void*, unsigned long *id) {
#ifdef MOCK_OPEN_DELAY_MS
    std::this_thread::sleep_for(std::chrono::milliseconds(MOCK_OPEN_DELAY_MS));
#endif
    *id=device;
#ifdef MOCK_OPEN_FAILURE
    failingOperation="open";
#endif
    return record("open",0);
}
EXPORT long PT_CALL PassThruClose(unsigned long id) {
#ifdef MOCK_CLOSE_FAILURE
    failingOperation="close";
#endif
    return id==device?record("close",id):ERR_INVALID_DEVICE_ID;
}
EXPORT long PT_CALL PassThruConnect(unsigned long id, unsigned long, unsigned long, unsigned long, unsigned long *out) { *out = channel; return id == device ? record("connect", id) : ERR_INVALID_DEVICE_ID; }
EXPORT long PT_CALL PassThruDisconnect(unsigned long id) { return id == channel ? record("disconnect", id) : ERR_INVALID_CHANNEL_ID; }
EXPORT long PT_CALL PassThruReadMsgs(unsigned long c, void* m, unsigned long* n, unsigned long t) { return mockRead(c,m,n,t); }
EXPORT long PT_CALL PassThruWriteMsgs(unsigned long c, const void* m, unsigned long* n, unsigned long t) { return mockWrite(c,m,n,t); }
EXPORT long PT_CALL PassThruStartPeriodicMsg(unsigned long c, const void*, unsigned long* id, unsigned long) { return mockPeriodic(c,id); }
EXPORT long PT_CALL PassThruStopPeriodicMsg(unsigned long c, unsigned long) { return mockStop(c); }
EXPORT long PT_CALL PassThruStartMsgFilter(unsigned long id, unsigned long, const void*, const void*, const void*, unsigned long *filter) { *filter = 9; return record("filter", id); }
EXPORT long PT_CALL PassThruStopMsgFilter(unsigned long, unsigned long) { return 0; }
EXPORT long PT_CALL PassThruSetProgrammingVoltage(unsigned long d, unsigned long pin, unsigned long v) { return mockVoltage(d,pin,v); }
#ifndef OMIT_VERSION
EXPORT long PT_CALL PassThruReadVersion(unsigned long id, char *fw, char *dll, char *api) {
    if (id != device) return ERR_INVALID_DEVICE_ID;
    #ifdef MOCK_VERSION_FAILURE
    failingOperation="version";
#endif
    long status = record("version", id);
    if (status) return status;
    if (metadata == 1) return ERR_NOT_SUPPORTED;
    if (metadata == 2) return 0;
    if (metadata >= 3) {
        size_t n = metadata == 3 ? 79 : metadata == 4 ? 80 : 256;
        for (char *p : {api, dll, fw}) memset(p, 'V', n);
        return 0;
    }
    strcpy(fw, "9.8.7"); strcpy(dll, "1.2.3"); strcpy(api, "04.04"); return 0;
}
#endif
EXPORT long PT_CALL PassThruGetLastError(char *text) { strcpy(text, "mock failure"); return 0; }
#ifndef OMIT_IOCTL
EXPORT long PT_CALL PassThruIoctl(unsigned long id, unsigned long op, const void*, void* out) {
    const long result = record(op == SET_CONFIG ? "config" : op == CLEAR_MSG_FILTERS ? "clearFilters" : "ioctl", id);
    return result ? result : mockIoctl(id, op, out);
}
#endif
