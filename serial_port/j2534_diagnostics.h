#ifndef FASTECU_J2534_DIAGNOSTICS_H
#define FASTECU_J2534_DIAGNOSTICS_H
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QSysInfo>
#include <QElapsedTimer>
#include <QVector>
#include <cstring>
#include <vector>
#include "j2534_trace.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace J2534Diagnostics {
struct Driver {
    QString id,description,path,registeredPath,api,view,manufacturer,registeredVersion;
    bool isDll=false;
    QString issue;
    unsigned machine=0;
    QJsonObject json() const {
        return {{"id",id},{"description",description},{"dll_path",path},{"registered_dll_path",registeredPath},
            {"process_architecture",QSysInfo::buildCpuArchitecture()},
            {"registered_api",api},{"registry_view",view},{"manufacturer",manufacturer},
            {"registered_version",registeredVersion},{"machine",int(machine)},
            {"architecture_from_header",machine==0x8664?"x64":machine==0x14c?"x86":machine==0xaa64?"arm64":"unknown"},
            {"is_dll",isDll},{"issue",issue}};
    }
};
inline unsigned nativeMachine() {
    const auto cpu=QSysInfo::buildCpuArchitecture();
    return cpu=="x86_64" ? 0x8664 : cpu=="i386" ? 0x14c : cpu=="arm64" ? 0xaa64 : 0;
}
inline QString normalizedPath(QString path) {
    path=path.trimmed();
    if(path.startsWith('"') && path.endsWith('"') && path.size()>=2) path=path.mid(1,path.size()-2);
#ifdef Q_OS_WIN
    const auto native=path.toStdWString();
    const DWORD count=ExpandEnvironmentStringsW(native.c_str(),nullptr,0);
    if(count && count<32768) {
        std::vector<wchar_t> output(count);
        if(ExpandEnvironmentStringsW(native.c_str(),output.data(),count)==count) path=QString::fromWCharArray(output.data());
    }
#endif
    return path;
}
inline void inspect(Driver &driver,unsigned expected=nativeMachine(),bool requireDll=true) {
    driver.machine=0;driver.isDll=false;driver.issue.clear();
    if(driver.path.startsWith("\\\\") || driver.path.startsWith("//")) {driver.issue="Network/device-namespace paths are not inspected or loaded; copy the reviewed file to a local drive";return;}
    if(driver.path.isEmpty() || driver.path.contains(QChar('\0')) || !QDir::isAbsolutePath(driver.path)) {driver.issue="Missing or invalid absolute FunctionLibrary path";return;}
    QFile file(driver.path);
    if(!QFileInfo(driver.path).isFile() || !file.open(QIODevice::ReadOnly)) {driver.issue="Driver DLL missing or inaccessible";return;}
    const auto dos=file.read(64);
    if(dos.size()!=64 || dos.left(2)!="MZ") {driver.issue="Not a Windows PE DLL";return;}
    const auto le16=[](const QByteArray &b,int i){return unsigned(uint8_t(b[i])) | (unsigned(uint8_t(b[i+1]))<<8);};
    quint32 offset=0;for(int i=0;i<4;++i) offset|=quint32(uint8_t(dos[60+i]))<<(8*i);
    if(offset<64 || quint64(offset)+26>quint64(file.size()) || !file.seek(offset)) {driver.issue="Malformed PE header offset";return;}
    const auto pe=file.read(26);
    if(pe.size()!=26 || pe.left(4)!=QByteArray("PE\0\0",4)) {driver.issue="Malformed PE signature";return;}
    driver.isDll=(le16(pe,22)&0x2000)!=0;
    if(requireDll && !driver.isDll) {driver.issue="PE image is not a DLL";return;}
    driver.machine=le16(pe,4);
    const auto magic=le16(pe,24);
    const unsigned optional=le16(pe,20),sections=le16(pe,6);
    const unsigned minimum=magic==0x10b?96:magic==0x20b?112:0;
    if(!minimum || optional<minimum || optional>4096 || !sections || sections>96 ||
        quint64(offset)+24+optional+quint64(sections)*40>quint64(file.size())) {
        driver.issue="Malformed or truncated PE optional/section headers";return;
    }
    if((driver.machine==0x14c && magic!=0x10b) ||
       ((driver.machine==0x8664 || driver.machine==0xaa64) && magic!=0x20b)) {driver.issue="Inconsistent PE architecture";return;}
    if(driver.machine!=0x14c && driver.machine!=0x8664 && driver.machine!=0xaa64) {driver.issue="Unsupported PE architecture";return;}
    if(!expected || driver.machine!=expected) driver.issue="DLL architecture does not match this process";
}
inline QVector<Driver> readRegistry(QSettings &settings,const QString &api,const QString &view) {
    QVector<Driver> out;
    for(const auto &key:settings.childGroups()) {
        Driver driver;driver.id=view+":"+api+":"+key;driver.api=api;driver.view=view;
        const auto name=settings.value(key+"/Name");
        driver.description=name.type()==QVariant::String && !name.toString().isEmpty()?name.toString():key;
        driver.manufacturer=settings.value(key+"/Vendor").toString();
        driver.registeredVersion=settings.value(key+"/APIVersion").toString();
        const auto value=settings.value(key+"/FunctionLibrary");
        driver.registeredPath=value.type()==QVariant::String?value.toString():QString();
        driver.path=normalizedPath(driver.registeredPath);
        inspect(driver);out.append(driver); // retain malformed/missing entries for diagnosis
    }
    return out;
}

