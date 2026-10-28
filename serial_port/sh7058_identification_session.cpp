#include "sh7058_identification_session.h"
#include "../tools/j2534_diagnostics/passive_metadata.h"
#include "j2534_registry.h"
#include <QJsonDocument>
#include <QApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QRegularExpression>
#ifdef Q_OS_WIN
SH7058IdentificationSession::SH7058IdentificationSession(const QString &library):library_(library) {
    QObject::connect(&port_,&SerialPortActionsDirect::LOG_E,&port_,[this](const QString &text,bool,bool) {
        errors_.append(text.left(500));if(errors_.size()>8)errors_.removeFirst();
    });
}
bool SH7058IdentificationSession::acquire() {
    if (used_ || (qApp && qApp->property("guiSmokeTest").toBool())) return false;
    used_=true;
    port_.is_iso15765_connection=true; port_.is_can_connection=false;
    port_.is_iso14230_connection=false;port_.is_29_bit_id=false;
    port_.can_speed="500000";port_.iso15765_source_address=0x7e0;port_.iso15765_destination_address=0x7e8;
    const auto path=library_.toLocal8Bit();
    if (QString::fromLocal8Bit(path)!=library_ || path.size()>255) return false;
    port_.j2534->setDllName(path.constData());
    if (port_.init_j2534_connection()!=STATUS_SUCCESS) return false;
    // Initialization may tolerate unavailable version metadata for older callers;
    // this workflow requires a complete matching 04.04 API version.
    char api[256]={},dll[256]={},firmware[256]={};
    const auto status=port_.j2534->PassThruReadVersion(api,dll,firmware,port_.devID);
    if (status || !memchr(api,0,80) || !memchr(dll,0,80) || !memchr(firmware,0,80) ||
        QString::fromLatin1(api)!="04.04" || !dll[0] || !firmware[0]) {port_.close_j2534_checked();return false;}
    port_.use_openport2_adapter=true;epoch_=1;return true;
}
quint64 SH7058IdentificationSession::generation() const noexcept {
    return port_.J2534_open_ok && port_.J2534_connect_ok && port_.J2534_init_ok ? epoch_:0;
}
CommunicationResult SH7058IdentificationSession::submit(const QByteArray &bytes) {
    if (!generation() || (bytes!=SubaruSH7058::identityRequest(false) && bytes!=SubaruSH7058::identityRequest(true)))
        return CommunicationResult::failure(CommunicationResult::State::Unsupported,"Only firmware-reviewed AA and 09/04 requests are admitted");
    return port_.write_j2534_checked(bytes);
}
CommunicationResult SH7058IdentificationSession::receive(uint16_t wait) {
    if (!generation()) return CommunicationResult::failure(CommunicationResult::State::Disconnected,"Identification channel retired");
    return port_.read_j2534_checked(wait);
}
bool SH7058IdentificationSession::release() noexcept {
    epoch_=0;return port_.close_j2534_checked().confirmed();
}
#endif
void addSH7058IdentificationOptions(QCommandLineParser &p) {
    p.addOption({"sh7058-identify","Explicit isolated ECU identity operation; only reviewed OBDX AA/09-04, no session changes","driver-id"});
    p.addOption({"identity-driver-sha256","Exact independently reviewed x64 OBDX DLL SHA-256","hash"});
    p.addOption({"reviewed-obdx-driver","Confirm official OBDX driver/open/firmware behavior reviewed and other adapter users closed"});
    p.addOption({"ecu-identification-authorized","Explicitly authorize only the reviewed non-programming identity requests"});
    p.addOption({"identity-output","NEW result JSON in an existing writable directory","file"});
}
int runSH7058IdentificationWorker(const QCommandLineParser &p) {
    QJsonObject report{{"timestamp_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"schema","fastecu.sh7058-identity.v1"},{"ok",false},{"programming_allowed",false},
        {"session_change_requested",false},{"security_requested",false},{"firmware_variant_observed",false}};
    const auto dest=p.value("identity-output");
    if (dest.isEmpty() || QFileInfo::exists(dest)) return 2;
    QFile output(dest+".writing");
    if (!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 2;
    auto finish=[&](int code,const QString &detail) {
        report["detail"]=detail;
        const auto bytes=QJsonDocument(DiagnosticReport::redact(report,false).toObject()).toJson();
        if(output.write(bytes)!=bytes.size() || !output.flush())return 1;
        output.close();if(!QFile::rename(output.fileName(),dest))return 1;return code;
    };
    if ((qApp && qApp->property("guiSmokeTest").toBool()) ||
        !p.isSet("reviewed-obdx-driver") || !p.isSet("ecu-identification-authorized") ||
        !QRegularExpression("^[a-fA-F0-9]{64}$").match(p.value("identity-driver-sha256")).hasMatch())
        return finish(2,"Reviewed OBDX driver/hash and explicit ECU identification authorization required; no device opened");
#ifndef Q_OS_WIN
    return finish(2,"Native Windows x64 is required; no device opened");
#else
    if (sizeof(void*)!=8) return finish(2,"The full GUI identification worker requires x64");
    // One such worker per Windows session. This does not exclude unrelated vendor apps.
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\FastECU-SH7058-Identity");
    if(!mutex)return finish(1,"Cannot establish exclusive worker ownership");
    const auto lockStatus=WaitForSingleObject(mutex,0);
    if(lockStatus!=WAIT_OBJECT_0){CloseHandle(mutex);return finish(1,"Another worker owns the device, or prior ownership was abandoned; review required");}
    struct MutexRelease{HANDLE h;~MutexRelease(){ReleaseMutex(h);CloseHandle(h);}} mutexOwner{mutex};
    QJsonArray errors;const auto drivers=J2534Diagnostics::registeredDrivers(errors);
    if(!errors.isEmpty())return finish(1,"Driver discovery incomplete; no DLL loaded");
    J2534Diagnostics::Driver selected;bool found=false;
    for(const auto &d:drivers)if(d.id==p.value("sh7058-identify")){selected=d;found=true;break;}
    if(!found)return finish(2,"Exact registered driver ID not found; no DLL loaded");
    const auto description=(selected.id+" "+selected.description+" "+selected.manufacturer+" "+selected.path).toLower();
    if(!description.contains("obdx") || description.contains("tactrix") || description.contains("openport") || description.contains("op20pt") ||
        selected.api!="04.04" || !selected.issue.isEmpty())return finish(2,"Requires reviewed matching-bitness official OBDX 04.04; clone/unreviewed/malformed drivers refused");
    // Pin and deny replacement/writes throughout load/open/channel/close/unload.
    HANDLE file=CreateFileW(reinterpret_cast<LPCWSTR>(selected.path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return finish(1,"Cannot lock reviewed driver file; no DLL loaded");
    struct FileRelease{HANDLE h;~FileRelease(){CloseHandle(h);}} fileOwner{file};
    const auto metadata=DiagnosticReport::fileInfo(selected.path);
    if(metadata["sha256"].toString().compare(p.value("identity-driver-sha256"),Qt::CaseInsensitive)!=0)
        return finish(2,"Driver hash changed or unavailable; no DLL loaded");
    report["driver_id"]=selected.id;report["driver_sha256"]=metadata["sha256"];
    SH7058IdentificationSession session(selected.path);std::atomic<bool> cancel{false};
    const auto result=SubaruSH7058::identify(session,cancel,2000);
    const auto cleanup=session.cleanup();
    report["ok"]=result.complete;report["ecu_id"]=result.identity.ecu;report["obd_calibration_id"]=result.identity.calibration;
    report["full_firmware_id"]=QString();report["generation"]=QString::number(result.identity.generation);
    report["backend_errors"]=QJsonArray::fromStringList(session.errors());
    report["cleanup_confirmed"]=result.cleanupConfirmed;report["disconnect_attempted"]=cleanup.disconnectAttempted;
    report["disconnect_status"]=qint64(cleanup.disconnectStatus);report["close_attempted"]=cleanup.closeAttempted;report["close_status"]=qint64(cleanup.closeStatus);
    report["driver_status_present"]=result.transport.hasStatus;if(result.transport.hasStatus)report["driver_status"]=qint64(result.transport.status);
    report["ecu_reply_bytes"]=result.identity.ecuReply.size();report["calibration_reply_hex"]=QString::fromLatin1(result.identity.calibrationReply.toHex());
    return finish(result.complete?0:1,result.detail);
#endif
}
QJsonObject launchSH7058Identification(const QString &driverId,const QString &hash) {
    QJsonObject failure{{"ok",false},{"programming_allowed",false}};
    if(qApp && qApp->property("guiSmokeTest").toBool()){failure["detail"]="Hardware opening disabled in GUI smoke mode";return failure;}
    QTemporaryDir directory;
    if(!directory.isValid()){failure["detail"]="Cannot create isolated worker directory";return failure;}
    directory.setAutoRemove(false); // Keep late evidence; never remove a running worker's output.
    const auto resultPath=directory.filePath("result.json");
    QProcess worker;worker.setProgram(QCoreApplication::applicationFilePath());
    worker.setArguments({"--sh7058-identify",driverId,"--identity-driver-sha256",hash,"--reviewed-obdx-driver","--ecu-identification-authorized","--identity-output",resultPath});
    worker.setStandardOutputFile(directory.filePath("stdout.txt"));worker.setStandardErrorFile(directory.filePath("stderr.txt"));qint64 pid=0;
    if(!worker.startDetached(&pid)){failure["detail"]="Unable to start isolated identity worker";return failure;}
    QEventLoop loop;QTimer poll,deadline;deadline.setSingleShot(true);
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(QFileInfo::exists(resultPath))loop.quit();});
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);poll.start(50);deadline.start(30000);loop.exec();
    QFile file(resultPath);
    if(file.open(QIODevice::ReadOnly)) {auto r=QJsonDocument::fromJson(file.readAll()).object();if(!r.isEmpty()){r["report_path"]=resultPath;return r;}}
    failure["worker_pid"]=QString::number(pid);failure["report_path"]=resultPath;failure["uncertain"]=true;
    failure["detail"]="Worker did not return a complete result within 30 seconds. It was NOT terminated; no concurrent Close/unload or retry. Stop and review late evidence; do not unplug during an uncertain vendor call.";return failure;
}
