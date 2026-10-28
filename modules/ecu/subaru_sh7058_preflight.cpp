#include "subaru_sh7058_preflight.h"
#include <QDomDocument>
#include <QXmlStreamReader>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <functional>
namespace SubaruSH7058 {
QByteArray identityRequest(bool calibration) {
    return QByteArray::fromHex(calibration ? "000007e00904" : "000007e0aa");
}
bool parseIdentityReply(bool calibration, const QByteArray &frame, QString &id, QString &error) {
    id.clear(); error.clear();
    if (frame.size()<5 || frame.left(4)!=QByteArray::fromHex("000007e8")) { error="Wrong/truncated response address"; return false; }
    const auto body=frame.mid(4);
    if (uint8_t(body[0])==0x7f) { error="Negative/pending response; no automatic retry or session transition"; return false; }
    if (!calibration) {
        // Pinned Base: AA170 gathers 104 ROM bytes via EC8C8, preceded by EA.
        // The first three bytes are A2 10 11; five ECU-ID bytes follow.
        if (body.size()!=105 || body.left(4)!=QByteArray::fromHex("eaa21011")) { error="Malformed EA ECU-ID response"; return false; }
        id=QString::fromLatin1(body.mid(4,5).toHex().toUpper()); return true;
    }
    // One OBD service-09 PID-04 record, 16 bytes. Never select the first of
    // multiple records or silently truncate a modified firmware identifier.
    if (body.size()!=19 || body.left(3)!=QByteArray::fromHex("490401")) { error="Expected exactly one 16-byte CAL-ID record"; return false; }
    auto bytes=body.mid(3); while (bytes.endsWith(char(0))) bytes.chop(1);
    if (bytes.isEmpty()) { error="Empty calibration identifier"; return false; }
    for (const char c:bytes) if (uint8_t(c)<0x20 || uint8_t(c)>0x7e) { error="Non-printable or embedded padding in CAL-ID"; return false; }
    id=QString::fromLatin1(bytes); return true;
}
IdentificationResult identify(IdentificationLink &link, const std::atomic<bool> &cancel,
        uint16_t timeout, const IdentificationClock *clock) {
    IdentificationResult r;
    if (cancel.load()) { r.detail="Cancelled before acquisition"; return r; }
    if ((!link.offlineFixture() && !link.isolatedDiagnostic()) || !timeout) {
        r.detail="Live SH7058 identification unavailable: a reviewed isolated OBDX session is required. No device acquired or request sent."; return r;
    }
    QElapsedTimer timer; timer.start();
    const auto now=[&] { return clock ? clock->nowMs() : timer.elapsed(); };
    const auto acquisitionBegan=now();
    try { r.acquired=link.acquire(); } catch (...) { r.detail="Acquisition failed; ownership unknown"; return r; }
    if (!r.acquired) { r.detail="Device/channel already owned or acquisition failed"; return r; }
    const auto epoch=link.generation();
    auto owned=[&] { return epoch && (link.offlineFixture() || link.isolatedDiagnostic()) && link.generation()==epoch; };
    auto checked=[](const CommunicationResult &v) { return v.ok() && v.hasStatus && v.status==0; };
    try {
        const auto acquiredAt=now();
        if (acquiredAt<acquisitionBegan || acquiredAt-acquisitionBegan>=timeout) {
            r.detail="Acquisition exceeded deadline; no ECU request sent";
        } else for (const bool cal:{false,true}) {
            if (cancel.load() || !owned()) { r.detail="Cancelled or channel generation lost"; break; }
            const auto began=now();
            r.transport=link.receive(0);
            const auto queuedAt=now();
            if (cancel.load() || !owned() || queuedAt<began || queuedAt-began>=timeout || r.transport.state!=CommunicationResult::State::Timeout) { r.detail="Receive queue not empty or ownership lost; no request sent"; break; }
            r.transport=link.submit(identityRequest(cal));
            const auto submitted=now();
            if (cancel.load() || !owned() || submitted<began || submitted-began>=timeout || !checked(r.transport)) { r.detail="Submission failed/uncertain; no retry"; break; }
            r.transport=link.receive(uint16_t(timeout-(submitted-began))); const auto ended=now();
            if (cancel.load() || !owned() || ended<began || ended-began>=timeout || !checked(r.transport)) { r.detail="Timeout, cancellation, transport failure or stale generation; no following request"; break; }
            QString id,error;
            if (!parseIdentityReply(cal,r.transport.bytes,id,error)) { r.detail=error; break; }
            if (!cal) { r.identity.ecu=id; r.identity.ecuReply=r.transport.bytes; }
            else { r.identity.calibration=id; r.identity.calibrationReply=r.transport.bytes; r.complete=true; }
        }
    } catch (...) { r.complete=false; r.detail="Transport callback failed; no retry/reset"; }
    r.cleanupConfirmed=link.release();
    if (!r.cleanupConfirmed) { r.complete=false; r.detail="Device/channel cleanup unconfirmed"; }
    if (cancel.load()) { r.complete=false; r.detail="Cancelled; no retry or session transition"; }
    r.identity.source=link.offlineFixture()?Identification::Source::OfflineReplay:Identification::Source::ObservedDiagnostic; r.identity.generation=epoch;
    if (r.complete) r.identity.observationContext=QUuid::createUuid();
    if (r.complete) r.detail=link.offlineFixture()?"Offline identification replay complete; not observed ECU identity or live authorization":"Read-only ECU/CAL-ID responses received; full custom firmware variant remains unobserved, no programming admission";
    return r;
}
Definition resolveDefinition(const QVector<QByteArray> &documents, const QString &selected) {
    Definition d; d.xmlId=selected;
    struct Node { QString parent; QMap<QString,QString> fields; };
    QMap<QString,Node> nodes;
    for (const auto &bytes:documents) {
        // This metadata parser has no external entity/file/network resolver.
        if (bytes.size()>16*1024*1024 || bytes.contains("<!DOCTYPE") || bytes.contains("<!ENTITY")) { d.error="Unsupported XML size/DTD/entities"; return d; }
        QXmlStreamReader scan(bytes);
        while (!scan.atEnd()) {
            if (scan.readNext()==QXmlStreamReader::DTD) { d.error="DTD/entity definitions are unsupported"; return d; }
        }
        if (scan.hasError()) { d.error="Malformed definition XML"; return d; }
        QDomDocument xml; if (!xml.setContent(bytes)) { d.error="Malformed definition XML"; return d; }
        const auto documentHash=SubaruSH7058Safety::fingerprint(bytes);
        d.documentHashes.append(QString::fromLatin1(documentHash.toHex()));
        const auto roms=xml.elementsByTagName("rom");
        for (int i=0;i<roms.size();++i) {
            const auto rom=roms.at(i).toElement(); const auto romid=rom.firstChildElement("romid");
            const auto id=romid.firstChildElement("xmlid").text().trimmed();
            if (id.isEmpty() || nodes.contains(id)) { d.error="Missing or duplicate/ambiguous XML ID"; return d; }
            Node n; n.parent=rom.attribute("base").trimmed();
            const auto includes=rom.elementsByTagName("include");
            if (includes.size()>1 || (!n.parent.isEmpty() && includes.size())) { d.error="Ambiguous inheritance"; return d; }
            if (includes.size()) n.parent=includes.at(0).toElement().text().trimmed();
            for (auto e=romid.firstChildElement();!e.isNull();e=e.nextSiblingElement()) {
                if (n.fields.contains(e.tagName())) { d.error="Duplicate identity metadata"; return d; }
                if (!e.text().trimmed().isEmpty()) n.fields[e.tagName()]=e.text().trimmed();
            }
            nodes[id]=n;
            d.documentHashByXmlId[id]=documentHash;
        }
    }
    QSet<QString> seen;
    std::function<bool(const QString&)> visit=[&](const QString &id) {
        if (seen.contains(id) || !nodes.contains(id) || seen.size()>=32) { d.error="Missing parent or cyclic/ambiguous inheritance"; return false; }
        seen.insert(id); d.ancestry.append(id);
        const auto n=nodes[id]; if (!n.parent.isEmpty() && !visit(n.parent)) return false;
        for (auto i=n.fields.begin();i!=n.fields.end();++i) d.fields[i.key()]=i.value();
        return true;
    };
    d.fields=nodes.value(selected).fields; // Retain leaf facts even when a parent is missing.
    d.valid=visit(selected); if (d.valid && d.documentHashes.isEmpty()) { d.valid=false; d.error="Definition evidence missing"; }
    return d;
}
namespace {
const QString FamilyEcu="A62A907107", StockDefinition="AE5T201V";
const QByteArray OriginalReferenceHash=QByteArray::fromHex("15102939df167a4d76be9f33c20966fa4f707bb171aff846c7117108cc9c875c");
QString hardwareName(HardwareState s) {
    switch(s) {case HardwareState::Compatible:return "compatible_with_reviewed_family";
    case HardwareState::Incompatible:return "incompatible";default:return "insufficient_evidence";}
}
QString integrityName(IntegrityState s) {
    switch(s) {case IntegrityState::Valid:return "valid";case IntegrityState::Invalid:return "invalid";
    case IntegrityState::Incomplete:return "incomplete";default:return "unsupported";}
}
QString transitionName(TransitionState s) {
    switch(s) {case TransitionState::SameComparableIdentity:return "same_comparable_identity";
    case TransitionState::DifferentComparableIdentity:return "different_comparable_identity";
    case TransitionState::PartiallyKnown:return "partially_known";default:return "unknown";}
}
QString readinessName(ReadinessState s) {
    switch(s) {case ReadinessState::SectorRequirementsUnknown:return "sector_requirements_unknown";
    case ReadinessState::ProtectedChangesUnsupported:return "protected_changes_unsupported";
    case ReadinessState::ProgrammingDisabled:return "programming_disabled";default:return "invalid_comparison";}
}
QString observationName(Identification::Source s) {
    switch(s) {case Identification::Source::ObservedDiagnostic:return "observed_diagnostic_historical";
    case Identification::Source::OfflineReplay:return "offline_replay";default:return "missing";}
}
QJsonArray strings(const QStringList &list) {QJsonArray a;for(const auto &v:list)a.append(v);return a;}
template<class State> QJsonObject assessmentJson(const Assessment<State> &a,const QString &status) {
    return {{"status",status},{"reasons",strings(a.reasons)},{"evidence",a.evidence}};
}
bool supportedConfiguration(const Configuration &c) {
    return c.protocol=="sub_ecu_denso_sh7058_can" && c.mcu=="SH7058" && c.transport=="iso15765" &&
        SubaruSH7058Safety::validProgrammingMap(c.map) && QString::fromLatin1(c.map->name)==c.mcu && c.kernelAddress==0xffff3000u;
}
QString embeddedId(const QByteArray &image,const Definition &d) {
    const auto declared=d.fields.value("internalidstring");
    const auto id=declared.toLatin1();
    if(QString::fromLatin1(id)!=declared)return {}; // Never replace Unicode with question marks.
    if(id.isEmpty() || id.size()>128)return {};
    for(const char c:id)if(uint8_t(c)<0x20 || uint8_t(c)>0x7e)return {};
    bool ok=false;auto address=d.fields.value("internalidaddress");
    if(address.startsWith("0x",Qt::CaseInsensitive))address=address.mid(2);
    const auto at=address.toULongLong(&ok,16);
    if(!ok || at>quint64(image.size()) || quint64(id.size())>quint64(image.size())-at || image.mid(int(at),id.size())!=id)return {};
    return QString::fromLatin1(id);
}
bool reviewedAssociation(const Definition &d,const ReviewedReferences &refs) {
    return d.valid && d.ancestry.contains(StockDefinition) && refs.stockDefinitionHash.size()==32 &&
        d.documentHashByXmlId.value(StockDefinition)==refs.stockDefinitionHash;
}
Assessment<HardwareState> assessHardware(const Definition &d,const Configuration &c,const Identification &id,
                                        const ReviewedReferences &refs,const QString &embedded,const QString &targetEcu) {
    Assessment<HardwareState> a{HardwareState::InsufficientEvidence};
    const bool associated=reviewedAssociation(d,refs);
    a.evidence={{"reviewed_family","2016 USDM STI / A62A907107 / AE5T201V / SH7058"},
        {"target_family_anchor_verified",associated},{"reviewed_stock_definition_sha256",QString::fromLatin1(refs.stockDefinitionHash.toHex())},{"target_rom_ecu_id",targetEcu},
        {"target_rom_ecu_id_provenance","Reviewed Base.bin AA ROM pointer table: five bytes at 0xE10F0; other layouts require evidence"},{"configured_protocol",c.protocol},{"configured_mcu",c.mcu},
        {"configured_geometry_matches",SubaruSH7058Safety::validProgrammingMap(c.map)},
        {"definition_mcu",d.fields.value("memmodel")},{"definition_ecu_id",d.fields.value("ecuid")},
        {"observed_rom_ecu_id",id.ecu},{"observation_source",observationName(id.source)},
        {"observation_generation",QString::number(id.generation)},
        {"observation_context",id.observationContext.isNull()?QString():id.observationContext.toString(QUuid::WithoutBraces)},
        {"hardware_fact_provenance","MCU/layout from reviewed firmware profile and configuration; ECU identifier is ROM-reported, not independent silicon identification"}};
    const auto conflict=[&](const char *field,const char *expected){return !d.fields.value(field).isEmpty() && d.fields.value(field)!=expected;};
    if(!supportedConfiguration(c) || conflict("memmodel","SH7058") || conflict("flashmethod","subarucan")) {
        a.state=HardwareState::Incompatible;
        a.reasons.append("Configured/definition MCU, protocol, RAM address or physical flash layout conflicts with the supported SH7058 profile; these are not newly measured hardware facts");
    }
    if(associated && !d.fields.value("ecuid").isEmpty() && d.fields.value("ecuid")!=FamilyEcu) {
        a.state=HardwareState::Incompatible;a.reasons.append("Target ECU metadata contradicts its reviewed stock-family anchor");
    }
    if(!associated)a.reasons.append("Target family association unresolved: requires the reviewed Alpha AE5T201V document fingerprint as stock leaf or actual ancestor; XML claims/prefixes alone are insufficient");
    if(embedded.isEmpty())a.reasons.append("Target BIN cannot yet be bound to the selected definition's exact embedded identifier");
    if(targetEcu!=FamilyEcu)a.reasons.append("Target ROM ECU-ID bytes do not establish the reviewed AE5T201V layout association; matching XML declarations alone are insufficient");
    const bool observed=id.source==Identification::Source::ObservedDiagnostic && id.generation && id.ecu==FamilyEcu;
    if(!observed)a.reasons.append("No qualifying historical observation of the reviewed ROM ECU identifier; unknown/replayed ECU codes are not independent hardware proof");
    if(a.state!=HardwareState::Incompatible && associated && !embedded.isEmpty() && observed && targetEcu==FamilyEcu && d.fields.value("ecuid")==FamilyEcu) {
        a.state=HardwareState::Compatible;
        a.reasons.append("Observed ROM ECU code and verified target definition map to the reviewed hardware family; physical MCU/layout remain inferred, not independently measured");
    }
    return a;
}
Assessment<IntegrityState> assessIntegrity(const QByteArray &image,const QByteArray &original,const QByteArray &kernel,
        const Definition &d,const Configuration &c,const ReviewedReferences &refs,const QString &embedded,bool &correction) {
    Assessment<IntegrityState> a{IntegrityState::Valid};
    auto invalid=[&](const QString &why){a.state=IntegrityState::Invalid;a.reasons.append(why);};
    auto unsupported=[&](const QString &why){if(a.state!=IntegrityState::Invalid)a.state=IntegrityState::Unsupported;a.reasons.append(why);};
    auto incomplete=[&](const QString &why){if(a.state==IntegrityState::Valid)a.state=IntegrityState::Incomplete;a.reasons.append(why);};
    const auto hash=SubaruSH7058Safety::fingerprint(image);
    a.evidence={{"image_size_bytes",image.size()},{"original_vs_current_hash_match",original.size()==32 && original==hash},
        {"historical_base_bin_hash_match",hash==OriginalReferenceHash},{"historical_hash_required",false},
        {"definition_inheritance_resolved",d.valid},{"definition_document_sha256",strings(d.documentHashes)},
        {"embedded_identifier_matches_definition",!embedded.isEmpty()},{"kernel_sha256",QString::fromLatin1(SubaruSH7058Safety::fingerprint(kernel).toHex())},
        {"kernel_hash_matches_reviewed",refs.kernelHash.size()==32 && SubaruSH7058Safety::fingerprint(kernel)==refs.kernelHash},
        {"checksum_provenance","Production SH7xxx evaluation on a copy; original bytes never repaired"}};
    if(image.size()!=int(SubaruSH7058Safety::ImageSize))invalid("Target must be exactly 1,048,576 bytes; no padding or truncation");
    if(original.size()!=32 || original!=hash)invalid("Target changed after selection or original SHA-256 is missing; no substitution accepted");
    if(!supportedConfiguration(c) || c.checksumEnabled!="yes")unsupported("Requires exact supported SH7058 protocol/geometry/RAM configuration and enabled checksum");
    if(kernel.isEmpty())incomplete("Selected kernel unavailable");
    else if(refs.kernelHash.size()!=32 || SubaruSH7058Safety::fingerprint(kernel)!=refs.kernelHash)invalid("Selected kernel differs from the reviewed bundled binary");
    if(image.size()==int(SubaruSH7058Safety::ImageSize) && c.mcu=="SH7058") {
        const auto checksum=SubaruSH7058Safety::prepare(image,c.mcu,true,c.checksumEnabled=="yes");
        correction=checksum.state==ChecksumEcuSubaruDensoSH7xxx::State::Corrected;
        const bool valid=checksum.state==ChecksumEcuSubaruDensoSH7xxx::State::Valid && SubaruSH7058Safety::preservesInput(checksum,image);
        a.evidence["checksum_valid_and_preserved"]=valid;
        if(checksum.state==ChecksumEcuSubaruDensoSH7xxx::State::Disabled)unsupported("Checksum table disabled; validity not established");
        else if(c.checksumEnabled=="yes" && !valid)invalid("Checksum invalid or correction proposed; tuner bytes preserved for separate review");
    }
    if(!d.valid)incomplete("Definition/inheritance unresolved: "+d.error);
    for(const auto *key:{"xmlid","internalidstring","internalidaddress","ecuid","memmodel","flashmethod","checksummodule"})
        if(d.fields.value(key).isEmpty())incomplete(QString("Missing target definition field: ")+key);
    if(d.valid && (d.xmlId.isEmpty() || d.fields.value("xmlid")!=d.xmlId))invalid("Selected XML ID conflicts with resolved target metadata");
    const auto f=d.fields;
    if(f.value("filesize").isEmpty()) {
        if(reviewedAssociation(d,refs))a.evidence["image_size_metadata_provenance"]="reviewed_AE5T201V_SH7058_profile_1048576_bytes";
        else incomplete("XML filesize absent and target has no reviewed family size association");
    } else a.evidence["image_size_metadata_provenance"]="definition";
    for(const auto &pair:QVector<QPair<QString,QString>>{{"memmodel","SH7058"},{"flashmethod","subarucan"},{"checksummodule","subarudbw"}})
        if(!f.value(pair.first).isEmpty() && f.value(pair.first)!=pair.second)unsupported("Unsupported target definition "+pair.first+": "+f.value(pair.first));
    if(!f.value("filesize").isEmpty() && f.value("filesize")!="1048576" && f.value("filesize").toUpper()!="1024KB")invalid("Target XML size conflicts with the required image dimensions");
    if(!f.value("internalidstring").isEmpty() && !f.value("internalidaddress").isEmpty() && embedded.isEmpty())invalid("Target's exact declared identifier does not match BIN bytes at the definition address");
    if(a.state==IntegrityState::Valid)a.reasons.append("Target passes independent size, immutable SHA, checksum, resolved definition/embedded-ID, configuration and reviewed-kernel checks; historical Base.bin equality is unnecessary");
    return a;
}
Assessment<TransitionState> assessTransition(const Identification &id,const QString &embedded,const QString &targetCal) {
    Assessment<TransitionState> a{TransitionState::Unknown};
    a.evidence={{"observation_source",observationName(id.source)},{"installed_full_variant_known",!id.firmware.isEmpty()},
        {"target_obd_cal_id_provenance",targetCal.isEmpty()?"not_established_for_this_target":"derived_from_pinned_Base.bin_09_04_handler"},
        {"identity_aliasing_permitted",false}};
    if(id.source==Identification::Source::Missing || (id.ecu.isEmpty() && id.calibration.isEmpty() && id.firmware.isEmpty()) || embedded.isEmpty()) {
        a.reasons.append("Installed observation or verified target identifier unavailable; transition unknown");return a;
    }
    if(!id.firmware.isEmpty()) {
        a.state=id.firmware==embedded?TransitionState::SameComparableIdentity:TransitionState::DifferentComparableIdentity;
        a.evidence["comparison_kind"]="full_firmware_identifier";
        a.reasons.append(id.firmware==embedded?"Comparable full identifiers match; this does not prove byte-identical calibration":"Installed and target full identifiers differ: firmware transition, not a hardware verdict");
    } else if(!id.calibration.isEmpty() && !targetCal.isEmpty() && id.calibration!=targetCal) {
        a.state=TransitionState::DifferentComparableIdentity;a.evidence["comparison_kind"]="obd_cal_id_only";
        a.reasons.append("Comparable OBD CAL-IDs differ; installed full variant remains unknown and hardware is assessed separately");
    } else {
        a.state=TransitionState::PartiallyKnown;a.evidence["comparison_kind"]="partial_identity_only";
        if(!targetCal.isEmpty())a.evidence["obd_cal_id_matches"]=id.calibration==targetCal;
        a.reasons.append("Installed full firmware variant is unknown; short OBD CAL-ID and XML ancestry are not substitutes for a complete custom identifier");
    }
    if(id.source==Identification::Source::OfflineReplay)a.reasons.append("Transition values are an offline replay, not an observed ECU");
    return a;
}
Assessment<ReadinessState> assessReadiness(const QByteArray &target,const QByteArray &targetHash,const Configuration &c,
        const Identification &id,const CurrentImageEvidence &current,QVector<unsigned> &required,bool &known) {
    Assessment<ReadinessState> a{ReadinessState::SectorRequirementsUnknown};
    a.evidence={{"programming_allowed",false},{"sector_requirements","unknown"},
        {"sector_policy_status","006N review proposal only; no erase/selection policy enabled"},
        {"comparison_provenance",current.source==CurrentImageEvidence::Source::QualifiedEcuCapture?"qualified_ecu_capture_supplied_by_caller":current.source==CurrentImageEvidence::Source::OfflineReference?"offline_reference":"unavailable"}};
    a.reasons.append("Production upload/write/test-write, legacy ROM Read, live candidate and voltage admission remain disabled");
    if(current.source!=CurrentImageEvidence::Source::QualifiedEcuCapture || id.source!=Identification::Source::ObservedDiagnostic ||
        !id.generation || current.observationGeneration!=id.generation || id.ecu.isEmpty() ||
        id.observationContext.isNull() || current.observationContext!=id.observationContext) {
        a.reasons.append("No context/generation-bound qualified current-image evidence; target alone or an offline reference cannot establish required sectors");return a;
    }
    if(target.size()!=int(SubaruSH7058Safety::ImageSize) || current.bytes.size()!=int(SubaruSH7058Safety::ImageSize) ||
        targetHash.size()!=32 || SubaruSH7058Safety::fingerprint(target)!=targetHash || current.capturedHash.size()!=32 ||
        SubaruSH7058Safety::fingerprint(current.bytes)!=current.capturedHash || !SubaruSH7058Safety::validProgrammingMap(c.map)) {
        a.state=ReadinessState::InvalidComparison;a.reasons.append("Current/target image dimensions, immutable hash or physical comparison map invalid");return a;
    }
    // Complete read-only comparison, not an erase plan: never omit protected
    // differences or pass a selected subset to a programming routine.
    bool protectedChange=false;
    for(unsigned i=0;i<c.map->numblocks;++i) {
        const auto &b=c.map->fblocks[i];
        if(current.bytes.mid(int(b.start),int(b.len))!=target.mid(int(b.start),int(b.len))) {
            required.append(i);if(i<=8 || i==15)protectedChange=true;
        }
    }
    known=true;a.evidence["sector_requirements"]="known_from_context_and_generation_bound_complete_comparison";
    a.evidence["observation_context"]=id.observationContext.toString(QUuid::WithoutBraces);
    a.evidence["current_image_sha256"]=QString::fromLatin1(current.capturedHash.toHex());
    a.evidence["target_sha256"]=QString::fromLatin1(targetHash.toHex());
    a.state=protectedChange?ReadinessState::ProtectedChangesUnsupported:ReadinessState::ProgrammingDisabled;
    a.reasons.append(protectedChange?"Required differences include sectors 0-8 or 15, unqualified under the 006N preservation proposal; no partial-target success claim":"Complete byte comparison recorded; no programming implementation is admitted even if no protected sectors differ");
    return a;
}
}
void Compatibility::recordFileRecheck(const QByteArray &hash) {
    integrity.evidence["file_sha256_after"]=QString::fromLatin1(hash.toHex());
    integrity.evidence["file_unchanged"]=hash.size()==32 && hash==originalHash && hash==finalHash;
    if(hash.size()!=32 || hash!=originalHash || hash!=finalHash) {
        integrity.state=IntegrityState::Invalid;integrity.reasons.prepend("Target file changed, was replaced or became unreadable after selection; reload required");
        sectorsKnown=false;requiredSectors.clear();readiness.state=ReadinessState::InvalidComparison;
        readiness.evidence["sector_requirements"]="unknown";readiness.reasons.prepend("Target file identity lost; any sector comparison is retired");
    }
}
QJsonObject Compatibility::json() const {
    QJsonArray sectors;for(const auto v:requiredSectors)sectors.append(int(v));
    auto operation=assessmentJson(readiness,readinessName(readiness.state));
    operation["required_sectors_known"]=sectorsKnown;operation["required_sectors"]=sectors;
    return {{"schema","fastecu.sh7058-preflight.v2"},
        {"hardware_compatibility",assessmentJson(hardware,hardwareName(hardware.state))},
        {"target_integrity",assessmentJson(integrity,integrityName(integrity.state))},
        {"firmware_transition",assessmentJson(transition,transitionName(transition.state))},
        {"flash_operation_readiness",operation},{"identifiers",identifiers},
        {"original_sha256",QString::fromLatin1(originalHash.toHex())},{"final_sha256",QString::fromLatin1(finalHash.toHex())},
        {"input_preserved",originalHash.size()==32 && originalHash==finalHash},
        {"checksum_correction_required",checksumCorrectionRequired},{"programming_allowed",false}};
}
QString Compatibility::summary() const {
    QString text="Hardware compatibility: "+hardwareName(hardware.state).replace('_',' ')+"\nTarget BIN integrity: "+integrityName(integrity.state).replace('_',' ')+
        "\nFirmware transition: "+transitionName(transition.state).replace('_',' ')+"\nFlash-operation readiness: "+readinessName(readiness.state).replace('_',' ')+"\n\n";
    for(const auto &pair:QVector<QPair<QString,QString>>{{"Installed ECU ID","observed_ecu_identifier"},{"Installed OBD CAL-ID","observed_obd_cal_id"},
        {"Installed full firmware ID","observed_full_firmware_id"},{"Target embedded ID","target_embedded_identifier"},{"Target XML internal ID","target_xml_internal_identifier"},{"Target definition","target_definition_id"}}) {
        const auto v=identifiers.value(pair.second).toString();text+=pair.first+": "+(v.isEmpty()?"unknown":v)+"\n";
    }
    text+="\nHardware facts and target integrity are assessed separately from firmware identity differences. Historical observations do not establish the currently connected ECU.\n";
    if(transition.state==TransitionState::DifferentComparableIdentity)text+="Firmware transition detected. No hardware incompatibility is inferred solely from an identifier difference.\n";
    if(identifiers.value("observed_full_firmware_id").toString().isEmpty())text+="Installed full firmware variant is unknown.\n";
    for(const auto &reasons:{hardware.reasons,integrity.reasons,transition.reasons,readiness.reasons})
        if(!reasons.isEmpty())text+="\n- "+reasons.first();
    text+="\n\nDetails contain all reasons, evidence provenance and JSON.";
    return text+"\n\nProgramming remains disabled. This report is not an authorization token.";
}
Compatibility checkCompatibility(const QByteArray &image,const QByteArray &original,const QByteArray &kernel,const Definition &d,
        const Configuration &c,const Identification &observed,const ReviewedReferences &refs,const CurrentImageEvidence &current) {
    Compatibility r;r.originalHash=original;r.finalHash=SubaruSH7058Safety::fingerprint(image);
    const auto embedded=embeddedId(image,d);
    // Only the actual reviewed original image establishes this target OBD CAL-ID.
    // No new target's CAL-ID is derived by truncating its custom ID or its parent.
    const QString targetCal=r.finalHash==OriginalReferenceHash?QString("ZE5T201V"):QString();
    r.identifiers={{"observed_ecu_identifier",observed.ecu},{"observed_obd_cal_id",observed.calibration},
        {"observed_full_firmware_id",observed.firmware},{"target_embedded_identifier",embedded},
        {"target_xml_internal_identifier",d.fields.value("internalidstring")},{"target_definition_id",d.xmlId},
        {"target_obd_cal_id",targetCal},{"target_definition_ancestry",strings(d.ancestry)}};
    r.integrity=assessIntegrity(image,original,kernel,d,c,refs,embedded,r.checksumCorrectionRequired);
    const auto targetEcu=image.size()==int(SubaruSH7058Safety::ImageSize)?QString::fromLatin1(image.mid(0xe10f0,5).toHex().toUpper()):QString();
    r.hardware=assessHardware(d,c,observed,refs,embedded,targetEcu);
    r.transition=assessTransition(observed,embedded,targetCal);
    r.readiness=assessReadiness(image,original,c,observed,current,r.requiredSectors,r.sectorsKnown);
    if(r.hardware.state!=HardwareState::Compatible)r.readiness.reasons.append("Hardware-family assessment is not compatible with reviewed evidence");
    if(r.integrity.state!=IntegrityState::Valid)r.readiness.reasons.append("Target integrity assessment is not valid");
    return r;
}
}
