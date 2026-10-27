#ifndef FASTECU_PASSIVE_METADATA_H
#define FASTECU_PASSIVE_METADATA_H
#include "../../serial_port/j2534_diagnostics.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#ifdef Q_OS_WIN
#include <setupapi.h>
#include <wintrust.h>
#include <softpub.h>
#include <cfgmgr32.h>
#include <winver.h>
#endif

namespace DiagnosticReport {
inline QJsonObject fileInfo(const QString &path) {
    if(path.startsWith("\\\\") || path.startsWith("//"))return {{"exists",false},{"hash_error","Network/device paths are not inspected; use a local copy"}};
    QFileInfo info(path);
    QJsonObject out{{"exists",info.isFile()},{"size_bytes",QString::number(info.size())},
        {"file_version_status","not_available"},{"signature_status","not_supported_on_this_host"}};
    if(!info.isFile()){out["hash_error"]="Not an accessible regular file";return out;}
    QFile file(path);
    if(info.size()>512ll*1024*1024) out["hash_error"]="File exceeds 512 MiB hashing limit";
    else if(file.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);bool ok=true;
        qint64 read=0;
        while(read<info.size()) {const auto part=file.read(qMin<qint64>(65536,info.size()-read));if(part.isEmpty()){ok=false;break;}hash.addData(part);read+=part.size();}
        if(ok && QFileInfo(path).size()==info.size() && QFileInfo(path).lastModified()==info.lastModified())
            out["sha256"]=QString::fromLatin1(hash.result().toHex());
        else out["hash_error"]="File changed or read failed during hashing";
    } else out["hash_error"]="File missing or inaccessible";
#ifdef Q_OS_WIN
    const auto name=path.toStdWString();DWORD ignored=0;
    const DWORD length=GetFileVersionInfoSizeW(name.c_str(),&ignored);
    if(length && length<=1024*1024) {
        std::vector<char> data(length);
        if(GetFileVersionInfoW(name.c_str(),0,length,data.data())) {
            VS_FIXEDFILEINFO *fixed=nullptr;UINT count=0;
            if(VerQueryValueW(data.data(),L"\\",reinterpret_cast<void **>(&fixed),&count) &&
                count>=sizeof(*fixed) && fixed->dwSignature==0xfeef04bd) {
                out["file_version_status"]="available";
                out["file_version"]=QString("%1.%2.%3.%4").arg(HIWORD(fixed->dwFileVersionMS)).arg(LOWORD(fixed->dwFileVersionMS)).arg(HIWORD(fixed->dwFileVersionLS)).arg(LOWORD(fixed->dwFileVersionLS));
            }
        }
    }
    WINTRUST_FILE_INFO target={};target.cbStruct=sizeof(target);target.pcwszFilePath=name.c_str();
    WINTRUST_DATA trust={};trust.cbStruct=sizeof(trust);trust.dwUIChoice=WTD_UI_NONE;
    trust.fdwRevocationChecks=WTD_REVOKE_NONE;trust.dwUnionChoice=WTD_CHOICE_FILE;trust.pFile=&target;
    trust.dwStateAction=WTD_STATEACTION_VERIFY;
    trust.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL|WTD_REVOCATION_CHECK_NONE;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status=WinVerifyTrust(nullptr,&action,&trust);
    out["signature_windows_status"]=qint64(status);
    out["signature_status"]=status==0?"offline_policy_valid":status==TRUST_E_NOSIGNATURE?"no_embedded_signature":"not_validated_by_offline_policy";
    out["signature_policy"]="Offline cached trust only; no network or revocation check. Not hardware/software compatibility proof.";
    trust.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&trust);
#endif
    return out;
}

