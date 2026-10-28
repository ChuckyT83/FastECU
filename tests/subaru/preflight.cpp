#include <QtTest>
#include "../../modules/ecu/subaru_sh7058_preflight.h"
using namespace SubaruSH7058;
namespace {
QByteArray ea() { return QByteArray::fromHex("000007e8ea010203a62a907107"); }
QByteArray cal() { return QByteArray::fromHex("000007e8490401")+"ZE5T201V_DM24000"; }
CommunicationResult success(QByteArray bytes={}) { CommunicationResult r;r.state=CommunicationResult::State::Success;r.hasStatus=true;r.bytes=bytes;return r; }
struct Clock : IdentificationClock { qint64 ms=0;qint64 nowMs() const noexcept override {return ms;} };
struct Link : IdentificationLink {
    bool offline=true,acquireOk=true,releaseOk=true;quint64 epoch=9;int acquires=0,releases=0,reads=0;
    QList<QByteArray> sent;QString fault;Clock *clock=nullptr;std::atomic<bool> *cancel=nullptr;
    bool offlineFixture() const noexcept override {return offline;}
    bool acquire() override {++acquires;return acquireOk;}
    quint64 generation() const noexcept override {return epoch;}
    bool release() noexcept override {++releases;epoch=0;return releaseOk;}
    CommunicationResult submit(const QByteArray &bytes) override {
        sent.append(bytes);
        if(fault=="exception")throw 1;
        if(fault=="generation")++epoch;
        if(fault=="cancel")cancel->store(true);
        auto r=success();
        if(fault=="unknown-status")r.hasStatus=false;
        if(fault=="write-status")r.status=7;
        return r;
    }
    CommunicationResult receive(uint16_t wait) override {
        ++reads;
        if(!wait)return fault=="queued"?success(ea()):CommunicationResult::failure(CommunicationResult::State::Timeout,"empty");
        if(fault=="timeout")return CommunicationResult::failure(CommunicationResult::State::Timeout,"timeout");
        if(fault=="disconnect")return CommunicationResult::failure(CommunicationResult::State::Disconnected,"lost");
        if(fault=="late")clock->ms+=100;
        if(fault=="malformed")return success(QByteArray::fromHex("000007e8ea"));
        return success(sent.size()==1?ea():cal());
    }
};
void be32(QByteArray &b,int offset,uint32_t n){for(int i=3;i>=0;--i){b[offset+i]=char(n);n>>=8;}}
QByteArray image() {
    QByteArray b(1048576,0);b.replace(0x2004,16,"ZE5T201V_DM24000");uint32_t sum=0;
    for(int i=0x2000;i<0x2100;i+=4){uint32_t word=0;for(int j=0;j<4;++j)word=(word<<8)|uint8_t(b[i+j]);sum+=word;}
    for(int i=0;i<17;++i){be32(b,0xffb80+i*12,0x2000);be32(b,0xffb84+i*12,0x2100);be32(b,0xffb88+i*12,0x5aa5a55a-sum);}return b;
}
QVector<QByteArray> documents() {return {QByteArray("<roms><rom base='AE5T201V'><romid><xmlid>ZE5T201V_DM24000</xmlid><internalidstring>ZE5T201V_DM24000</internalidstring><internalidaddress>2004</internalidaddress><ecuid>A62A907107</ecuid></romid></rom><rom><romid><xmlid>AE5T201V</xmlid><memmodel>SH7058</memmodel><flashmethod>subarucan</flashmethod><checksummodule>subarudbw</checksummodule><filesize>1024KB</filesize></romid></rom></roms>")};}
Configuration configuration() { Configuration c;c.protocol="sub_ecu_denso_sh7058_can";c.transport="iso15765";c.mcu="SH7058";c.checksumEnabled="yes";c.map=SubaruSH7058Safety::memoryMap(c.mcu);c.kernelAddress=0xffff3000u;return c; }
}
class PreflightTest:public QObject {
    Q_OBJECT
private slots:
    void requestWhitelist(){QCOMPARE(identityRequest(false),QByteArray::fromHex("000007e0aa"));QCOMPARE(identityRequest(true),QByteArray::fromHex("000007e00904"));}
    void parseExact(){QString id,error;QVERIFY(parseIdentityReply(false,ea(),id,error));QCOMPARE(id,QString("A62A907107"));QVERIFY(parseIdentityReply(true,cal(),id,error));QCOMPARE(id,QString("ZE5T201V_DM24000"));}
    void parseRejected_data(){QTest::addColumn<QByteArray>("bytes");QTest::addColumn<bool>("calibration");QTest::newRow("truncated-ecu")<<ea().left(12)<<false;QTest::newRow("wrong-address")<<(QByteArray::fromHex("000007e9")+cal().mid(4))<<true;QTest::newRow("negative")<<QByteArray::fromHex("000007e87f0911")<<true;QTest::newRow("pending")<<QByteArray::fromHex("000007e87f0978")<<true;QTest::newRow("multiple-records")<<(cal()+cal().mid(7))<<true;QTest::newRow("wrong-pid")<<QByteArray::fromHex("000007e8490201")+QByteArray(16,'A')<<true;QTest::newRow("embedded-zero")<<(QByteArray::fromHex("000007e8490401")+QByteArray("ZE\0T201V_DM24000",16))<<true;}
    void parseRejected(){QFETCH(QByteArray,bytes);QFETCH(bool,calibration);QString id="old",error;QVERIFY(!parseIdentityReply(calibration,bytes,id,error));QVERIFY(id.isEmpty());QVERIFY(!error.isEmpty());}
    void liveUnavailable(){UnavailableIdentificationLink blocked;std::atomic<bool> cancel{false};const auto r=identify(blocked,cancel,100);QVERIFY(!r.complete);QVERIFY(!r.acquired);QVERIFY(r.detail.contains("No device acquired"));Link link;link.offline=false;identify(link,cancel,100);QCOMPARE(link.acquires,0);QVERIFY(link.sent.isEmpty());}
    void lifecycle(){Link link;Clock clock;link.clock=&clock;std::atomic<bool> cancel{false};const auto r=identify(link,cancel,100,&clock);QVERIFY(r.complete);QVERIFY(r.cleanupConfirmed);QCOMPARE(link.releases,1);QCOMPARE(link.sent,QList<QByteArray>({identityRequest(false),identityRequest(true)}));QCOMPARE(r.identity.source,Identification::Source::OfflineReplay);QCOMPARE(r.identity.ecuReply,ea());}
    void failure_data(){QTest::addColumn<QString>("fault");for(const char *s:{"queued","generation","cancel","unknown-status","write-status","timeout","disconnect","late","malformed","exception"})QTest::newRow(s)<<QString(s);}
    void failure(){QFETCH(QString,fault);Link link;Clock clock;std::atomic<bool> cancel{false};link.fault=fault;link.clock=&clock;link.cancel=&cancel;const auto r=identify(link,cancel,100,&clock);QVERIFY(!r.complete);QVERIFY(!r.detail.isEmpty());QCOMPARE(link.releases,1);QVERIFY(link.sent.size()<=1);}
    void ownershipAndCleanup(){std::atomic<bool> cancel{false};Link busy;busy.acquireOk=false;QVERIFY(!identify(busy,cancel,100).complete);QCOMPARE(busy.releases,0);QVERIFY(busy.sent.isEmpty());Link bad;bad.releaseOk=false;QVERIFY(!identify(bad,cancel,100).complete);QCOMPARE(bad.releases,1);Link stopped;cancel=true;identify(stopped,cancel,100);QCOMPARE(stopped.acquires,0);}
    void compatibility_data(){QTest::addColumn<QString>("fault");QTest::addColumn<int>("state");using S=Compatibility::State;for(auto f:{"exact","missing-observed","replay","ecu-mismatch","cal-prefix","parent-alias","bin-modified","original-changed","short","checksum","disabled-checksum","kernel","map","protocol","mcu","checksum-config","xml-missing","xml-mismatch","xml-address","xml-ambiguous","partial-mismatch","missing-kernel","raw-can"}) {const QString v=f;S state=S::Incompatible;
        if(v=="exact")state=S::CompatibleEvidence;else if(v=="missing-observed"||v=="xml-missing"||v=="xml-ambiguous"||v=="missing-kernel")state=S::MissingEvidence;else if(v=="replay")state=S::HardwareQualificationRequired;else if(v=="map"||v=="protocol"||v=="mcu"||v=="checksum-config"||v=="disabled-checksum"||v=="raw-can")state=S::Unsupported;QTest::newRow(f)<<v<<int(state);}}
    void compatibility(){
        QFETCH(QString,fault);QFETCH(int,state);auto b=image();const auto initial=b;auto original=SubaruSH7058Safety::fingerprint(b);QByteArray kernel="offline synthetic kernel";Target t;t.imageHash=original;t.kernelHash=SubaruSH7058Safety::fingerprint(kernel);auto c=configuration();auto docs=documents();Identification id;id.ecu=t.ecu;id.calibration=t.calibration;id.source=Identification::Source::ObservedDiagnostic;id.generation=9;
        if(fault=="missing-observed")id=Identification();
        if(fault=="replay")id.source=Identification::Source::OfflineReplay;
        if(fault=="ecu-mismatch")id.ecu="A62A907108";
        if(fault=="cal-prefix")id.calibration="ZE5T201V";
        if(fault=="parent-alias")id.calibration="AE5T201V";
        if(fault=="bin-modified")b[1024]=1;
        if(fault=="original-changed")original[0]=char(original[0]^1);
        if(fault=="short")b.chop(1);
        if(fault=="checksum"){b[0xffb88]=char(b[0xffb88]^1);original=SubaruSH7058Safety::fingerprint(b);t.imageHash=original;}
        if(fault=="disabled-checksum"){be32(b,0xffb80,0);be32(b,0xffb84,0);be32(b,0xffb88,0x5aa5a55a);original=SubaruSH7058Safety::fingerprint(b);t.imageHash=original;}
        if(fault=="partial-mismatch"){id.ecu="BAD";id.calibration.clear();}
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
        const auto before=b;const auto d=resolveDefinition(docs,t.calibration);const auto r=checkCompatibility(b,original,kernel,d,c,id,t);QCOMPARE(int(r.state),state);QCOMPARE(b,before);QVERIFY(!r.programmingAllowed());QVERIFY(!r.reasons.isEmpty());
        if(fault=="checksum")QVERIFY(r.checksumCorrectionRequired);
        if(fault=="exact")QCOMPARE(b,initial);
    }
    void inheritance_data(){QTest::addColumn<QByteArray>("xml");QTest::newRow("cycle")<<QByteArray("<roms><rom base='A'><romid><xmlid>A</xmlid></romid></rom></roms>");QTest::newRow("missing-parent")<<QByteArray("<roms><rom base='B'><romid><xmlid>A</xmlid></romid></rom></roms>");QTest::newRow("DTD")<<QByteArray("<!DOCTYPE roms><roms/>");QTest::newRow("malformed")<<QByteArray("<roms>");}
    void inheritance(){QFETCH(QByteArray,xml);const auto d=resolveDefinition({xml},"A");QVERIFY(!d.valid);QVERIFY(!d.error.isEmpty());}
    void romReadAdmission(){QVERIFY(!legacyReadAdmitted());}
};
QTEST_GUILESS_MAIN(PreflightTest)
#include "preflight.moc"
