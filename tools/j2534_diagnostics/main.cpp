#include "../../serial_port/j2534_registry.h"
#include "report_summary.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QTextStream>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QRegularExpression>
#include "operation_policy.h"
#include "reviewed_openport.h"
#include "../../serial_port/j2534_adapter_selection.h"
#ifdef Q_OS_WIN
#include "windows_device_api.h"
#endif
#ifndef FASTECU_DIAGNOSTIC_COMMIT
#define FASTECU_DIAGNOSTIC_COMMIT "unrecorded"
#endif
using namespace J2534Diagnostics;
using namespace DiagnosticReport;

static bool writeNewJson(const QString &path,const QJsonObject &object) {
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return false;
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.write(bytes)==bytes.size() && file.flush();
}
static QJsonObject readJson(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024)return {};
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    return error.error==QJsonParseError::NoError?doc.object():QJsonObject();
}
static QString jobDirectory(const QString &token) {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("fastecu-device-"+token);
}
static QJsonObject isolatedDevice(const QString &driverId,const QString &kind,const QString &hash,
                                  const QString &trace,int timeout,bool custom=false,bool manager=false) {
    const auto token=QUuid::createUuid().toString(QUuid::Id128);
    const auto directory=jobDirectory(token);
    QJsonObject report{{"ok",false},{"validation_level","device_call_not_started"}};
    if(!QDir().mkdir(directory)){report["error"]="Unable to create private worker output directory";return report;}
    QProcess worker;
    worker.setProgram(QCoreApplication::applicationFilePath());
    QStringList arguments{"--internal-worker",token,manager?"--adapter-manager-test":custom?"--qualify-openport-usb-only":"--validate-device",driverId,"--adapter-kind",kind,
        "--driver-sha256",hash,"--vehicle-disconnected","--reviewed-open-behavior"};
    if(manager)arguments=QStringList{"--internal-worker",token,"--adapter-manager-test",driverId,"--driver-sha256",hash};
    if(!trace.isEmpty())arguments<<"--trace"<<trace;
    worker.setArguments(arguments);
    worker.setStandardOutputFile(QDir(directory).filePath("worker-output.txt"));
    worker.setStandardErrorFile(QDir(directory).filePath("worker-errors.txt"));
    qint64 pid=0;
    if(!worker.startDetached(&pid)){report["error"]="Unable to start isolated device worker";return report;}
    report["worker_pid"]=QString::number(pid);report["worker_result_token"]=token;
    QElapsedTimer elapsed;elapsed.start();
    const auto resultPath=QDir(directory).filePath("result.json");
    while(elapsed.elapsed()<timeout) {
        if(QFileInfo::exists(resultPath)) {
            auto result=readJson(resultPath);
            if(result.isEmpty()){report["error"]="Worker returned malformed diagnostic report; device state uncertain";return report;}
            result["isolated_process"]=true;result["parent_wait_ms"]=elapsed.elapsed();return result;
        }
#ifdef Q_OS_WIN
        HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));
        if(process) {
            const bool exited=WaitForSingleObject(process,0)==WAIT_OBJECT_0;DWORD status=0;const bool known=GetExitCodeProcess(process,&status);CloseHandle(process);
            if(exited && !QFileInfo::exists(resultPath)){if(known)report["worker_exit_status"]=qint64(status);report["device_opened"]=QJsonValue();report["error"]="Worker exited without a result; device state/cleanup uncertain";return report;}
        }
