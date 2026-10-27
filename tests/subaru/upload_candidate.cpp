#include <QtTest>
#include <QLibrary>
#include <functional>
#include <memory>
#include <stdexcept>
#include "../../serial_port/serial_port_actions.h"
#include "../../serial_port/serial_port_actions_direct.h"
#include "../../serial_port/sh7058_candidate_link.h"
#ifndef Q_OS_WIN
#include "../j2534/mock_j2534.h"
#define U_CALL
extern "C" {
void MockTransportReset();
void MockTransportWrite(long,unsigned long);
void MockTransportRead(long,unsigned long,unsigned long,unsigned long,unsigned long);
void MockTransportReadBytes(long,unsigned long,const unsigned char*,unsigned long,unsigned long);
unsigned long MockTransportValue(int);
unsigned long MockTransportByte(int);
}
#else
#define U_CALL WINAPI
#endif
using C=SubaruSH7058::UploadCandidate;
using Context=SubaruSH7058::StreamContext;
using Cancel=SubaruSH7058::UploadCancellation;

class FakeUploadClock : public SubaruSH7058::UploadClock {
public:
    qint64 ms=0;
    qint64 nowMs() const noexcept override { return ms; }
};
// Controlled provenance belongs to this test fixture, never to an F6 token.
// Every I/O operation still traverses the production facade/direct/J2534 path.
class ControlledLink : public SubaruSH7058::UploadLink {
public:
    SH7058CandidateLink actual;
    Context evidence=Context::ControlledOfflineLoader;
    QString fault;
    quint64 epoch=1;
    bool retired=false;
    int outstanding=0,maximumOutstanding=0;
    FakeUploadClock clock;
    Cancel *cancel=nullptr;
    QVector<QByteArray> sent, apiSent;
    std::function<void(long,unsigned long)> tx;
    std::function<void(long,unsigned long,unsigned long,unsigned long,unsigned long)> rx;
    std::function<void(long,unsigned long,const unsigned char*,unsigned long,unsigned long)> rxBytes;
    std::function<unsigned long(int)> value, byte;
    explicit ControlledLink(SerialPortActions &port):actual(port) {}
    Context context() const noexcept override { return evidence; }
    quint64 generation() const noexcept override { return epoch; }
    void retireContext() noexcept override { retired=true; evidence=Context::Unreviewed; }
    CommunicationResult submit(const QByteArray &bytes) override {
        sent.append(bytes);const bool block=uint8_t(bytes.at(4))==0xb6;
        if (block && fault=="submit-exception") throw std::runtime_error("fixture");
        tx(block && fault=="write-error"?ERR_FAILED:STATUS_NOERROR,
           block && fault=="zero-write"?0:block && fault=="invalid-count"?2:1);
        auto result=actual.submit(bytes);
        if(result.ok()) { ++outstanding; maximumOutstanding=qMax(maximumOutstanding,outstanding); }
        QByteArray payload;
        for(unsigned long i=0;i<value(7);++i) payload+=char(byte(int(i)));
        apiSent.append(payload);
        if(fault=="cancel-after-submit" && block) cancel->requested=true;
        return result;
    }
    CommunicationResult receive(uint16_t timeout) override {
        if(!timeout) {
            if(fault=="stale-before-download" || (fault=="stale-before-block" && sent.size()==1)) {
                const auto stale=QByteArray::fromHex("000007e8f6");
                rxBytes(0,1,reinterpret_cast<const unsigned char*>(stale.constData()),stale.size(),0);
            } else rx(ERR_BUFFER_EMPTY,0,0,0,0);
            return actual.receive(timeout);
        }
        const auto sid=uint8_t(sent.last().at(4));QByteArray body;
        if (sid==0xb6 && fault=="read-exception") throw std::runtime_error("fixture");
        if(sid==0x34) body=QByteArray::fromHex("74200084");
        else if(sid==0xb6) body=QByteArray::fromHex("f6");
        else if(sid==0x37) body=QByteArray::fromHex("77");
        else if(sid==0x31) body=QByteArray::fromHex("71010202");
        else body=C::kernelIdentityReply().mid(4);
        if(sid==0xb6) {
            if(fault=="wrong-F6") body=QByteArray::fromHex("f7");
            if(fault=="extra-F6") body=QByteArray::fromHex("f600");
            if(fault=="negative") body=QByteArray::fromHex("7fb613");
            if(fault=="pending") body=QByteArray::fromHex("7fb678");
            if(fault=="unrelated-negative") body=QByteArray::fromHex("7f3713");
            if(fault=="late-ambiguous-F6") evidence=Context::Unreviewed;
            if(fault=="cancel-read") cancel->requested=true;
        }
        if(sid==0x34 && fault=="short-download") body=QByteArray::fromHex("7420");
        if(sid==0x37 && fault=="exit-negative") body=QByteArray::fromHex("7f3722");
        if(sid==0x31 && fault=="start-negative") body=QByteArray::fromHex("7f3122");
        if(sid==0xbe && fault=="stale-71-in-readiness") body=QByteArray::fromHex("71010202");
        const bool missing=(sid==0xb6 && fault=="timeout") ||
            (sid==0x31 && (fault=="start-timeout" || fault=="start-and-ready-timeout")) ||
            (sid==0xbe && (fault=="ready-timeout" || fault=="start-and-ready-timeout"));
        if(missing) rx(ERR_TIMEOUT,0,0,0,0);
        else if(sid==0xb6 && fault=="disconnect") rx(ERR_DEVICE_NOT_CONNECTED,0,0,0,0);
        else if(sid==0xb6 && fault=="read-error") rx(ERR_FAILED,0,0,0,0);
        else {
            const auto bytes=QByteArray::fromHex(sid==0xb6 && fault=="wrong-id"?"000007e9":"000007e8")+body;
            if(sid==0xb6 && fault=="notifications") rx(0,1,4,TX_MSG_TYPE,0);
            rxBytes(sid==0xb6 && fault=="partial-read"?ERR_TIMEOUT:0,1,
                reinterpret_cast<const unsigned char*>(bytes.constData()),bytes.size(),0);
        }
        const auto result=actual.receive(timeout);
        if(result.ok()) --outstanding;
        if(sid==0xb6 && fault=="replaced-context") ++epoch;
        if((sid==0xb6 && fault=="late-success") || (sid==0x31 && fault=="late-start")) clock.ms+=timeout;
        if(sid==0xb6 && fault=="in-deadline") clock.ms+=timeout-1;
        if(sid==0xb6 && fault=="clock-regressed") --clock.ms;
        if(fault=="cancel-after-final-block" && sent.size()==52) cancel->requested=true;
        return result;
    }
};
class SH7058UploadTest : public QObject {
    Q_OBJECT
    QLibrary library;
    using Reset=void(U_CALL*)();using Write=void(U_CALL*)(long,unsigned long);
    using Read=void(U_CALL*)(long,unsigned long,unsigned long,unsigned long,unsigned long);
    using ReadBytes=void(U_CALL*)(long,unsigned long,const unsigned char*,unsigned long,unsigned long);
    using Value=unsigned long(U_CALL*)(int);
    Reset reset=nullptr;Write tx=nullptr;Read rx=nullptr;ReadBytes rxBytes=nullptr;Value value=nullptr,byte=nullptr;
    std::unique_ptr<SerialPortActions> facade;
    static SubaruSH7058::KernelLayout layout(){return SubaruSH7058::KernelLayout::create(6528,0xffff3000,0xffff3000,0x9000);}
    void configure(ControlledLink &link,Cancel &cancel){link.tx=tx;link.rx=rx;link.rxBytes=rxBytes;link.value=value;link.byte=byte;link.cancel=&cancel;}
private slots:
    void initTestCase(){
#ifdef Q_OS_WIN
        library.setFileName(QDir::current().absoluteFilePath("mock-j2534.dll"));QVERIFY(library.load());
        reset=reinterpret_cast<Reset>(library.resolve("MockTransportReset"));
        tx=reinterpret_cast<Write>(library.resolve("MockTransportWrite"));rx=reinterpret_cast<Read>(library.resolve("MockTransportRead"));
        rxBytes=reinterpret_cast<ReadBytes>(library.resolve("MockTransportReadBytes"));
        value=reinterpret_cast<Value>(library.resolve("MockTransportValue"));byte=reinterpret_cast<Value>(library.resolve("MockTransportByte"));
#else
        reset=MockTransportReset;tx=MockTransportWrite;rx=MockTransportRead;rxBytes=MockTransportReadBytes;value=MockTransportValue;byte=MockTransportByte;
#endif
        QVERIFY(reset && tx && rx && rxBytes && value && byte);
    }
    void init(){
#ifndef Q_OS_WIN
        MockJ2534::reset();
#endif
        reset();facade.reset(new SerialPortActions);auto *port=facade->serial_direct;
#ifdef Q_OS_WIN
        port->j2534->setDllName(library.fileName().toLocal8Bit().constData());
#else
        port->serial_port="mock";
#endif
        port->is_iso15765_connection=true;QCOMPARE(port->init_j2534_connection(),STATUS_SUCCESS);
        port->use_openport2_adapter=true;
    }
    void cleanup(){facade.reset();}
    void liveBoundaryCannotSubmit(){
        SH7058CandidateLink link(*facade);Cancel cancel;C candidate;
        const auto report=candidate.run(link,QByteArray(6528,'K'),layout(),100,cancel);
        QCOMPARE(int(report.outcome),int(C::Outcome::EntryBlocked));QCOMPARE(value(0),0UL);QCOMPARE(value(1),0UL);QVERIFY(!report.programmingAllowed());
    }
    void complete(){
        Cancel cancel;ControlledLink link(*facade);configure(link,cancel);C candidate;
        const auto report=candidate.run(link,QByteArray(6528,'K'),layout(),100,cancel);
        QCOMPARE(int(report.outcome),int(C::Outcome::ReadyForOfflineReview));
        QCOMPARE(link.maximumOutstanding,1);QCOMPARE(link.outstanding,0);
        QCOMPARE(report.acknowledgedBlocks,51u);QCOMPARE(report.submissions,55u);QCOMPARE(link.sent,link.apiSent);
        QVERIFY(report.startupAcknowledged && report.kernelResponding);QVERIFY(link.retired);
        QCOMPARE(link.sent.first(),QByteArray::fromHex("000007e0340433ff3000001980"));
        for(int i=0;i<51;++i){
            auto expected=QByteArray::fromHex("000007e0b6");const uint32_t address=0xffff3000u+uint32_t(i)*128;
            for(int shift=16;shift>=0;shift-=8) expected+=char(address>>shift);
            expected+=QByteArray(128,'K');QCOMPARE(link.sent.at(i+1),expected);
        }
        QCOMPARE(link.sent.at(52),QByteArray::fromHex("000007e037"));
        QCOMPARE(link.sent.at(53),QByteArray::fromHex("000007e03101020202"));
        QCOMPARE(link.sent.at(54),QByteArray::fromHex("000007e0beef000101000000"));QVERIFY(!report.programmingAllowed());
        const auto reused=candidate.run(link,QByteArray(6528,'K'),layout(),100,cancel);
        QCOMPARE(int(reused.outcome),int(C::Outcome::Reused));QCOMPARE(link.sent.size(),55);
    }
    void faults_data(){
        QTest::addColumn<QString>("fault");QTest::addColumn<int>("outcome");QTest::addColumn<int>("submissions");
        const auto row=[](const char *name,C::Outcome o,int n){QTest::newRow(name)<<QString(name)<<int(o)<<n;};
        row("stale-before-download",C::Outcome::OwnershipUncertain,0);row("stale-before-block",C::Outcome::OwnershipUncertain,1);
        row("short-download",C::Outcome::Malformed,1);row("wrong-F6",C::Outcome::Unrelated,2);row("extra-F6",C::Outcome::Malformed,2);
        row("negative",C::Outcome::Rejected,2);row("pending",C::Outcome::Unsupported,2);row("unrelated-negative",C::Outcome::Unrelated,2);
        row("wrong-id",C::Outcome::Malformed,2);row("late-ambiguous-F6",C::Outcome::OwnershipUncertain,2);
        row("write-error",C::Outcome::SubmissionUncertain,2);row("zero-write",C::Outcome::SubmissionUncertain,2);row("invalid-count",C::Outcome::SubmissionUncertain,2);
        row("replaced-context",C::Outcome::OwnershipUncertain,2);
        row("late-success",C::Outcome::DeadlineExceeded,2);
        row("late-start",C::Outcome::DeadlineExceeded,54);
        row("in-deadline",C::Outcome::ReadyForOfflineReview,55);
        row("clock-regressed",C::Outcome::DeadlineExceeded,2);
        row("timeout",C::Outcome::Timeout,2);row("disconnect",C::Outcome::TransportFailure,2);
        row("cancel-after-submit",C::Outcome::Cancelled,2);row("cancel-read",C::Outcome::Cancelled,2);row("cancel-after-final-block",C::Outcome::Cancelled,52);
        row("exit-negative",C::Outcome::Rejected,53);row("start-timeout",C::Outcome::ReadyAfterUncertainStartReviewRequired,55);
        row("ready-timeout",C::Outcome::StartupUncertain,55);row("start-and-ready-timeout",C::Outcome::StartupUncertain,55);
        row("notifications",C::Outcome::ReadyForOfflineReview,55);
        row("submit-exception",C::Outcome::SubmissionUncertain,2);
        row("read-exception",C::Outcome::TransportFailure,2);
        row("start-negative",C::Outcome::Rejected,54);
        row("stale-71-in-readiness",C::Outcome::Malformed,55);
        row("partial-read",C::Outcome::Malformed,2);
        row("read-error",C::Outcome::TransportFailure,2);
    }
    void faults(){
        QFETCH(QString,fault);QFETCH(int,outcome);QFETCH(int,submissions);Cancel cancel;ControlledLink link(*facade);configure(link,cancel);link.fault=fault;C candidate(&link.clock);
        const auto report=candidate.run(link,QByteArray(6528,'K'),layout(),100,cancel);
        QCOMPARE(int(report.outcome),outcome);QCOMPARE(link.sent.size(),submissions);QVERIFY(!report.programmingAllowed());
        QVERIFY(link.retired);
        const auto sentBefore=link.sent.size();
        C fresh;
        const auto resumed=fresh.run(link,QByteArray(6528,'K'),layout(),100,cancel);
        QCOMPARE(int(resumed.outcome),int(cancel.requested.load()?C::Outcome::Cancelled:C::Outcome::EntryBlocked));
        QCOMPARE(link.sent.size(),sentBefore);
        if(fault=="late-success" || fault=="late-start") QVERIFY(report.transport.ok()); // raw success is retained, not accepted
        if(fault=="start-timeout"){
            QVERIFY(!report.startupAcknowledged);QVERIFY(report.kernelResponding);
            QCOMPARE(int(report.startupTransport.state),int(CommunicationResult::State::Timeout));
            QVERIFY(report.startupTransport.hasStatus);QCOMPARE(report.startupTransport.status,long(ERR_TIMEOUT));
        }
        if(fault=="ready-timeout"){QVERIFY(report.startupAcknowledged);QVERIFY(!report.kernelResponding);}
        if(fault=="write-error"){QVERIFY(report.transport.hasStatus);QCOMPARE(report.transport.status,long(ERR_FAILED));}
        if(fault=="start-negative") QCOMPARE(int(report.startup),int(C::Startup::Failed));
        for(const auto &bytes:link.sent){
            const auto sid=uint8_t(bytes.at(4));QVERIFY(sid==0x34 || sid==0xb6 || sid==0x37 || sid==0x31 || sid==0xbe);
            if(sid==0xbe) QCOMPARE(bytes,QByteArray::fromHex("000007e0beef000101000000"));
        }
    }
    void invalidLayout_data(){
        QTest::addColumn<uint>("base");QTest::addColumn<int>("size");
        QTest::newRow("wrong-block-address")<<0xffff3080u<<6528;QTest::newRow("empty")<<0xffff3000u<<0;
        QTest::newRow("outside-OEM-download")<<0xffff3000u<<8320;QTest::newRow("wrong-kernel-count")<<0xffff3000u<<6400;
    }
    void invalidLayout(){
        QFETCH(uint,base);QFETCH(int,size);Cancel cancel;ControlledLink link(*facade);configure(link,cancel);C candidate;
        auto plan=SubaruSH7058::KernelLayout::create(size,base,0xffff3000,0x9000);
        const auto report=candidate.run(link,QByteArray(size,'K'),plan,100,cancel);
        QCOMPARE(int(report.outcome),int(C::Outcome::InvalidImage));QVERIFY(link.sent.isEmpty());
    }
    void missingSessionGeneration(){
        Cancel cancel;ControlledLink link(*facade);configure(link,cancel);link.epoch=0;C candidate;
        const auto report=candidate.run(link,QByteArray(6528,'K'),layout(),100,cancel);
        QCOMPARE(int(report.outcome),int(C::Outcome::EntryBlocked));QVERIFY(link.sent.isEmpty());
        QVERIFY(!link.retired);QCOMPARE(value(1),0UL);
    }
    void cancelBeforeEntry(){
        Cancel cancel;cancel.requested=true;SH7058CandidateLink link(*facade);C candidate;
        const auto report=candidate.run(link,{},layout(),100,cancel);QCOMPARE(int(report.outcome),int(C::Outcome::Cancelled));QCOMPARE(value(0),0UL);
    }
};
QTEST_MAIN(SH7058UploadTest)
#include "upload_candidate.moc"
