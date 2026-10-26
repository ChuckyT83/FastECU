#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QTextStream>
#include "../../serial_port/j2534_diagnostics.h"
#ifdef Q_OS_WIN
#include "windows_device_api.h"
#endif
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setApplicationName("fastecu-j2534-diagnostics");
    app.setApplicationVersion("006H-diagnostics-1");
    QCommandLineParser parser;parser.addHelpOption();
    parser.addOption({"validate-device","Explicit USB/device-only open/version/close of a listed driver ID","id"});
    parser.addOption({"vehicle-disconnected","Acknowledge adapter is physically disconnected from vehicle/ECU"});
    parser.addOption({"identification-plan","Report the blocked ECU identification capability; performs no driver operations"});
    parser.addOption({"trace","Optional NEW JSONL trace file; review sensitive data before sharing","file"});
    parser.process(app);
    const auto print=[](const QJsonObject &value){QTextStream(stdout)<<QJsonDocument(value).toJson(QJsonDocument::Indented);};
    if(parser.isSet("identification-plan")){print(J2534Diagnostics::identificationPlan());return 2;}
#ifndef Q_OS_WIN
    print({{"ok",false},{"error","Windows registry/device diagnostics require native Windows"},{"validation_level","not_executed"}});return 2;
#else
    QVector<J2534Diagnostics::Driver> drivers;QJsonArray registryErrors;
    for(const auto &view:QVector<QPair<QString,QSettings::Format>>{{"x64",QSettings::Registry64Format},{"x86",QSettings::Registry32Format}})
        for(const QString api:{QString("04.04"),QString("05.00")}) {
            QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\PassThruSupport."+api,view.second);
            drivers+=J2534Diagnostics::readRegistry(registry,api,view.first);
            if(registry.status()!=QSettings::NoError)registryErrors.append(view.first+":"+api+": registry read error");
        }
    if(parser.isSet("validate-device")) {
        if(!parser.isSet("vehicle-disconnected")){print({{"ok",false},{"error","--vehicle-disconnected is required; no driver loaded"}});return 2;}
        for(const auto &driver:drivers) if(driver.id==parser.value("validate-device")) {
            J2534Trace trace;
            if(parser.isSet("trace")) {qWarning("Raw diagnostic traces may contain sensitive data; review before sharing.");trace.start(parser.value("trace"));}
            WindowsDeviceApi api;auto report=J2534Diagnostics::validateDevice(driver,true,true,api,&trace);
            trace.finish();report["trace_failed"]=trace.failed();report["registry_errors"]=registryErrors;print(report);return report["ok"].toBool()?0:1;
        }
        print({{"ok",false},{"error","Selected registered driver ID not found; no driver loaded"}});return 1;
    }
    QJsonArray entries;int compatible=0;
    for(const auto &driver:drivers){entries.append(driver.json());if(driver.api=="04.04"&&driver.issue.isEmpty())++compatible;}
    print({{"validation_level","discovery_only"},{"drivers",entries},{"registry_errors",registryErrors},
        {"compatible_04_04_count",compatible},{"error",compatible?QString():QString("No compatible registered J2534 04.04 DLL found for this process")},
        {"device_opened",false},{"ecu_communication",false},{"note","Discovery reads registry/PE files only. No vendor DLL is loaded."}});
    return !compatible || !registryErrors.isEmpty()?1:0;
#endif
}