// Capability boundary: this interface deliberately cannot connect channels,
// install filters, transmit, read ECU messages, change voltage, reset or flash.
class DeviceOnlyApi {
public:
    virtual ~DeviceOnlyApi() = default;
    virtual bool load(const QString &path,QString &error)=0;
    virtual long open(unsigned long &device)=0;
    virtual long version(unsigned long device,char *api,char *dll,char *firmware)=0;
    virtual long close(unsigned long device)=0;
    virtual void unload()=0;
    virtual QJsonObject loaderDiagnostics() const {return {};}
};
inline QJsonObject validateDevice(Driver driver,bool explicitRequest,bool vehicleDisconnected,DeviceOnlyApi &api,J2534Trace *trace=nullptr) {
    QJsonObject report{{"driver",driver.json()},{"validation_level","discovery_only"},{"ok",false},
        {"ecu_communication",false},{"channel_opened",false}};
    if(!explicitRequest || !vehicleDisconnected) {report["error"]="Explicit device validation and vehicle-disconnected acknowledgement required";return report;}
    inspect(driver);report["driver"]=driver.json();
    if(!driver.issue.isEmpty()) {report["error"]=driver.issue;return report;}
    if(driver.api!="04.04") {report["error"]="Only the reviewed J2534 04.04 device API is supported";return report;}
    report["validation_level"]="device_validation_failed";
    QString error;
    QElapsedTimer loadClock;loadClock.start();
    const bool loaded=api.load(driver.path,error);
    report["load_elapsed_ms"]=loadClock.elapsed();report["dll_loaded"]=loaded;
    if(trace)trace->record("dll_load",{{"loaded",loaded},{"validation_level","device_only"}});
    if(!loaded) {report["error"]=error;api.unload();report["loader"]=api.loaderDiagnostics();if(trace)trace->record("dll_unload");return report;}
    unsigned long id=0;
    QElapsedTimer elapsed;elapsed.start();
    const long opened=api.open(id);report["open_elapsed_ms"]=elapsed.elapsed();report["open_status"]=qint64(opened);
    if(trace)trace->record("PassThruOpen",{{"status",qint64(opened)},{"validation_level","device_only"}});
    if(opened!=0) {report["error"]="PassThruOpen failed";api.unload();report["loader"]=api.loaderDiagnostics();if(trace)trace->record("dll_unload");return report;}
    report["device_handle"]=QString::number(id);
    char apiVersion[256]={},dll[256]={},firmware[256]={};
    elapsed.restart();
    const long version=api.version(id,apiVersion,dll,firmware);report["version_elapsed_ms"]=elapsed.elapsed();report["version_status"]=qint64(version);
    bool metadataValid=version==0;
    if(version==0) {
        const char *names[]={"api_version","dll_version","firmware_version"};const char *values[]={apiVersion,dll,firmware};
        for(int i=0;i<3;++i) {
            if(std::memchr(values[i],0,80)) {
                report[names[i]]=QString::fromLatin1(values[i]);
                if(!values[i][0]){metadataValid=false;report["metadata_warning"]="Driver version response is empty";}
            }
            else {metadataValid=false;report["metadata_warning"]="Driver version response is unterminated";}
        }
    } else report["metadata_warning"]="PassThruReadVersion unsupported or failed; device open is distinct from metadata";
    if(trace)trace->record("PassThruReadVersion",{{"status",qint64(version)},
        {"api_version",report["api_version"]},{"dll_version",report["dll_version"]},{"firmware_version",report["firmware_version"]}});
    if(metadataValid && report["api_version"].toString()!=driver.api) {
        metadataValid=false;report["metadata_warning"]="Reported API version differs from registered 04.04; review required";
    }
    report["version_ok"]=metadataValid;
    elapsed.restart();
    const long closed=api.close(id);report["close_elapsed_ms"]=elapsed.elapsed();report["close_status"]=qint64(closed);
    if(trace)trace->record("PassThruClose",{{"status",qint64(closed)},{"validation_level","device_only"}});
    api.unload();if(trace)trace->record("dll_unload");report["cleanup_confirmed"]=closed==0;
    report["loader"]=api.loaderDiagnostics();
    if(closed!=0) {report["error"]="PassThruClose failed; device state/cleanup unconfirmed";return report;}
    if(report["loader"].toObject().contains("dll_reference_released") && !report["loader"].toObject()["dll_reference_released"].toBool()) {report["error"]="Driver DLL reference release failed";return report;}
    report["ok"]=true;report["validation_level"]="device_open_close_only";
    report["compatibility_claim"]="No channel, ECU, ISO15765 or flashing compatibility validated";
    return report;
}
inline QJsonObject identificationPlan() {
    return {{"ok",false},{"validation_level","not_executed"},{"ecu_communication",false},
        {"error","This device-only toolkit never performs ECU identification. Use the separately reviewed FastECU identity workflow only after physical-test approval; no adapter/channel opened"},
        {"programming_available",false},{"allowed_requests",QJsonArray()}};
}
}
#endif
