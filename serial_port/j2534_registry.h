#ifndef FASTECU_J2534_REGISTRY_H
#define FASTECU_J2534_REGISTRY_H
#include "j2534_diagnostics.h"
namespace J2534Diagnostics {
inline QVector<Driver> registeredDrivers(QJsonArray &errors) {
    QVector<Driver> out;
#ifdef Q_OS_WIN
    for(const auto &view:QVector<QPair<QString,QSettings::Format>>{{"x64",QSettings::Registry64Format},{"x86",QSettings::Registry32Format}})
        for(const QString api:{QString("04.04"),QString("05.00")}) {
            QString root="HKEY_LOCAL_MACHINE\\SOFTWARE\\PassThruSupport."+api;
#ifdef FASTECU_DIAGNOSTICS_TEST_BUILD
            const auto test=qEnvironmentVariable("FASTECU_DIAGNOSTICS_TEST_REGISTRY");
            if(test.startsWith("HKEY_CURRENT_USER\\Software\\FastECU-Diagnostics-Test-"))root=test+"\\"+view.first+"-"+api;
#endif
            QSettings registry(root,view.second);
            out+=readRegistry(registry,api,view.first);
            if(registry.status()!=QSettings::NoError)errors.append(view.first+":"+api+": registry read error");
        }
#else
    errors.append("Windows registry/device diagnostics require native Windows");
#endif
    return out;
}
}
#endif