#endif
        QThread::msleep(20);
    }
    writeNewJson(QDir(directory).filePath("parent-timeout.json"),{{"timed_out",true}});
    report["validation_level"]="device_call_pending_or_uncertain";
    report["error"]="Parent wait expired. Worker was NOT terminated and no concurrent close/unload was attempted. Stop; do not open again or unplug during an uncertain call.";
    report["worker_terminated"]=false;report["cleanup_confirmed"]=false;
    report["device_opened"]=QJsonValue();
    report["parent_wait_ms"]=elapsed.elapsed();return report;
}

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setApplicationName("fastecu-j2534-diagnostics");app.setApplicationVersion("006P-1+usbqual.1");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"qualify-openport-usb-only","Reviewed hash-pinned custom x64 OpenPort USB lifecycle only (no channels/ECU)","absolute-dll"});
    parser.addOption({"validate-device","Explicit isolated USB-only open/version/close; reviewed OBDX driver only","id"});
    parser.addOption({"adapter-manager-list","Internal GUI adapter discovery"});
    parser.addOption({"adapter-manager-test","Internal GUI USB lifecycle test", "path"});
    parser.addOption({"adapter-kind","obdx-pro-vx, reviewed-custom-openport (separate qualification mode), openport-clone, or unreviewed","kind","unreviewed"});
    parser.addOption({"vehicle-disconnected","Acknowledge adapter is physically disconnected from vehicle/ECU"});
    parser.addOption({"reviewed-open-behavior","Owner affirms independent review of this driver's open/firmware behavior; not verified by the tool"});
    parser.addOption({"driver-sha256","Exact reviewed driver hash; selection integrity, not compatibility proof","sha256"});
    parser.addOption({"output","Write a NEW structured JSON report; existing files are never overwritten","file"});
    parser.addOption({"summary","Write a NEW readable text summary alongside JSON; same privacy settings","file"});
    parser.addOption({"inspect-file","Passively inspect an installer/DLL without executing it","file"});
    parser.addOption({"usb-all","Include all present USB devices instead of adapter-description candidates; instance suffixes remain redacted"});
    parser.addOption({"include-private-details","Explicitly include full paths and USB instance identifiers; review before sharing"});
    parser.addOption({"wait-ms","Parent device-worker wait budget (100..30000 ms); never terminates a driver call","milliseconds","30000"});
    parser.addOption({"collect","Collect a late worker report by token; preserves prior timeout uncertainty","token"});
    parser.addOption({"identification-plan","Report blocked ECU identification; no driver operations"});
    parser.addOption({"trace","Optional NEW JSONL device-call trace; review vendor strings before sharing","file"});
    QCommandLineOption internal("internal-worker","Internal isolated worker token","token");internal.setFlags(QCommandLineOption::HiddenFromHelp);parser.addOption(internal);
    parser.process(app);
    auto report=base(FASTECU_DIAGNOSTIC_COMMIT);report["adapter_label"]=parser.value("adapter-kind");
    #ifdef FASTECU_DIAGNOSTICS_TEST_BUILD
    report["test_build"]=true;
#else
    report["test_build"]=false;
#endif
#ifdef FASTECU_DIAGNOSTIC_DIRTY_TREE
    report["source_working_tree_dirty"]=true;
#else
    report["source_working_tree_dirty"]=false;
