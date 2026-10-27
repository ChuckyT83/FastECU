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
    lines << "No ECU/vehicle compatibility or programming authorization is established. Review JSON before sharing.";
    return lines.join('\n') + '\n';
}
}
#endif
