#include <QtTest>
#include "../../modules/ecu/subaru_sh7058_preflight.h"
using namespace SubaruSH7058;
namespace {
QByteArray ea() { return QByteArray::fromHex("000007e8eaa21011a62a907107")+QByteArray(96,0); }
QByteArray cal() { return QByteArray::fromHex("000007e8490401")+"ZE5T201V"+QByteArray(8,0); }
CommunicationResult success(QByteArray bytes={}) { CommunicationResult r;r.state=CommunicationResult::State::Success;r.hasStatus=true;r.bytes=bytes;return r; }
struct Clock : IdentificationClock { qint64 ms=0;qint64 nowMs() const noexcept override {return ms;} };
struct Link : IdentificationLink {
    bool offline=true,acquireOk=true,releaseOk=true;quint64 epoch=9;int acquires=0,releases=0,reads=0;
    QList<QByteArray> sent;QString fault;Clock *clock=nullptr;std::atomic<bool> *cancel=nullptr;
    bool offlineFixture() const noexcept override {return offline;}
    bool acquire() override {++acquires;if(fault=="late-acquire")clock->ms+=100;return acquireOk;}
    quint64 generation() const noexcept override {return epoch;}
    bool release() noexcept override {++releases;epoch=0;return releaseOk;}
    CommunicationResult submit(const QByteArray &bytes) override {
        sent.append(bytes);
        if(fault=="exception")throw 1;
        if(fault=="generation")++epoch;
        if(fault=="cancel")cancel->store(true);
        if(fault=="late-submit")clock->ms+=100;
        auto r=success();
        if(fault=="unknown-status")r.hasStatus=false;
        if(fault=="write-status")r.status=7;
        return r;
    }
    CommunicationResult receive(uint16_t wait) override {
        ++reads;
        if(fault=="late-queue")clock->ms+=100;
        if(!wait)return fault=="queued"?success(ea()):CommunicationResult::failure(CommunicationResult::State::Timeout,"empty");
        if(fault=="timeout")return CommunicationResult::failure(CommunicationResult::State::Timeout,"timeout");
        if(fault=="disconnect")return CommunicationResult::failure(CommunicationResult::State::Disconnected,"lost");
        if(fault=="late")clock->ms+=100;
        if(fault=="malformed")return success(QByteArray::fromHex("000007e8ea"));
        return success(sent.size()==1?ea():cal());
    }
};
void be32(QByteArray &b,int offset,uint32_t n){for(int i=3;i>=0;--i){b[offset+i]=char(n);n>>=8;}}
QByteArray image(const QString &id="ZE5T201V_DM24000") {
    QByteArray b(1048576,0);b.replace(0xe10f0,5,QByteArray::fromHex("a62a907107"));const auto bytes=id.toLatin1();b.replace(0x2004,bytes.size(),bytes);uint32_t sum=0;
    for(int i=0x2000;i<0x2100;i+=4){uint32_t word=0;for(int j=0;j<4;++j)word=(word<<8)|uint8_t(b[i+j]);sum+=word;}
    for(int i=0;i<17;++i){be32(b,0xffb80+i*12,0x2000);be32(b,0xffb84+i*12,0x2100);be32(b,0xffb88+i*12,0x5aa5a55a-sum);}return b;
}
QVector<QByteArray> documents(const QString &id="ZE5T201V_DM24000") {
    const auto stock=QByteArray("<rom><include>32BITBASE</include><romid><xmlid>AE5T201V</xmlid><internalidstring>AE5T201V</internalidstring><internalidaddress>2004</internalidaddress><ecuid>A62A907107</ecuid><memmodel>SH7058</memmodel><flashmethod>subarucan</flashmethod><checksummodule>subarudbw</checksummodule></romid></rom>");
    const auto base=QByteArray("<rom><romid><xmlid>32BITBASE</xmlid><internalidaddress>2000</internalidaddress></romid></rom>");
    if(id=="AE5T201V")return {stock,base};
    const auto child=QString("<rom><include>AE5T201V</include><romid><xmlid>%1</xmlid><internalidstring>%1</internalidstring><internalidaddress>2004</internalidaddress><ecuid>A62A907107</ecuid><filesize>1024kb</filesize></romid></rom>").arg(id).toUtf8();
    return {child,stock,base};
}
ReviewedReferences references() {ReviewedReferences refs;refs.stockDefinitionHash=SubaruSH7058Safety::fingerprint(documents("AE5T201V").first());refs.kernelHash=SubaruSH7058Safety::fingerprint("offline synthetic kernel");return refs;}
Identification observed(const QString &full="ZE5T201V_DM24000") {Identification id;id.ecu="A62A907107";id.calibration="ZE5T201V";id.firmware=full;id.source=Identification::Source::ObservedDiagnostic;id.generation=9;id.observationContext=QUuid("12345678-1234-1234-1234-123456789abc");return id;}
Configuration configuration() { Configuration c;c.protocol="sub_ecu_denso_sh7058_can";c.transport="iso15765";c.mcu="SH7058";c.checksumEnabled="yes";c.map=SubaruSH7058Safety::memoryMap(c.mcu);c.kernelAddress=0xffff3000u;return c; }
}
class PreflightTest:public QObject {
    Q_OBJECT
private slots:
    void requestWhitelist(){QCOMPARE(identityRequest(false),QByteArray::fromHex("000007e0aa"));QCOMPARE(identityRequest(true),QByteArray::fromHex("000007e00904"));}
    void parseExact(){QString id,error;QVERIFY(parseIdentityReply(false,ea(),id,error));QCOMPARE(id,QString("A62A907107"));QVERIFY(parseIdentityReply(true,cal(),id,error));QCOMPARE(id,QString("ZE5T201V"));}
    void parseRejected_data(){QTest::addColumn<QByteArray>("bytes");QTest::addColumn<bool>("calibration");QTest::newRow("truncated-ecu")<<ea().left(12)<<false;QTest::newRow("wrong-address")<<(QByteArray::fromHex("000007e9")+cal().mid(4))<<true;QTest::newRow("negative")<<QByteArray::fromHex("000007e87f0911")<<true;QTest::newRow("pending")<<QByteArray::fromHex("000007e87f0978")<<true;QTest::newRow("multiple-records")<<(cal()+cal().mid(7))<<true;QTest::newRow("wrong-pid")<<QByteArray::fromHex("000007e8490201")+QByteArray(16,'A')<<true;QTest::newRow("embedded-zero")<<(QByteArray::fromHex("000007e8490401")+QByteArray("ZE\0T201V_DM24000",16))<<true;}
    void parseRejected(){QFETCH(QByteArray,bytes);QFETCH(bool,calibration);QString id="old",error;QVERIFY(!parseIdentityReply(calibration,bytes,id,error));QVERIFY(id.isEmpty());QVERIFY(!error.isEmpty());}
    void liveUnavailable(){UnavailableIdentificationLink blocked;std::atomic<bool> cancel{false};const auto r=identify(blocked,cancel,100);QVERIFY(!r.complete);QVERIFY(!r.acquired);QVERIFY(r.detail.contains("No device acquired"));Link link;link.offline=false;identify(link,cancel,100);QCOMPARE(link.acquires,0);QVERIFY(link.sent.isEmpty());}
    void lifecycle(){Link link;Clock clock;link.clock=&clock;std::atomic<bool> cancel{false};const auto r=identify(link,cancel,100,&clock);QVERIFY(r.complete);QVERIFY(r.cleanupConfirmed);QCOMPARE(link.releases,1);QCOMPARE(link.sent,QList<QByteArray>({identityRequest(false),identityRequest(true)}));QCOMPARE(r.identity.source,Identification::Source::OfflineReplay);QCOMPARE(r.identity.ecuReply,ea());QVERIFY(!r.identity.observationContext.isNull());}
    void failure_data(){QTest::addColumn<QString>("fault");for(const char *s:{"queued","generation","cancel","unknown-status","write-status","timeout","disconnect","late","late-submit","late-acquire","late-queue","malformed","exception"})QTest::newRow(s)<<QString(s);}
    void failure(){QFETCH(QString,fault);Link link;Clock clock;std::atomic<bool> cancel{false};link.fault=fault;link.clock=&clock;link.cancel=&cancel;const auto r=identify(link,cancel,100,&clock);QVERIFY(!r.complete);QVERIFY(!r.detail.isEmpty());QCOMPARE(link.releases,1);QVERIFY(link.sent.size()<=1);}
    void ownershipAndCleanup(){std::atomic<bool> cancel{false};Link busy;busy.acquireOk=false;QVERIFY(!identify(busy,cancel,100).complete);QCOMPARE(busy.releases,0);QVERIFY(busy.sent.isEmpty());Link bad;bad.releaseOk=false;QVERIFY(!identify(bad,cancel,100).complete);QCOMPARE(bad.releases,1);Link stopped;cancel=true;identify(stopped,cancel,100);QCOMPARE(stopped.acquires,0);}
    void compatibility_data(){
        QTest::addColumn<QString>("fault");QTest::addColumn<int>("hardware");QTest::addColumn<int>("integrity");
        for(const auto *f:{"exact","missing-observed","replay","unknown-ecu","bin-modified","original-changed","short","checksum","disabled-checksum","kernel","map","protocol","mcu","checksum-config","xml-missing","xml-mismatch","xml-address","xml-ambiguous","missing-kernel","raw-can","unreviewed-family","xml-claims-only"}) {
            const QString v=f;auto h=HardwareState::Compatible;auto i=IntegrityState::Valid;
            if(v=="missing-observed"||v=="replay"||v=="unknown-ecu"||v=="xml-missing"||v=="xml-address"||v=="xml-ambiguous"||v=="unreviewed-family"||v=="xml-claims-only"||v=="short")h=HardwareState::InsufficientEvidence;
            if(v=="map"||v=="protocol"||v=="mcu"||v=="raw-can"||v=="xml-mismatch")h=HardwareState::Incompatible;
            if(v=="bin-modified"||v=="original-changed"||v=="short"||v=="checksum"||v=="kernel"||v=="xml-address")i=IntegrityState::Invalid;
            if(v=="map"||v=="protocol"||v=="mcu"||v=="raw-can"||v=="checksum-config"||v=="disabled-checksum")i=IntegrityState::Unsupported;
            if(v=="xml-missing"||v=="xml-ambiguous"||v=="missing-kernel")i=IntegrityState::Incomplete;
            QTest::newRow(f)<<v<<int(h)<<int(i);
        }
    }
    void compatibility(){
        QFETCH(QString,fault);QFETCH(int,hardware);QFETCH(int,integrity);
        auto b=image();auto original=SubaruSH7058Safety::fingerprint(b);auto kernel=QByteArray("offline synthetic kernel");auto c=configuration();auto docs=documents();auto id=observed();
        if(fault=="missing-observed")id=Identification();
        if(fault=="replay")id.source=Identification::Source::OfflineReplay;
        if(fault=="unknown-ecu")id.ecu="A62A907108";
        if(fault=="bin-modified")b[0x3000]=1;
        if(fault=="original-changed")original[0]=char(original[0]^1);
        if(fault=="short")b.chop(1);
        if(fault=="checksum"){b[0xffb88]=char(b[0xffb88]^1);original=SubaruSH7058Safety::fingerprint(b);}
        if(fault=="disabled-checksum"){be32(b,0xffb80,0);be32(b,0xffb84,0);be32(b,0xffb88,0x5aa5a55a);original=SubaruSH7058Safety::fingerprint(b);}
        if(fault=="missing-kernel")kernel.clear();
        if(fault=="kernel")kernel+="changed";
        if(fault=="map")c.map=SubaruSH7058Safety::memoryMap("SH7058_1block");
        if(fault=="raw-can")c.transport="CAN";
        if(fault=="protocol")c.protocol="sub_ecu_denso_sh7058_can_ecutek";
        if(fault=="mcu")c.mcu="SH7059";
        if(fault=="checksum-config")c.checksumEnabled="no";
        if(fault=="xml-missing")docs.clear();
        if(fault=="xml-mismatch")docs[0].replace("A62A907107","A62A907108");
        if(fault=="xml-address")docs[0].replace("2004</internalidaddress>","FFFFFFFF</internalidaddress>");
        if(fault=="xml-ambiguous")docs.append(docs[0]);
        if(fault=="xml-claims-only"){b.replace(0xe10f0,5,QByteArray(5,0));original=SubaruSH7058Safety::fingerprint(b);}
        if(fault=="unreviewed-family")docs[1].append("<!-- same claims, unreviewed document -->");
        const auto before=b;const auto d=resolveDefinition(docs,"ZE5T201V_DM24000");const auto r=checkCompatibility(b,original,kernel,d,c,id,references());
        QCOMPARE(int(r.hardware.state),hardware);QCOMPARE(int(r.integrity.state),integrity);QCOMPARE(b,before);QVERIFY(!r.programmingAllowed());QVERIFY(!r.sectorsKnown);
        QVERIFY(!r.hardware.reasons.isEmpty());QVERIFY(!r.integrity.reasons.isEmpty());QVERIFY(!r.transition.reasons.isEmpty());QVERIFY(!r.readiness.reasons.isEmpty());
        if(fault=="checksum")QVERIFY(r.checksumCorrectionRequired);
        const auto json=r.json();for(const auto *key:{"hardware_compatibility","target_integrity","firmware_transition","flash_operation_readiness"})QVERIFY(!json[key].toObject().isEmpty());
    }
    void transition_data(){
        QTest::addColumn<QString>("source");QTest::addColumn<QString>("target");QTest::addColumn<int>("transition");
        QTest::newRow("same")<<QString("ZE5T201V_DM24000")<<QString("ZE5T201V_DM24000")<<int(TransitionState::SameComparableIdentity);
        QTest::newRow("stock-to-dimemod")<<QString("AE5T201V")<<QString("ZE5T201V_DM24000")<<int(TransitionState::DifferentComparableIdentity);
        QTest::newRow("dimemod-to-stock")<<QString("ZE5T201V_DM24000")<<QString("AE5T201V")<<int(TransitionState::DifferentComparableIdentity);
        QTest::newRow("updated-custom")<<QString("ZE5T201V_DM24000")<<QString("ZE5T201V_DM25000")<<int(TransitionState::DifferentComparableIdentity);
        QTest::newRow("changed-tuner-id")<<QString("ZE5T201V_DM24000")<<QString("REVIEWED_TUNER_V2")<<int(TransitionState::DifferentComparableIdentity);
        QTest::newRow("full-variant-unavailable")<<QString()<<QString("ZE5T201V_DM24000")<<int(TransitionState::PartiallyKnown);
    }
    void transition(){
        QFETCH(QString,source);QFETCH(QString,target);QFETCH(int,transition);
        const auto b=image(target),before=b;const auto d=resolveDefinition(documents(target),target);const auto r=checkCompatibility(b,SubaruSH7058Safety::fingerprint(b),"offline synthetic kernel",d,configuration(),observed(source),references());
        QCOMPARE(r.hardware.state,HardwareState::Compatible);QCOMPARE(r.integrity.state,IntegrityState::Valid);QCOMPARE(int(r.transition.state),transition);QCOMPARE(b,before);QVERIFY(!r.programmingAllowed());
        QCOMPARE(r.identifiers["observed_full_firmware_id"].toString(),source);QCOMPARE(r.identifiers["target_embedded_identifier"].toString(),target);
        if(target=="AE5T201V")QCOMPARE(r.integrity.evidence["image_size_metadata_provenance"].toString(),QString("reviewed_AE5T201V_SH7058_profile_1048576_bytes"));
        QVERIFY(r.summary().contains("Programming remains disabled"));
    }
    void newHashAndFileReplacement(){
        auto b=image();const auto first=SubaruSH7058Safety::fingerprint(b);b[0xc0123]=1;const auto second=SubaruSH7058Safety::fingerprint(b);QVERIFY(first!=second);
        const auto before=b;auto r=checkCompatibility(b,second,"offline synthetic kernel",resolveDefinition(documents(),"ZE5T201V_DM24000"),configuration(),observed(),references());
        QCOMPARE(r.integrity.state,IntegrityState::Valid);QVERIFY(!r.integrity.evidence["historical_hash_required"].toBool());r.recordFileRecheck(second);QCOMPARE(r.integrity.state,IntegrityState::Valid);
        r.recordFileRecheck(first);QCOMPARE(r.integrity.state,IntegrityState::Invalid);QVERIFY(r.integrity.reasons.first().contains("reload required"));QCOMPARE(b,before);QVERIFY(!r.programmingAllowed());
        auto unreadable=checkCompatibility(b,second,"offline synthetic kernel",resolveDefinition(documents(),"ZE5T201V_DM24000"),configuration(),observed(),references());unreadable.recordFileRecheck({});QCOMPARE(unreadable.integrity.state,IntegrityState::Invalid);
    }
    void nonAsciiIdentifierIsNotLossilyConverted(){
        const auto label=QString("REVIEWED_")+QChar(0x03a9);const auto b=image(label),before=b;
        const auto r=checkCompatibility(b,SubaruSH7058Safety::fingerprint(b),"offline synthetic kernel",resolveDefinition(documents(label),label),configuration(),observed(),references());
        QCOMPARE(r.integrity.state,IntegrityState::Invalid);QVERIFY(r.identifiers["target_embedded_identifier"].toString().isEmpty());QCOMPARE(b,before);QVERIFY(!r.programmingAllowed());
    }
    void noIdentifierAliases(){
        const auto b=image(),before=b;const auto d=resolveDefinition(documents(),"ZE5T201V_DM24000");
        for(const auto *raw:{"ZE5T201V","AE5T201V","ZE5T"}) {
            auto id=observed(QString());id.calibration=raw;const auto r=checkCompatibility(b,SubaruSH7058Safety::fingerprint(b),"offline synthetic kernel",d,configuration(),id,references());
            QCOMPARE(r.integrity.state,IntegrityState::Valid);QCOMPARE(r.transition.state,TransitionState::PartiallyKnown);QVERIFY(r.identifiers["observed_full_firmware_id"].toString().isEmpty());
            QCOMPARE(r.identifiers["observed_obd_cal_id"].toString(),QString(raw));QVERIFY(r.identifiers["target_obd_cal_id"].toString().isEmpty());QVERIFY(!r.programmingAllowed());
        }
        QCOMPARE(b,before);
    }
    void readiness_data(){QTest::addColumn<QString>("fault");for(const auto *f:{"none","offline-reference","stale-generation","stale-context","capture-mutated","target-mutated","protected-changes","unprotected-changes","no-changes"})QTest::newRow(f)<<QString(f);}
    void readiness(){
        QFETCH(QString,fault);auto b=image();auto id=observed();auto docs=documents();CurrentImageEvidence current;
        if(fault!="none") {current.source=CurrentImageEvidence::Source::QualifiedEcuCapture;current.bytes=b;current.capturedHash=SubaruSH7058Safety::fingerprint(b);current.observationGeneration=id.generation;current.observationContext=id.observationContext;}
        if(fault=="offline-reference")current.source=CurrentImageEvidence::Source::OfflineReference;
        if(fault=="stale-generation")++current.observationGeneration;
        if(fault=="stale-context")current.observationContext=QUuid::createUuid();
        if(fault=="capture-mutated")current.bytes[0x2004]='X';
        if(fault=="protected-changes"){b=image("ZE5T201V_DM25000");docs=documents("ZE5T201V_DM25000");}
        if(fault=="unprotected-changes")b[0xc0123]=1;
        auto hash=SubaruSH7058Safety::fingerprint(b);if(fault=="target-mutated")hash[0]=char(hash[0]^1);
        const auto before=b,oldCapture=current.bytes;const auto target=fault=="protected-changes"?"ZE5T201V_DM25000":"ZE5T201V_DM24000";
        auto r=checkCompatibility(b,hash,"offline synthetic kernel",resolveDefinition(docs,target),configuration(),id,references(),current);
        if(fault=="none"||fault=="offline-reference"||fault=="stale-generation"||fault=="stale-context"){QVERIFY(!r.sectorsKnown);QVERIFY(r.requiredSectors.isEmpty());QCOMPARE(r.readiness.state,ReadinessState::SectorRequirementsUnknown);}
        else if(fault=="capture-mutated"||fault=="target-mutated"){QVERIFY(!r.sectorsKnown);QCOMPARE(r.readiness.state,ReadinessState::InvalidComparison);}
        else {QVERIFY(r.sectorsKnown);
            if(fault=="protected-changes"){QCOMPARE(r.requiredSectors,QVector<unsigned>({2,15}));QCOMPARE(r.readiness.state,ReadinessState::ProtectedChangesUnsupported);QCOMPARE(r.hardware.state,HardwareState::Compatible);QCOMPARE(r.integrity.state,IntegrityState::Valid);}
            else {
                QCOMPARE(int(r.readiness.state),int(ReadinessState::ProgrammingDisabled));
                // Explicit count/index assertions avoid one-element container
                // constructor ambiguity and retain useful older-Qt diagnostics.
                QCOMPARE(r.requiredSectors.size(),fault=="no-changes"?0:1);
                if(fault!="no-changes")QCOMPARE(r.requiredSectors.at(0),14u);
            }
            if(fault=="protected-changes"){r.recordFileRecheck({});QVERIFY(!r.sectorsKnown);QVERIFY(r.requiredSectors.isEmpty());}
        }
        QCOMPARE(b,before);QCOMPARE(current.bytes,oldCapture);QVERIFY(!r.programmingAllowed());QVERIFY(!r.json()["programming_allowed"].toBool());
    }
    void inheritance_data(){QTest::addColumn<QByteArray>("xml");QTest::newRow("cycle")<<QByteArray("<roms><rom base='A'><romid><xmlid>A</xmlid></romid></rom></roms>");QTest::newRow("missing-parent")<<QByteArray("<roms><rom base='B'><romid><xmlid>A</xmlid></romid></rom></roms>");QTest::newRow("DTD")<<QByteArray("<!DOCTYPE roms><roms/>");QTest::newRow("malformed")<<QByteArray("<roms>");}
    void inheritance(){QFETCH(QByteArray,xml);const auto d=resolveDefinition({xml},"A");QVERIFY(!d.valid);QVERIFY(!d.error.isEmpty());}
    void incompleteParentStillReportsIndependentFacts(){
        const auto d=resolveDefinition({documents().first()},"ZE5T201V_DM24000");QVERIFY(!d.valid);QCOMPARE(d.fields.value("ecuid"),QString("A62A907107"));
    }
    void ecuFlashThreeLevelInheritance(){
        const auto d=resolveDefinition(documents(),"ZE5T201V_DM24000");QVERIFY2(d.valid,qPrintable(d.error));
        QCOMPARE(d.ancestry,QStringList({"ZE5T201V_DM24000","AE5T201V","32BITBASE"}));QCOMPARE(d.fields.value("internalidaddress"),QString("2004"));
        QCOMPARE(d.documentHashByXmlId.value("AE5T201V"),references().stockDefinitionHash);
    }
    void romReadAdmission(){QVERIFY(!legacyReadAdmitted());}
};
QTEST_GUILESS_MAIN(PreflightTest)
#include "preflight.moc"
