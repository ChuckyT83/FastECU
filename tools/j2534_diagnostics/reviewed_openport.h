#ifndef FASTECU_REVIEWED_CUSTOM_OPENPORT_H
#define FASTECU_REVIEWED_CUSTOM_OPENPORT_H
#include "passive_metadata.h"
#include <QJsonDocument>
#include <QMap>
namespace DiagnosticReport {
inline QString reviewedOpenPortHash() {
#ifdef FASTECU_DIAGNOSTICS_TEST_BUILD
    const auto fixture=qEnvironmentVariable("FASTECU_USBQUAL_TEST_HASH");
    if(!fixture.isEmpty())return fixture;
#endif
#ifdef FASTECU_REVIEWED_OPENPORT_SHA256
    return QStringLiteral(FASTECU_REVIEWED_OPENPORT_SHA256);
#else
    return QStringLiteral("d9a0f009a1eea6f5a6957b341880c6c68942c80dc946dfe8eb76c84c5ab4a554");
#endif
}
inline QString reviewedLibusbHash() {
#ifdef FASTECU_DIAGNOSTICS_TEST_BUILD
    const auto fixture=qEnvironmentVariable("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    if(!fixture.isEmpty())return fixture;
#endif
    return QStringLiteral("5f9b026847dadff2ee0891e1b6b607cd9a5e89eef4fadef17a25c72e781d5a3c");
}
inline QString reviewedOpenPortPolicy(const J2534Diagnostics::Driver &driver,
                                     const QString &expected,bool disconnected,bool reviewed) {
    if(!disconnected || !reviewed)return "Reviewed custom OpenPort requires vehicle-disconnected and reviewed-open-behavior acknowledgements";
    if(driver.machine!=0x8664 || J2534Diagnostics::nativeMachine()!=0x8664)
        return "Reviewed custom OpenPort qualification requires x64 DLL and x64 process";
    if(!driver.issue.isEmpty())return driver.issue;
    if(expected.compare(reviewedOpenPortHash(),Qt::CaseInsensitive)!=0)
        return "This mode admits only the reviewed USBQUAL/1 custom DLL hash; original, earlier candidates and proprietary drivers are refused";
    if(fileInfo(driver.path)["sha256"].toString().compare(expected,Qt::CaseInsensitive)!=0)
        return "Selected custom DLL does not match the reviewed hash";
    J2534Diagnostics::Driver dependency;
    dependency.path=QFileInfo(driver.path).dir().absoluteFilePath("libusb-1.0.dll");
    J2534Diagnostics::inspect(dependency,0x8664);
    if(!dependency.issue.isEmpty())return "Reviewed libusb dependency: "+dependency.issue;
    if(fileInfo(dependency.path)["sha256"].toString().compare(reviewedLibusbHash(),Qt::CaseInsensitive)!=0)
        return "libusb-1.0.dll must match the reviewed dependency hash";
    return {};
}
inline QJsonObject openPortLifecycleEvidence(const QByteArray &bytes) {
    QJsonObject evidence;
    QMap<QByteArray,int> markers;bool malformed=bytes.size()>65536;
    for(const auto &line:bytes.split('\n')) {
        if(!line.startsWith("USBQUAL/1 "))continue;
        const auto fields=line.mid(10).trimmed().split('=');bool ok=false;
        const int value=fields.size()==2?fields[1].toInt(&ok):0;
        if(!ok || markers.contains(fields.value(0))) {malformed=true;continue;}
        markers[fields[0]]=value;
    }
    const auto equals=[&](const char *key,int value){return !malformed && markers.contains(key) && markers[key]==value;};
    evidence["lifecycle_evidence_valid"]=!malformed && equals("begin",1);
    evidence["initialization_acknowledged"]=equals("begin",1) && equals("ati_ack",1) && equals("ata_ack",1) && equals("open_complete",1);
    evidence["adapter_shutdown_confirmed"]=equals("shutdown_ack",1);
    evidence["usb_interface_claimed"]=equals("interface_claimed",1);
    evidence["usb_interface_release_attempted"]=markers.contains("release_status");
    evidence["usb_interface_release_confirmed"]=equals("release_status",0);
    if(markers.contains("release_status"))evidence["usb_interface_release_status"]=markers["release_status"];
    evidence["usb_handle_close_returned"]=equals("handle_close_returned",1);
    evidence["usb_context_exit_returned"]=equals("context_exit_returned",1);
    evidence["close_contract_succeeded"]=equals("close_status",0);
    evidence["kernel_handle_release_independently_observed"]=false;
    evidence["evidence_basis"]="Hash-pinned reviewed driver USB acknowledgments and libusb return markers; void close/exit returns are not independent kernel-handle inspection";
    return evidence;
}
}
#endif
