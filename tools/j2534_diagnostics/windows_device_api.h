#ifndef FASTECU_WINDOWS_DEVICE_ONLY_API_H
#define FASTECU_WINDOWS_DEVICE_ONLY_API_H
#include "../../serial_port/j2534_diagnostics.h"
#include <QCryptographicHash>
// Only three device-level exports exist here; there is no channel/message,
// filter/IOCTL, voltage, firmware update or ECU programming API.
class WindowsDeviceApi final : public J2534Diagnostics::DeviceOnlyApi {
    HMODULE library=nullptr;
    HANDLE fileLock=INVALID_HANDLE_VALUE;
    QString expectedHash;
    QJsonObject diagnostics_;
    using Open=long(WINAPI *)(const void *,unsigned long *);
    using Close=long(WINAPI *)(unsigned long);
    using Version=long(WINAPI *)(unsigned long,char *,char *,char *);
    Open open_=nullptr;Close close_=nullptr;Version version_=nullptr;
public:
    ~WindowsDeviceApi() override {unload();}
    void pinHash(const QString &hash) {expectedHash=hash;}
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
        library=LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!library){const auto status=GetLastError();diagnostics_["load_windows_status"]=qint64(status);error=QString("LoadLibraryEx failed (Windows status %1)").arg(status);unload();return false;}
        diagnostics_["load_windows_status"]=0;
        open_=reinterpret_cast<Open>(GetProcAddress(library,"PassThruOpen"));
        close_=reinterpret_cast<Close>(GetProcAddress(library,"PassThruClose"));
        version_=reinterpret_cast<Version>(GetProcAddress(library,"PassThruReadVersion"));
        if(!open_ || !close_ || !version_){error="Missing required device-only export (Open, ReadVersion or Close)";unload();return false;}
        return true;
    }
    long open(unsigned long &id) override{return open_(nullptr,&id);}
    long version(unsigned long id,char *api,char *dll,char *fw) override{return version_(id,fw,dll,api);}
    long close(unsigned long id) override{return close_(id);}
    void unload() override {if(library){const auto ok=FreeLibrary(library);diagnostics_["dll_reference_released"]=bool(ok);if(!ok)diagnostics_["release_windows_status"]=qint64(GetLastError());}library=nullptr;open_=nullptr;close_=nullptr;version_=nullptr;if(fileLock!=INVALID_HANDLE_VALUE)CloseHandle(fileLock);fileLock=INVALID_HANDLE_VALUE;}
};
#endif
