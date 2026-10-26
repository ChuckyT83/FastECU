// Included once by serial_port_actions_direct.cpp. The wrappers preserve every
// driver argument/return value; capture never calls another driver operation.
#include "j2534_trace_message.h"
#include <QJsonArray>
#define TRACE_API(name, fields) do { try { if(trace.enabled()) trace_api(name,s,(fields)); } catch (...) {} } while(false)
static QJsonObject traceHandle(unsigned long id, const char *kind) {
    return {{kind,QString::number(id)}};
}
void SerialPortActionsDirect::trace_api(const char *operation,long status,QJsonObject fields) {
    fields["status"]=qint64(status);
    fields["outcome"]=status==STATUS_NOERROR ? "api_success" :
        status==ERR_TIMEOUT ? "timeout" : status==ERR_BUFFER_EMPTY ? "no_message" : "driver_error";
    fields["configured_protocol"]=qint64(protocol); fields["configured_baudrate"]=qint64(baudrate);
    fields["iso15765_source_id"]=QString::number(iso15765_source_address,16);
    fields["iso15765_destination_id"]=QString::number(iso15765_destination_address,16);
    trace.record(operation,fields);
}
long SerialPortActionsDirect::PassThruOpen(const void *name,unsigned long *id) {
    const auto s=j2534->PassThruOpen(name,id);
    TRACE_API("PassThruOpen",s==0 && id ? traceHandle(*id,"device_id") : QJsonObject()); return s;
}
long SerialPortActionsDirect::PassThruClose(unsigned long id) {
    const auto s=j2534->PassThruClose(id);TRACE_API("PassThruClose",traceHandle(id,"device_id"));return s;
}
long SerialPortActionsDirect::PassThruConnect(unsigned long device,unsigned long proto,unsigned long flags,unsigned long baud,unsigned long *id) {
    const auto s=j2534->PassThruConnect(device,proto,flags,baud,id);
    if(trace.enabled()) try {
        auto f=traceHandle(device,"device_id");f["protocol_id"]=qint64(proto);f["flags"]=qint64(flags);f["baudrate"]=qint64(baud);
        if(s==0 && id) f["channel_id"]=QString::number(*id);
        trace_api("PassThruConnect",s,f);
    } catch (...) {} return s;
}
long SerialPortActionsDirect::PassThruDisconnect(unsigned long id) {
    const auto s=j2534->PassThruDisconnect(id);TRACE_API("PassThruDisconnect",traceHandle(id,"channel_id"));return s;
}
long SerialPortActionsDirect::PassThruWriteMsgs(unsigned long id,const PASSTHRU_MSG *msg,unsigned long *count,unsigned long timeout) {
    const unsigned long capacity=count?*count:0;
    QElapsedTimer clock;if(trace.enabled())clock.start();
    const auto s=j2534->PassThruWriteMsgs(id,msg,count,timeout);
    const auto elapsed=clock.isValid()?clock.nsecsElapsed()/1000:0;
    if(trace.enabled()) try {
        auto f=capacity && msg ? j2534TraceMessage(msg,"tx") : QJsonObject();
        f["channel_id"]=QString::number(id);f["timeout_ms"]=qint64(timeout);
        f["requested_count"]=qint64(capacity);f["returned_count"]=qint64(count?*count:0);
        f["elapsed_us"]=elapsed;
        trace_api("PassThruWriteMsgs",s,f);
    } catch (...) {} return s;
}
long SerialPortActionsDirect::PassThruReadMsgs(unsigned long id,PASSTHRU_MSG *msg,unsigned long *count,unsigned long timeout) {
    const unsigned long capacity=count?*count:0;
    QElapsedTimer clock;if(trace.enabled())clock.start();
    const auto s=j2534->PassThruReadMsgs(id,msg,count,timeout);
    const auto elapsed=clock.isValid()?clock.nsecsElapsed()/1000:0;
    if(trace.enabled()) try {
        // Production callers request one initialized slot. Never trust a count
        // larger than its supplied capacity, or iterate beyond that slot.
        auto f=capacity && count && *count && msg ? j2534TraceMessage(msg,"rx") : QJsonObject{{"direction","rx"},{"length",0}};
        f["channel_id"]=QString::number(id);f["timeout_ms"]=qint64(timeout);
        f["requested_count"]=qint64(capacity);f["returned_count"]=qint64(count?*count:0);
        f["elapsed_us"]=elapsed;
        trace_api("PassThruReadMsgs",s,f);
    } catch (...) {} return s;
}
long SerialPortActionsDirect::PassThruStartPeriodicMsg(unsigned long id,const PASSTHRU_MSG *msg,unsigned long *message,unsigned long interval) {
    const auto s=j2534->PassThruStartPeriodicMsg(id,msg,message,interval);
    if(trace.enabled()) try {
        auto f=j2534TraceMessage(msg,"tx_periodic");
        f["channel_id"]=QString::number(id);f["interval_ms"]=qint64(interval);
        trace_api("PassThruStartPeriodicMsg",s,f);
    } catch(...) {}
    return s;
}
long SerialPortActionsDirect::PassThruStopPeriodicMsg(unsigned long id,unsigned long message) {
    const auto s=j2534->PassThruStopPeriodicMsg(id,message);TRACE_API("PassThruStopPeriodicMsg",traceHandle(id,"channel_id"));return s;
}
long SerialPortActionsDirect::PassThruStartMsgFilter(unsigned long id,unsigned long type,const PASSTHRU_MSG *mask,const PASSTHRU_MSG *pattern,const PASSTHRU_MSG *flow,unsigned long *filter) {
    const auto s=j2534->PassThruStartMsgFilter(id,type,mask,pattern,flow,filter);
    if(trace.enabled()) try {
        auto f=traceHandle(id,"channel_id");f["filter_type"]=qint64(type);
        f["mask"]=j2534TraceMessage(mask,"filter");f["pattern"]=j2534TraceMessage(pattern,"filter");
        if(flow)f["flow_control"]=j2534TraceMessage(flow,"filter");
        trace_api("PassThruStartMsgFilter",s,f);
    }catch(...){} return s;
}
long SerialPortActionsDirect::PassThruStopMsgFilter(unsigned long id,unsigned long filter) {
    const auto s=j2534->PassThruStopMsgFilter(id,filter);TRACE_API("PassThruStopMsgFilter",traceHandle(id,"channel_id"));return s;
}
long SerialPortActionsDirect::PassThruSetProgrammingVoltage(unsigned long id,unsigned long pin,unsigned long voltage) {
    const auto s=j2534->PassThruSetProgrammingVoltage(id,pin,voltage);
    TRACE_API("PassThruSetProgrammingVoltage",(QJsonObject{{"device_id",QString::number(id)},{"pin",qint64(pin)},{"voltage_argument",QString::number(voltage)}}));return s;
}
long SerialPortActionsDirect::PassThruReadVersion(char *api,char *dll,char *fw,unsigned long id) {
    const auto s=j2534->PassThruReadVersion(api,dll,fw,id);
    if(trace.enabled())try {
        auto f=traceHandle(id,"device_id");
        if(s==0) { const char *names[]={"api_version","dll_version","firmware_version"};const char *values[]={api,dll,fw};
            for(int i=0;i<3;++i) if(values[i] && memchr(values[i],0,80)) f[names[i]]=QString::fromLatin1(values[i]); }
        trace_api("PassThruReadVersion",s,f);
    }catch(...){} return s;
}
long SerialPortActionsDirect::PassThruGetLastError(char *error) {
    const auto s=j2534->PassThruGetLastError(error);TRACE_API("PassThruGetLastError",QJsonObject());return s; // vendor free text omitted
}
long SerialPortActionsDirect::PassThruIoctl(unsigned long id,unsigned long ioctl,const void *input,void *output) {
    const auto s=j2534->PassThruIoctl(id,ioctl,input,output);
    if(trace.enabled())try {
        auto f=traceHandle(id,"api_handle");f["ioctl_id"]=qint64(ioctl);
        if(ioctl==SET_CONFIG && input) {
            const auto *list=static_cast<const SCONFIG_LIST*>(input);QJsonArray config;
            if(list->ConfigPtr)for(unsigned long i=0;i<list->NumOfParams && i<32;++i)
                config.append(QJsonObject{{"parameter",qint64(list->ConfigPtr[i].Parameter)},{"value",qint64(list->ConfigPtr[i].Value)}});
            f["config"]=config;f["config_count"]=qint64(list->NumOfParams);
        }
        trace_api("PassThruIoctl",s,f);
    }catch(...){} return s;
}
#undef TRACE_API
