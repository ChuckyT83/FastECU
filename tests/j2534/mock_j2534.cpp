// Link-time replacement for the hardware backend. The production connection
// orchestration is compiled unchanged; no serial/adapter/ECU I/O occurs.
#include "../../serial_port/J2534_linux.h"
#include "mock_j2534.h"
#include <cstring>
#define EXPORT extern "C"
#define PT_CALL
#include "mock_transport.inc"
namespace MockJ2534 {
QString fail;
QStringList calls;
int metadata;
long errorTextStatus;
bool loaded;
unsigned long device = 41;
void reset() { MockTransportReset(); fail.clear(); calls.clear(); metadata = 0; errorTextStatus = 0; loaded = false; device = 41; }
long call(QString op, unsigned long id = 0) {
    calls << op + ":" + QString::number(id);
    return fail == op ? ERR_FAILED : STATUS_NOERROR;
}
}
using namespace MockJ2534;
J2534::J2534() {}
J2534::~J2534() { delete serial; }
QString J2534::open_serial_port(QString s) { loaded = true; calls << "transportOpen"; return s; }
void J2534::close_serial_port() { if (loaded) calls << "transportClose"; loaded = false; }
bool J2534::is_serial_port_open() { return loaded; }
long J2534::PassThruOpen(const void*, unsigned long *id) { *id = device; return call("open"); }
long J2534::PassThruClose(unsigned long id) { return call("close", id); }
long J2534::PassThruConnect(unsigned long id, unsigned long, unsigned long, unsigned long, unsigned long *channel) { *channel = 999; return call("connect", id); }
long J2534::PassThruDisconnect(unsigned long id) { return call("disconnect", id); }
long J2534::PassThruIoctl(unsigned long id, unsigned long op, const void*, void* out) {
    QString name = op == SET_CONFIG ? "config" : op == CLEAR_MSG_FILTERS ? "clearFilters" : op == CLEAR_RX_BUFFER ? "clearRx" : op == CLEAR_TX_BUFFER ? "clearTx" : "ioctl";
    const long result = call(name, id);
    return result ? result : mockIoctl(id, op, out);
}
long J2534::PassThruStartMsgFilter(unsigned long id, unsigned long, const PASSTHRU_MSG*, const PASSTHRU_MSG*, const PASSTHRU_MSG*, unsigned long *filter) { *filter = 9; return call("filter", id); }
long J2534::PassThruStopMsgFilter(unsigned long id, unsigned long) { return call("stopFilter", id); }
long J2534::PassThruReadVersion(char *api, char *dll, char *fw, unsigned long id) {
    long result = call("version", id);
    if (result) return result;
    if (metadata == 1) return ERR_NOT_SUPPORTED;
    if (metadata == 2) return 0; // valid empty outputs
    if (metadata >= 3) {
        size_t length = metadata == 3 ? 79 : metadata == 4 ? 80 : 256;
        for (char *p : {api, dll, fw}) memset(p, 'V', length);
    } else { strcpy(api, "04.04"); strcpy(dll, "1.2.3"); strcpy(fw, "9.8.7"); }
    return 0;
}
long J2534::PassThruGetLastError(char *error) { if (!errorTextStatus) strcpy(error, "mock driver failure"); return errorTextStatus; }
long J2534::PassThruReadMsgs(unsigned long id, PASSTHRU_MSG* m, unsigned long *n, unsigned long t) { return mockRead(id,m,n,t); }
long J2534::PassThruWriteMsgs(unsigned long id, const PASSTHRU_MSG* m, unsigned long* n, unsigned long t) { return mockWrite(id,m,n,t); }
long J2534::PassThruStartPeriodicMsg(unsigned long id, const PASSTHRU_MSG*, unsigned long* n, unsigned long) { return mockPeriodic(id,n); }
long J2534::PassThruStopPeriodicMsg(unsigned long id, unsigned long) { return mockStop(id); }
long J2534::PassThruSetProgrammingVoltage(unsigned long id, unsigned long pin, unsigned long v) { return mockVoltage(id,pin,v); }
bool J2534::get_is_tx_done() { return false; }
void J2534::handle_error(QSerialPort::SerialPortError) {}
