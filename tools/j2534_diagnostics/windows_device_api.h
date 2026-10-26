#ifndef FASTECU_WINDOWS_DEVICE_ONLY_API_H
#define FASTECU_WINDOWS_DEVICE_ONLY_API_H
#include "../../serial_port/j2534_diagnostics.h"
#include "../../serial_port/J2534_win.h"
class WindowsDeviceApi final : public J2534Diagnostics::DeviceOnlyApi {
    J2534 driver;
public:
    bool load(const QString &path,QString &error) override {
        const auto local=path.toLocal8Bit();
        if(local.size()>=255 || QString::fromLocal8Bit(local)!=path) {error="Existing loader requires a short ANSI-representable path";return false;}
        driver.setDllName(local.constData());
        if(!driver.init()){error=QString::fromLocal8Bit(driver.getLastError());return false;}return true;
    }
    long open(unsigned long &id) override{return driver.PassThruOpen(nullptr,&id);}
    long version(unsigned long id,char *api,char *dll,char *fw) override{return driver.PassThruReadVersion(api,dll,fw,id);}
    long close(unsigned long id) override{return driver.PassThruClose(id);}
    void unload() override{driver.disable();}
};
#endif
