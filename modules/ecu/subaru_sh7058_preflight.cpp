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
        d.documentHashes.append(QString::fromLatin1(SubaruSH7058Safety::fingerprint(bytes).toHex()));
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
QJsonObject Compatibility::json() const {
    static const char *names[]={"compatible_evidence_only","confirmed_incompatible","missing_identity_or_definition","unsupported_configuration","requires_hardware_qualification"};
    QJsonArray why; for (const auto &s:reasons) why.append(s);
    return {{"result",names[int(state)]},{"original_sha256",QString::fromLatin1(originalHash.toHex())},
        {"final_sha256",QString::fromLatin1(finalHash.toHex())},{"input_preserved",originalHash==finalHash},
        {"evidence",evidence},{"checksum_correction_required",checksumCorrectionRequired},{"programming_allowed",false},{"reasons",why}};
}
Compatibility checkCompatibility(const QByteArray &image, const QByteArray &expectedOriginal,
        const QByteArray &kernel, const Definition &d, const Configuration &c, const Identification &observed, const Target &t) {
    Compatibility r; r.originalHash=expectedOriginal; r.finalHash=SubaruSH7058Safety::fingerprint(image);
    auto fail=[&](Compatibility::State state,const QString &why) { r.state=state; r.reasons.append(why); return r; };
    using State=Compatibility::State;
    if (c.protocol!="sub_ecu_denso_sh7058_can" || c.mcu!="SH7058" || c.checksumEnabled!="yes" || c.transport!="iso15765" ||
        !SubaruSH7058Safety::validProgrammingMap(c.map) || QString::fromLatin1(c.map->name)!=c.mcu || c.kernelAddress!=0xffff3000u)
        return fail(State::Unsupported,"Requires exact subarucan SH7058/16-sector map, enabled checksum and reviewed kernel RAM address");
    if (image.size()!=1048576 || expectedOriginal.size()!=32 || expectedOriginal!=r.finalHash || r.finalHash!=t.imageHash)
        return fail(State::Incompatible,"BIN size or original/reference SHA-256 mismatch; no image changes applied");
    if (kernel.isEmpty()) return fail(State::MissingEvidence,"Selected kernel unavailable; no compatibility inferred");
    if (SubaruSH7058Safety::fingerprint(kernel)!=t.kernelHash)
        return fail(State::Incompatible,"Selected kernel is not the pinned reviewed binary");
    const auto checksum=SubaruSH7058Safety::prepare(image,c.mcu,true,true);
    r.checksumCorrectionRequired=checksum.state==ChecksumEcuSubaruDensoSH7xxx::State::Corrected;
    if (checksum.state==ChecksumEcuSubaruDensoSH7xxx::State::Disabled)
        return fail(State::Unsupported,"Checksum table disabled; validity not established");
    if (checksum.state!=ChecksumEcuSubaruDensoSH7xxx::State::Valid || !SubaruSH7058Safety::preservesInput(checksum,image))
        return fail(State::Incompatible,"Checksum invalid/disabled or correction proposed; original BIN retained for separate review");
    if ((!observed.ecu.isEmpty() && observed.ecu!=t.ecu) || (!observed.calibration.isEmpty() && observed.calibration!=t.obdCalibration) ||
        (!observed.firmware.isEmpty() && observed.firmware!=t.calibration)) return fail(State::Incompatible,"Observed/replayed identifiers mismatch; no prefix or parent equivalence accepted");
    r.evidence["checksum_valid_and_preserved"]=true;
    r.evidence["xml_inheritance_resolved"]=d.valid;
    for (const auto &key:{"xmlid","internalidstring","internalidaddress","ecuid","memmodel","flashmethod","checksummodule","filesize"})
        if (d.fields.value(key).isEmpty()) return fail(State::MissingEvidence,QString("Missing XML identity field: ")+key);
    const auto f=d.fields;
    if (d.xmlId!=t.calibration || f["xmlid"]!=t.calibration || (d.valid && (d.ancestry.size()<2 || !d.ancestry.mid(1).contains(t.parent))) || f["internalidstring"]!=t.calibration ||
        f["ecuid"]!=t.ecu || f["memmodel"]!="SH7058" || f["flashmethod"]!="subarucan" || f["checksummodule"]!="subarudbw" ||
        (f["filesize"]!="1048576" && f["filesize"].toUpper()!="1024KB"))
        return fail(State::Incompatible,"Exact XML identity/parent/MCU/flash method/checksum module/size mismatch; parent IDs are not firmware aliases");
    bool ok=false; QString address=f["internalidaddress"]; if (address.startsWith("0x",Qt::CaseInsensitive)) address=address.mid(2);
    const auto offset=address.toULongLong(&ok,16); const auto id=t.calibration.toLatin1();
    if (!ok || offset>quint64(image.size()) || quint64(id.size())>quint64(image.size())-offset || image.mid(int(offset),id.size())!=id)
        return fail(State::Incompatible,"Full embedded BIN identifier does not match the XML's explicit address");
    r.evidence["full_embedded_id_at_xml_address"]=true;
    r.evidence["leaf_identity_metadata_match"]=true;
    if (!d.valid) return fail(State::MissingEvidence,"Definition/inheritance unresolved: "+d.error);
    // Pinning the full image binds the XML-specified identifier to this reviewed
    // reference. No raw ECU-ID location is guessed from ASCII/hex searches.
    if (observed.ecu.isEmpty() || observed.calibration.isEmpty()) return fail(State::MissingEvidence,"No complete observed ECU identity; offline BIN/XML match is not ECU compatibility");
    r.evidence["ecu_and_obd_calibration_match"]=true;
    if (observed.firmware.isEmpty()) return fail(State::MissingEvidence,"OBD CAL-ID matches the baseline only; full custom firmware variant was not observed. No parent/prefix equivalence inferred");
    if (observed.firmware!=t.calibration) return fail(State::Incompatible,"Observed full firmware variant differs from the reference");
    if (observed.source!=Identification::Source::ObservedDiagnostic || !observed.generation)
        return fail(State::HardwareQualificationRequired,"Identity is inferred/offline replay, not a qualified live observation");
    return fail(State::CompatibleEvidence,"Exact available identity/BIN/XML/kernel evidence matches; hardware/kernel qualification and programming admission remain separate and disabled");
}
}
