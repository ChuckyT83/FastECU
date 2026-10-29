#ifndef FASTECU_WINDOWS_DEVICE_ONLY_API_H
#define FASTECU_WINDOWS_DEVICE_ONLY_API_H
#include "../../serial_port/j2534_diagnostics.h"
#include <QCryptographicHash>
#include "reviewed_openport.h"
// Only three device-level exports exist here; there is no channel/message,
// filter/IOCTL, voltage, firmware update or ECU programming API.
class WindowsDeviceApi final : public J2534Diagnostics::DeviceOnlyApi {
    HMODULE library=nullptr;
    HANDLE fileLock=INVALID_HANDLE_VALUE;
    QString expectedHash;
    QJsonObject diagnostics_;
    HANDLE dependencyLock=INVALID_HANDLE_VALUE;
    HANDLE ownershipMutex=nullptr;
    HMODULE dependencyLibrary=nullptr;
    bool custom=false, mutexOwned=false, logConfigured=false, previousLogSet=false;
    QString lifecycleLog;
    QByteArray previousLog;
    using Open=long(WINAPI *)(const void *,unsigned long *);
    using Close=long(WINAPI *)(unsigned long);
    using Version=long(WINAPI *)(unsigned long,char *,char *,char *);
    using LastError=long(WINAPI *)(char *);
    LastError lastError_=nullptr;
    Open open_=nullptr;Close close_=nullptr;Version version_=nullptr;
public:
    ~WindowsDeviceApi() override {unload();}
    void pinHash(const QString &hash) {expectedHash=hash;}
    void qualifyCustomOpenPort(const QString &log) {custom=true;lifecycleLog=log;}
    bool reviewedOpenPort() const override {return custom;}
    QJsonObject lifecycleEvidence() const override {
        QFile file(lifecycleLog);
        if(!custom || !file.open(QIODevice::ReadOnly) || file.size()>65536)return {};
        return DiagnosticReport::openPortLifecycleEvidence(file.readAll());
    }
    QString failureDetail() override {
        if(!custom || !lastError_)return {};
        char text[80]={};const auto status=lastError_(text);
        diagnostics_["get_last_error_status"]=qint64(status);
        diagnostics_["get_last_error_calls"]=diagnostics_["get_last_error_calls"].toInt()+1;
        text[79]=0;
        return status==0?QString::fromLocal8Bit(text):QString("GetLastError failed (%1)").arg(status);
    }
    QJsonObject loaderDiagnostics() const override {return diagnostics_;}
    bool load(const QString &path,QString &error) override {
        unload();
        diagnostics_={};
        // Deny replacement/writes while checking and loading the selected file.
        fileLock=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(fileLock==INVALID_HANDLE_VALUE){const auto status=GetLastError();diagnostics_["file_lock_windows_status"]=qint64(status);error=QString("Unable to lock driver file (Windows status %1)").arg(status);return false;}
        if(!expectedHash.isEmpty()) {
            LARGE_INTEGER size={};QCryptographicHash hash(QCryptographicHash::Sha256);
            bool ok=GetFileSizeEx(fileLock,&size) && size.QuadPart>=0 && size.QuadPart<=512ll*1024*1024;
            char data[65536];DWORD count=0;
            while(ok){if(!ReadFile(fileLock,data,sizeof(data),&count,nullptr)){ok=false;break;}if(!count)break;hash.addData(data,int(count));}
            if(!ok || QString::fromLatin1(hash.result().toHex()).compare(expectedHash,Qt::CaseInsensitive)!=0){error="Pinned driver hash mismatch while file locked; no DLL loaded";unload();return false;}
        }
        if(custom) {
            if(expectedHash.compare(DiagnosticReport::reviewedOpenPortHash(),Qt::CaseInsensitive)!=0) {
                error="Unreviewed custom DLL hash; no library loaded";unload();return false;
            }
            ownershipMutex=CreateMutexW(nullptr,FALSE,L"Local\\FastECU-ReviewedCustomOpenPort-USBOnly-v1");
            if(!ownershipMutex){error="Cannot create exclusive qualification ownership";unload();return false;}
            const auto wait=WaitForSingleObject(ownershipMutex,0);
            mutexOwned=wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED;
            if(wait!=WAIT_OBJECT_0){error="Another qualification owns the adapter, or prior ownership is uncertain; no DLL loaded";unload();return false;}
            const auto dependency=QFileInfo(path).dir().absoluteFilePath("libusb-1.0.dll");
            dependencyLock=CreateFileW(reinterpret_cast<LPCWSTR>(dependency.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(dependencyLock==INVALID_HANDLE_VALUE){error="Cannot lock reviewed libusb dependency";unload();return false;}
            QCryptographicHash depHash(QCryptographicHash::Sha256);char data[65536];DWORD count=0;
            LARGE_INTEGER length={};bool ok=GetFileSizeEx(dependencyLock,&length) && length.QuadPart>0 && length.QuadPart<=512ll*1024*1024;
            while(ok){if(!ReadFile(dependencyLock,data,sizeof(data),&count,nullptr)){ok=false;break;}if(!count)break;depHash.addData(data,int(count));}
            if(!ok || QString::fromLatin1(depHash.result().toHex()).compare(DiagnosticReport::reviewedLibusbHash(),Qt::CaseInsensitive)!=0) {
                error="Reviewed libusb hash mismatch while locked; no library loaded";unload();return false;
            }
            if(GetModuleHandleW(L"libusb-1.0.dll")){error="Preloaded libusb dependency refused in qualification worker";unload();return false;}
            QFile log(lifecycleLog);
            if(!log.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error="Lifecycle evidence destination unavailable or already exists";unload();return false;}
            log.close();
            previousLogSet=qEnvironmentVariableIsSet("LOG_ENABLE");previousLog=qgetenv("LOG_ENABLE");
            logConfigured=qputenv("LOG_ENABLE",QFile::encodeName(lifecycleLog));
            if(!logConfigured){error="Unable to configure driver lifecycle evidence";unload();return false;}
            dependencyLibrary=LoadLibraryExW(reinterpret_cast<LPCWSTR>(dependency.utf16()),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!dependencyLibrary){diagnostics_["dependency_load_windows_status"]=qint64(GetLastError());error="Reviewed libusb dependency load failed";unload();return false;}
            diagnostics_["dependency_sha256"]=DiagnosticReport::reviewedLibusbHash();
            diagnostics_["exclusive_worker_ownership"]=true;
        }
        library=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,
            custom?LOAD_LIBRARY_SEARCH_SYSTEM32:(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
        if(!library){const auto status=GetLastError();diagnostics_["load_windows_status"]=qint64(status);error=QString("LoadLibraryEx failed (Windows status %1)").arg(status);unload();return false;}
        diagnostics_["load_windows_status"]=0;
        open_=reinterpret_cast<Open>(GetProcAddress(library,"PassThruOpen"));
        close_=reinterpret_cast<Close>(GetProcAddress(library,"PassThruClose"));
        version_=reinterpret_cast<Version>(GetProcAddress(library,"PassThruReadVersion"));
        if(custom)lastError_=reinterpret_cast<LastError>(GetProcAddress(library,"PassThruGetLastError"));
        if(!open_ || !close_ || !version_){error="Missing required device-only export (Open, ReadVersion or Close)";unload();return false;}
        if(custom && !lastError_){error="Reviewed custom driver missing GetLastError";unload();return false;}
        return true;
    }
    long open(unsigned long &id) override{return open_(nullptr,&id);}
    long version(unsigned long id,char *api,char *dll,char *fw) override{return version_(id,fw,dll,api);}
    long close(unsigned long id) override{return close_(id);}
    void unload() override {
        if(library){const auto ok=FreeLibrary(library);diagnostics_["dll_reference_released"]=bool(ok);if(!ok)diagnostics_["release_windows_status"]=qint64(GetLastError());}
        library=nullptr;open_=nullptr;close_=nullptr;version_=nullptr;lastError_=nullptr;
        if(dependencyLibrary){diagnostics_["dependency_reference_released"]=bool(FreeLibrary(dependencyLibrary));dependencyLibrary=nullptr;}
        if(fileLock!=INVALID_HANDLE_VALUE){CloseHandle(fileLock);}
        fileLock=INVALID_HANDLE_VALUE;
        if(dependencyLock!=INVALID_HANDLE_VALUE){CloseHandle(dependencyLock);}
        dependencyLock=INVALID_HANDLE_VALUE;
        if(logConfigured){if(previousLogSet)qputenv("LOG_ENABLE",previousLog);else qunsetenv("LOG_ENABLE");logConfigured=false;}
        if(mutexOwned){ReleaseMutex(ownershipMutex);}
        mutexOwned=false;
        if(ownershipMutex){CloseHandle(ownershipMutex);}
        ownershipMutex=nullptr;
    }
};
#endif
