#ifndef FASTECU_DEVICE_OPERATION_POLICY_H
#define FASTECU_DEVICE_OPERATION_POLICY_H
#include "passive_metadata.h"
#include <QRegularExpression>
namespace DiagnosticReport {
inline QString devicePolicy(const J2534Diagnostics::Driver &driver,const QString &kind,
                            bool disconnected,bool reviewed,const QString &expectedHash) {
    if(kind!="obdx-pro-vx")return "Only passive inspection is available for clones and unreviewed adapters. Task 007 must review clone software first.";
    const auto description=(driver.id+" "+driver.description+" "+driver.manufacturer+" "+driver.path).toLower();
    if(description.contains("tactrix") || description.contains("openport") || description.contains("op20pt"))
        return "OpenPort/clone device calls are not qualified in this toolkit; passive inspection only.";
    if(!disconnected || !reviewed)return "Vehicle-disconnected and driver-specific open/firmware-behavior review acknowledgements are required. No DLL loaded.";
    if(!QRegularExpression("^[a-fA-F0-9]{64}$").match(expectedHash).hasMatch())
        return "Provide the reviewed driver's exact SHA-256; this pins selection, not safety or compatibility.";
    if(driver.api!="04.04")return "Device calls support reviewed J2534 04.04 only; registration inspection supports 04.04 and 05.00.";
    return {};
}
}
#endif
