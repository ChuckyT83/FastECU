#ifndef FASTECU_J2534_ADAPTER_SELECTION_H
#define FASTECU_J2534_ADAPTER_SELECTION_H
#include "j2534_registry.h"
#include "../tools/j2534_diagnostics/reviewed_openport.h"
#include <QCoreApplication>

// Shared adapter selection, independent of any ECU protocol or programming gate.
namespace J2534Adapters {
struct Descriptor {
    J2534Diagnostics::Driver driver;
    QString kind, name, hash, unavailable;
    bool supported() const {return unavailable.isEmpty();}
    QJsonObject json() const {
        return {{"id",driver.path},{"name",name},{"kind",kind},{"hash",hash},
            {"available",supported()},{"reason",unavailable},{"driver",driver.json()},
            {"capabilities",supported()?QJsonArray{"usb_lifecycle","version_information"}:QJsonArray()}};
    }
};
inline Descriptor classify(J2534Diagnostics::Driver driver,const QJsonObject &info) {
    using namespace DiagnosticReport;
    Descriptor a;a.driver=driver;a.hash=info["sha256"].toString();a.name=driver.description;a.kind="unsupported";
    if(a.hash.compare(reviewedOpenPortHash(),Qt::CaseInsensitive)==0 && !a.hash.isEmpty()) {
        a.kind="reviewed-custom-openport";a.name="OpenPort 2.0 (Custom x64)";
        a.unavailable=reviewedOpenPortPolicy(driver,a.hash,true,true);
    } else {
        const auto label=(driver.description+" "+driver.manufacturer+" "+QFileInfo(driver.path).fileName()).toLower();
        const auto company=info["company_name"].toString().toLower();
        const auto product=info["product_name"].toString().toLower();
        if(label.contains("tactrix") || label.contains("openport") || label.contains("op20pt"))
            a.unavailable="Proprietary or unreviewed OpenPort driver; use the packaged custom x64 driver";
        else if(label.contains("obdx") && (label.contains("vx") || product.contains("vx")) &&
                (company.contains("obdx") || company.contains("envyous")) && product.contains("obdx")) {
            a.kind="obdx-pro-vx";a.name="OBDX Pro VX (Windows J2534)";
            if(!driver.issue.isEmpty())a.unavailable=driver.issue;
            else if(driver.api!="04.04")a.unavailable="Only J2534 API 04.04 adapter tests are supported";
            else if(a.hash.isEmpty())a.unavailable="Cannot hash the selected driver";
        } else a.unavailable="Unsupported driver identity: OBDX registry model and embedded vendor/product information required";
    }
    if(!driver.issue.isEmpty())a.unavailable=driver.issue;
    return a;
}
inline QVector<Descriptor> enumerate(const QString &applicationDirectory,QJsonArray &errors) {
    auto drivers=J2534Diagnostics::registeredDrivers(errors);
    // A reviewed manifest is build provenance, not a runtime trust override.
    const auto manifestPath=QDir(applicationDirectory).filePath("adapters/openport/adapter-manifest.json");
    QFile manifestFile(manifestPath);
    QString manifestIssue;
    if(manifestFile.open(QIODevice::ReadOnly) && manifestFile.size()<65536) {
        const auto m=QJsonDocument::fromJson(manifestFile.readAll()).object();
        const bool valid=m["driver_sha256"].toString()==DiagnosticReport::reviewedOpenPortHash() &&
            m["dependency_sha256"].toString()==DiagnosticReport::reviewedLibusbHash() &&
            m["source_revision"].toString()=="22ae8e85aa421dd74b2a3678bd9bf0f0db8ff55a" && m["lifecycle"].toString()=="USBQUAL/1";
        if(!valid)manifestIssue="Packaged OpenPort build manifest does not match this application";
    } else manifestIssue="Packaged OpenPort build manifest missing or unreadable";
    J2534Diagnostics::Driver bundled;bundled.path=QDir(applicationDirectory).filePath("adapters/openport/j2534.dll");
    bundled.api="04.04";bundled.description="OpenPort 2.0 (Custom x64, packaged)";bundled.id="packaged-openport";
    J2534Diagnostics::inspect(bundled);drivers.prepend(bundled);
    QVector<Descriptor> out;QStringList paths;
    for(const auto &driver:drivers) {
        if(paths.contains(driver.path,Qt::CaseInsensitive))continue;
        paths<<driver.path;
        auto a=classify(driver,DiagnosticReport::fileInfo(driver.path));
        if(driver.id=="packaged-openport" && !manifestIssue.isEmpty())a.unavailable=manifestIssue;
        out.append(a);
    }
    return out;
}
inline QJsonObject presentation(const QJsonObject &report) {
    auto worker=report["device_result"].toObject();
    auto calls=worker["device_result"].toObject();
    if(calls.isEmpty())calls=worker;
    const bool pending=worker["validation_level"].toString()=="device_call_pending_or_uncertain" ||
        worker["error"].toString().contains("uncertain",Qt::CaseInsensitive);
    QString status;
    if(pending)status="Adapter test is still pending or uncertain. Overlapping tests are disabled.";
    else if(report["ok"].toBool())status="Connected successfully (USB adapter test)";
    else if(calls.contains("open_status") && calls["open_status"].toInt()!=0)
        status="Adapter initialization failed. Check USB connection/driver or whether another application is using it.";
    else if(calls.contains("close_status") && calls["close_status"].toInt()!=0)
        status="Adapter did not close cleanly. See technical details.";
    else if(calls["version_called"].toBool() && !calls["version_ok"].toBool())status="Version information unavailable or invalid.";
    else if(calls["primary_error_stage"].toString()=="initialization_evidence")status="Initialization handshake was not confirmed.";
    else {status=report["error"].toString();if(status.isEmpty())status=calls["error"].toString();if(status.isEmpty())status="Driver failed to load or complete the adapter test. See technical details.";}
    return {{"status",status},{"pending",pending},{"firmware",calls["firmware_version"]},
        {"dll",calls["dll_version"]},{"api",calls["api_version"]},{"token",worker["worker_result_token"]}};
}
}
#endif