#endif
    report["stage"]="A_passive";report["validation_level"]="discovery_only";
    bool privateDetails=parser.isSet("include-private-details");
    const auto emitReport=[&](QJsonObject result,int code) {
        if(parser.isSet("internal-worker")) {
            const auto token=parser.value("internal-worker");
            if(!QRegularExpression("^[a-f0-9]{32}$").match(token).hasMatch() || !QFileInfo(jobDirectory(token)).isDir())return 2;
            const auto directory=jobDirectory(token);const auto temporary=QDir(directory).filePath("result-writing.json");
            if(!writeNewJson(temporary,result) || !QFile::rename(temporary,QDir(directory).filePath("result.json")))return 1;
            return code;
        }
        result=redact(result,privateDetails).toObject();result["private_details_included"]=privateDetails;
        result["request_refused"] = code == 2;
        QFile jsonFile(parser.value("output")), summaryFile(parser.value("summary"));
        if (parser.isSet("output") && !jsonFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            result["export_error"] = "Report destination unavailable or already exists; no existing file overwritten";
            result["ok"] = false; code = 1;
        }
        if (parser.isSet("summary") && !summaryFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            result["summary_export_error"] = "Summary destination unavailable or already exists; no existing file overwritten";
            result["ok"] = false; code = 1;
        }
        result["human_summary"] = humanSummary(result);
        if (summaryFile.isOpen()) {
            const auto bytes = result["human_summary"].toString().toUtf8();
            if (summaryFile.write(bytes) != bytes.size() || !summaryFile.flush()) {
                result["summary_export_error"] = "Failed writing readable summary";
                result["ok"] = false; code = 1;
                result["human_summary"] = humanSummary(result);
            }
        }
        if (jsonFile.isOpen()) {
            const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
            if (jsonFile.write(bytes) != bytes.size() || !jsonFile.flush()) {
                result["export_error"] = "Failed writing JSON report";
                result["ok"] = false; code = 1;
                result["human_summary"] = humanSummary(result);
            }
        }
        QTextStream(stdout)<<QJsonDocument(result).toJson(QJsonDocument::Indented);return code;
    };
    if(parser.isSet("internal-worker")) {
        const auto token=parser.value("internal-worker");
        if(!QRegularExpression("^[a-f0-9]{32}$").match(token).hasMatch() || !QFileInfo(jobDirectory(token)).isDir())return 2;
    }
    const bool managerTest=parser.isSet("adapter-manager-test");
    if(managerTest || parser.isSet("adapter-manager-list")) {
        if(parser.isSet("validate-device") || parser.isSet("qualify-openport-usb-only") || parser.isSet("collect") || parser.isSet("inspect-file") || parser.isSet("identification-plan") ||
           (managerTest && parser.isSet("adapter-manager-list"))) {
            report["error"]="Adapter Manager refuses mixed operations";return emitReport(report,2);
        }
        const auto root=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../..");
        QJsonArray errors,entries;const auto adapters=J2534Adapters::enumerate(root,errors);
        if(!managerTest) {
            for(const auto &a:adapters)entries.append(a.json());
            report["adapters"]=entries;report["registry_errors"]=errors;report["ok"]=true;
            // Paths are machine-local internal selection keys, not a mandatory user export.
            privateDetails=true;return emitReport(report,0);
        }
        J2534Adapters::Descriptor selected;bool found=false;
        for(const auto &a:adapters)if(a.driver.path==parser.value("adapter-manager-test")){selected=a;found=true;break;}
        if(!found || !selected.supported()) {report["error"]=found?selected.unavailable:"Adapter selection no longer available; refresh the list";return emitReport(report,2);}
        report["adapter_label"]=selected.kind;
        if(parser.isSet("internal-worker")) {
            if(selected.hash!=parser.value("driver-sha256")){report["error"]="Driver changed after selection";return emitReport(report,2);}
#ifdef Q_OS_WIN
            WindowsDeviceApi api;api.pinHash(selected.hash);api.managerDiagnostics(selected.driver.path);
            if(selected.kind=="reviewed-custom-openport")api.qualifyCustomOpenPort(QDir(jobDirectory(parser.value("internal-worker"))).filePath("driver-lifecycle.log"));
            const auto result=validateDevice(selected.driver,true,true,api);
            report["device_result"]=result;report["ok"]=result["ok"].toBool() && result["version_ok"].toBool();
            report["device_opened"]=result.contains("device_handle");
            if(!report["ok"].toBool())report["usb_pnp"]=usbInfo(true);
#else
            report["ok"]=false;report["error"]="Adapter testing requires native Windows";
#endif
        } else {
            const auto result=isolatedDevice(selected.driver.path,selected.kind,selected.hash,QString(),15000,false,true);
            report["device_result"]=result;report["ok"]=result["ok"].toBool();report["device_opened"]=result["device_opened"];
        }
        return emitReport(report,report["ok"].toBool()?0:1);
    }
    const bool customRequest=parser.isSet("qualify-openport-usb-only");
    if(customRequest && (parser.isSet("validate-device") || parser.isSet("inspect-file") ||
       parser.isSet("collect") || parser.isSet("identification-plan") ||
       (parser.isSet("adapter-kind") && parser.value("adapter-kind")!="reviewed-custom-openport"))) {
        report["error"]="Custom OpenPort mode cannot be combined with another operation or adapter identity";
        return emitReport(report,2);
    }
    if(customRequest){report["adapter_label"]="reviewed-custom-openport";
        report["reviewed_driver_sha256"]=reviewedOpenPortHash();report["reviewed_libusb_sha256"]=reviewedLibusbHash();}
    if(parser.isSet("identification-plan")){report["operation"]=identificationPlan();report["ok"]=false;return emitReport(report,2);}
    if(parser.isSet("inspect-file")) {
        Driver file;file.path=QFileInfo(parser.value("inspect-file")).absoluteFilePath();inspect(file,nativeMachine(),false);
        report["operation"]="file_inspection_only";report["file"]=file.path;report["pe"]=file.json();report["file_info"]=fileInfo(file.path);
        report["ok"]=report["file_info"].toObject()["exists"].toBool();return emitReport(report,report["ok"].toBool()?0:1);
    }
    if(parser.isSet("collect")) {
        const auto token=parser.value("collect");
        if(!QRegularExpression("^[a-f0-9]{32}$").match(token).hasMatch()){report["error"]="Invalid worker result token";return emitReport(report,2);}
        const auto directory=jobDirectory(token);auto result=readJson(QDir(directory).filePath("result.json"));
        report["stage"]="B_late_result_review";
        if(result.isEmpty()){report["error"]="Worker result not available; no operation retried";return emitReport(report,1);}
        report["device_result"]=result;report["prior_parent_timeout"]=QFileInfo::exists(QDir(directory).filePath("parent-timeout.json"));
        report["ok"]=false;report["validation_level"]="review_required_late_result";return emitReport(report,1);
    }
    QJsonArray registryErrors;const auto drivers=customRequest?QVector<Driver>():registeredDrivers(registryErrors);
    report["registry_errors"]=registryErrors;
    if(parser.isSet("validate-device") || customRequest) {
        report["stage"]="B_explicit_USB_only";report["ok"]=false;
        bool found=false;Driver selected;
        if(customRequest) {
            selected.path=parser.value("qualify-openport-usb-only");
            selected.api="04.04";selected.id="reviewed-custom-openport-usb-only";
            selected.description="Reviewed custom OpenPort USBQUAL/1";selected.manufacturer="Reviewed NikolaKozina derivative";
            selected.view="x64";inspect(selected);found=true;
        } else for(const auto &d:drivers)if(d.id==parser.value("validate-device")){selected=d;found=true;break;}
        if(!found){report["error"]="Selected driver ID not found; no DLL loaded";return emitReport(report,1);}
        const auto policy=customRequest ? reviewedOpenPortPolicy(selected,parser.value("driver-sha256"),
            parser.isSet("vehicle-disconnected"),parser.isSet("reviewed-open-behavior")) :
            devicePolicy(selected,parser.value("adapter-kind"),parser.isSet("vehicle-disconnected"),parser.isSet("reviewed-open-behavior"),parser.value("driver-sha256"));
        if(!policy.isEmpty()){report["error"]=policy;return emitReport(report,2);}
        inspect(selected);
        if(!selected.issue.isEmpty()){report["error"]=selected.issue;return emitReport(report,1);}
        report["driver"]=selected.json();report["driver_file"]=fileInfo(selected.path);
        if(report["driver_file"].toObject()["sha256"].toString().compare(parser.value("driver-sha256"),Qt::CaseInsensitive)!=0) {
            report["error"]="Reviewed driver hash mismatch or unreadable file; no DLL loaded";return emitReport(report,1);
        }
        if(parser.isSet("internal-worker")) {
            const auto token=parser.value("internal-worker");
            if(!QRegularExpression("^[a-f0-9]{32}$").match(token).hasMatch() || !QFileInfo(jobDirectory(token)).isDir())return 2;
#ifdef Q_OS_WIN
            WindowsDeviceApi api;api.pinHash(parser.value("driver-sha256"));
            if(customRequest)api.qualifyCustomOpenPort(QDir(jobDirectory(token)).filePath("driver-lifecycle.log"));
            J2534Trace trace;
            if(parser.isSet("trace"))trace.start(parser.value("trace"));
            const auto outcome=validateDevice(selected,true,true,api,&trace);trace.finish();
            report["device_result"]=outcome;report["trace_failed"]=trace.failed();
            report["device_opened"]=outcome.contains("device_handle");
            report["ok"]=outcome["ok"].toBool() && outcome["version_ok"].toBool();
            report["validation_level"]=outcome["validation_level"];
#else
            report["error"]="Device operations require native Windows";report["ok"]=false;
#endif
            return emitReport(report,report["ok"].toBool()?0:1);
        }
        bool number=false;const int wait=parser.value("wait-ms").toInt(&number);
        if(!number || wait<100 || wait>30000){report["error"]="wait-ms must be 100..30000; no driver called";return emitReport(report,2);}
        report["open_behavior_review"]="Owner attestation only; this tool cannot prove vendor initialization/firmware side effects or physical adapter identity.";
        const auto outcome=isolatedDevice(customRequest?selected.path:selected.id,customRequest?"reviewed-custom-openport":parser.value("adapter-kind"),parser.value("driver-sha256"),parser.value("trace"),wait,customRequest);
        report["device_result"]=outcome;report["ok"]=outcome["ok"].toBool();
        report["device_opened"]=outcome["device_opened"];
        report["validation_level"]=outcome["validation_level"];return emitReport(report,report["ok"].toBool()?0:1);
    }
    QJsonArray entries;int compatible=0;
    for(const auto &driver:drivers){auto entry=driver.json();entry["file_info"]=fileInfo(driver.path);entries.append(entry);if(driver.api=="04.04"&&driver.issue.isEmpty())++compatible;}
    report["drivers"]=entries;report["compatible_04_04_count"]=compatible;
    report["usb_pnp"]=usbInfo(parser.isSet("usb-all"));report["ok"]=registryErrors.isEmpty();
    report["status"]=drivers.isEmpty()?"no_drivers_registered":"discovery_complete";
    report["note"]="Passive registry/file/PnP inspection only. A registration/file/signature does not establish device presence, driver behavior or compatibility.";
    return emitReport(report,registryErrors.isEmpty()?0:1);
}