#ifdef Q_OS_WIN
inline QStringList deviceProperty(HDEVINFO set,SP_DEVINFO_DATA &device,DWORD property) {
    DWORD bytes=0,type=0;
    SetupDiGetDeviceRegistryPropertyW(set,&device,property,&type,nullptr,0,&bytes);
    if(!bytes || bytes>65536 || bytes%sizeof(wchar_t)) return {};
    std::vector<wchar_t> data(bytes/sizeof(wchar_t)+2,0);
    if(!SetupDiGetDeviceRegistryPropertyW(set,&device,property,&type,reinterpret_cast<PBYTE>(data.data()),bytes,nullptr))return {};
    if(type!=REG_SZ && type!=REG_MULTI_SZ)return {};
    QStringList out;
    for(size_t at=0;at<data.size() && data[at];) {
        const auto value=QString::fromWCharArray(data.data()+at);out<<value;
        if(type==REG_SZ)break;at+=size_t(value.size())+1;
    }
    return out;
}
inline QString registryText(HKEY key,const wchar_t *name) {
    DWORD bytes=0,type=0;
    if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes)!=ERROR_SUCCESS ||
        type!=REG_SZ || bytes>65536 || bytes%sizeof(wchar_t))return {};
    std::vector<wchar_t> data(bytes/sizeof(wchar_t)+1,0);
    if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<PBYTE>(data.data()),&bytes)!=ERROR_SUCCESS)return {};
    return QString::fromWCharArray(data.data());
}
#endif
inline QJsonObject usbInfo(bool all) {
    QJsonObject out{{"status","not_supported_on_this_host"},{"scope",all?"all_present_USB_devices":"adapter_description_candidates"},
        {"identity_note","Descriptions/USB IDs are observations, not authentication of adapter model or clone identity."}};
    QJsonArray records;
#ifdef Q_OS_WIN
    HDEVINFO set=SetupDiGetClassDevsW(nullptr,L"USB",nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(set==INVALID_HANDLE_VALUE){out["status"]="enumeration_failed";out["windows_status"]=qint64(GetLastError());return out;}
    out["status"]="complete";
    for(DWORD i=0;i<512;++i) {
        SP_DEVINFO_DATA device={};device.cbSize=sizeof(device);
        if(!SetupDiEnumDeviceInfo(set,i,&device)) {
            if(GetLastError()!=ERROR_NO_MORE_ITEMS){out["status"]="enumeration_failed";out["windows_status"]=qint64(GetLastError());}
            break;
        }
        if(i==511)out["status"]="device_limit_reached";
        const auto description=deviceProperty(set,device,SPDRP_DEVICEDESC).value(0);
        const auto manufacturer=deviceProperty(set,device,SPDRP_MFG).value(0);
        const auto text=(description+" "+manufacturer).toLower();
        if(!all && !text.contains("obdx") && !text.contains("tactrix") && !text.contains("openport") && !text.contains("j2534"))continue;
        QJsonObject entry{{"description",description},{"manufacturer",manufacturer}};
        QJsonArray ids;for(const auto &id:deviceProperty(set,device,SPDRP_HARDWAREID))ids.append(id);
        entry["hardware_ids"]=ids;
        wchar_t instance[2048]={};
        if(SetupDiGetDeviceInstanceIdW(set,&device,instance,2048,nullptr))entry["device_instance_id"]=QString::fromWCharArray(instance);
        ULONG status=0,problem=0;
        const auto result=CM_Get_DevNode_Status(&status,&problem,device.DevInst,0);
        entry["pnp_query_status"]=int(result);
        if(result==CR_SUCCESS){entry["pnp_status_flags"]=qint64(status);entry["pnp_problem_code"]=qint64(problem);}
        entry["service"]=deviceProperty(set,device,SPDRP_SERVICE).value(0);
        HKEY driver=SetupDiOpenDevRegKey(set,&device,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);
        if(driver!=INVALID_HANDLE_VALUE){entry["driver_provider"]=registryText(driver,L"ProviderName");entry["driver_version"]=registryText(driver,L"DriverVersion");RegCloseKey(driver);}
        records.append(entry);
    }
    SetupDiDestroyDeviceInfoList(set);
#endif
    out["devices"]=records;
    return out;
}

inline QString privatePath(QString value) {
    // Preserve a filename for diagnostics, never an arbitrary user's directory.
    value.replace('\\','/');return "<redacted-directory>/"+value.section('/',-1);
}
inline QJsonValue redact(const QJsonValue &value,bool includePrivate,const QString &key=QString()) {
    if(includePrivate)return value;
    if(value.isArray()){QJsonArray a;for(const auto &v:value.toArray())a.append(redact(v,false,key));return a;}
    if(value.isObject()){QJsonObject o;const auto source=value.toObject();for(auto i=source.begin();i!=source.end();++i)o[i.key()]=redact(i.value(),false,i.key());return o;}
    if(value.isString()) {
        auto text=value.toString();
        if(key.contains("path",Qt::CaseInsensitive) || key=="file")return privatePath(text);
        if(key=="device_instance_id"){text=text.section('\\',0,-2)+"\\<redacted-instance>";return text;}
        const auto home=QDir::homePath();if(!home.isEmpty())text.replace(home,"<user-home>",Qt::CaseInsensitive);
        auto native=QDir::toNativeSeparators(home);if(!native.isEmpty())text.replace(native,"<user-home>",Qt::CaseInsensitive);
        return text;
    }
    return value;
}
inline QJsonObject base(const QString &commit) {
    return {{"schema","fastecu.device-diagnostics.v2"},{"tool_version","006P-1"},{"development_commit",commit},
        {"timestamp_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"windows_product",QSysInfo::prettyProductName()},{"kernel_version",QSysInfo::kernelVersion()},
        {"process_architecture",QSysInfo::buildCpuArchitecture()},{"host_architecture",QSysInfo::currentCpuArchitecture()},
        {"filesystem_view","Process-native Windows/WOW64 file resolution. Registry view labels identify registry origin, not DLL bitness."},
        {"statement","Device-only J2534 diagnostics; no vehicle communication performed."},
        {"device_opened",false},{"ecu_communication",false},{"programming_available",false},
        {"privacy_note","Paths and device-instance suffixes redacted by default. Review arbitrary vendor descriptions/version strings before sharing."}};
}
}
#endif
