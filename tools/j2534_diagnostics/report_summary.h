#ifndef FASTECU_DIAGNOSTIC_REPORT_SUMMARY_H
#define FASTECU_DIAGNOSTIC_REPORT_SUMMARY_H
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
namespace DiagnosticReport {
// Input is already privacy-redacted by the caller. No additional discovery/calls.
inline QString humanSummary(const QJsonObject &r) {
    QStringList lines;
    lines << "FastECU J2534 diagnostic summary" << ("UTC: " + r["timestamp_utc"].toString())
          << ("Process architecture: " + r["process_architecture"].toString())
          << ("Stage: " + r["stage"].toString());
    if (r["request_refused"].toBool()) lines << "Request REFUSED; review the reason below. No programming permission.";
    else if(r["ok"].toBool() && r["adapter_label"]=="reviewed-custom-openport") lines << "Reviewed USB-only lifecycle completed; vehicle/CAN/flashing remain UNTESTED.";
    else if (r["ok"].toBool()) lines << "Requested inspection/operation completed; hardware compatibility remains UNTESTED.";
    else lines << "Inspection/operation failed or remains uncertain; review JSON details.";
    if (r.contains("drivers")) {
        const auto drivers = r["drivers"].toArray();
        if (drivers.isEmpty()) lines << "No J2534 driver registrations found in the queried views (driver not installed/registered).";
        for (const auto &value : drivers) {
            const auto d = value.toObject();
            const auto version = [](const QString &value) { return value.isEmpty() ? QString("not available") : value; };
            lines << ("Driver: " + d["description"].toString() + " [" + d["id"].toString() + "]")
                  << ("  DLL: " + d["dll_path"].toString() + "; PE architecture: " + d["architecture_from_header"].toString())
                  << ("  Registered API: " + d["registered_api"].toString() + "; registered version: " + version(d["registered_version"].toString())
                      + "; file version: " + version(d["file_info"].toObject()["file_version"].toString()));
            const auto issue = d["issue"].toString();
            lines << (issue.isEmpty() ? "  Driver file detected; hardware UNTESTED; no DLL loaded by passive inspection." : "  Problem: " + issue);
        }
    }
    if (r.contains("usb_pnp")) {
        const auto usb = r["usb_pnp"].toObject();
        lines << ("USB/PnP metadata: " + usb["status"].toString() + "; listed devices: " + QString::number(usb["devices"].toArray().size()));
        if (usb["status"] == "complete" && usb["devices"].toArray().isEmpty())
            lines << "No candidate adapter detected by this metadata filter; this does not prove physical absence. Review Device Manager or --usb-all.";
        lines << "USB detection is separate from validated J2534 compatibility.";
    }
    for (const auto &key : {"error", "export_error", "summary_export_error"})
        if (r.contains(key)) lines << (QString(key) + ": " + r[key].toString());
    if (!r["registry_errors"].toArray().isEmpty()) lines << "Registry inspection incomplete; see registry_errors in JSON.";
    if (r.contains("pe")) lines << ("File inspection: " + r["pe"].toObject()["issue"].toString());
    if(r["adapter_label"]=="reviewed-custom-openport" && r.contains("device_result")) {
        auto device=r["device_result"].toObject();
        for(int depth=0;depth<4 && !device.contains("open_status") && device.contains("device_result");++depth)
            device=device["device_result"].toObject();
        if(device.contains("open_status")) {
            const auto state=[](bool value){return value?QString("confirmed by reviewed driver evidence"):QString("NOT confirmed");};
            const auto life=device["lifecycle"].toObject();
            lines << "Reviewed custom OpenPort USB-only lifecycle (no vehicle calls):"
                  << ("  Initialization acknowledgment: "+state(device["initialization_acknowledged"].toBool()))
                  << ("  Usable version metadata: "+state(device["version_ok"].toBool()))
                  << ("  Adapter shutdown acknowledgment: "+state(life["adapter_shutdown_confirmed"].toBool()))
                  << ("  USB interface release: "+state(life["usb_interface_release_confirmed"].toBool()))
                  << ("  Void handle-close call returned: "+state(life["usb_handle_close_returned"].toBool()))
                  << "  Windows kernel-handle release: not independently observed; do not infer it from a void return.";
            for(const auto &key:{"error","cleanup_error","driver_error_detail","close_error_detail"})
                if(device.contains(key))lines << ("  "+QString(key)+": "+device[key].toString());
        }
    }
    lines << "No ECU/vehicle compatibility or programming authorization is established. Review JSON before sharing.";
    return lines.join('\n') + '\n';
}
}
#endif
