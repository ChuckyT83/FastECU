#include <QtTest>
#include <limits>
#include <memory>
#include "experimental_kernel_transfer.h"

using SubaruSH7058::KernelLayout;
using namespace OfflineKernelModel;
using S = CommunicationResult::State;

class KernelTransferTest : public QObject {
    Q_OBJECT
    SyntheticProfile profile;
    static KernelLayout plan(int64_t n = 384) {
        return KernelLayout::create(n, 0xffff3000, 0xffff3000, 0x9000);
    }
    static CommunicationResult result(S state = S::Success, const QByteArray &bytes = {}, long raw = 0) {
        CommunicationResult r; r.state = state; r.bytes = bytes; r.hasStatus = true; r.status = raw; return r;
    }
    static void acknowledge(Transfer &t, const Request &r) {
        t.submitted(r.fixtureToken, result(), 1);
        t.received(r.fixtureToken, result(S::Success, SyntheticProfile::reply(r.phase)));
    }
    static bool advance(Transfer &t, Phase phase) {
        for (int i = 0; i < 16; ++i) {
            if (t.phase() == phase && t.outcome() == Outcome::Running) return true;
            Request r; if (!t.takeRequest(r)) return false; acknowledge(t, r);
        }
        return false;
    }
    static void stopped(Transfer &t, Outcome expected) {
        QCOMPARE(int(t.outcome()), int(expected)); QVERIFY(!t.simulatedReady());
        Request next; QVERIFY(!t.takeRequest(next)); QVERIFY(next.bytes.isEmpty());
        // A late/stale positive must never resurrect a failed/cancelled attempt.
        t.received(1, result(S::Success, SyntheticProfile::reply(Phase::Readiness)));
        t.submitted(1, result(), 1);
        QCOMPARE(int(t.outcome()), int(expected)); QVERIFY(!t.simulatedReady());
    }
private slots:
    void layout_data() {
        QTest::addColumn<qlonglong>("size"); QTest::addColumn<uint>("destination");
        QTest::addColumn<uint>("ramStart"); QTest::addColumn<uint>("ramLength"); QTest::addColumn<bool>("valid");
        for (int n : {-1,0,1,3,4,127,128,129,6428,0x9000,0x9001})
            QTest::newRow(qPrintable(QString::number(n))) << qlonglong(n) << 0xffff3000u << 0xffff3000u << 0x9000u << bool(n >= 4 && n <= 0x9000);
        QTest::newRow("unaligned") << 128LL << 0xffff3001u << 0xffff3000u << 0x9000u << false;
        QTest::newRow("below-RAM") << 128LL << 0xffff2ffcu << 0xffff3000u << 0x9000u << false;
        QTest::newRow("past-RAM") << 128LL << 0xffffc000u << 0xffff3000u << 0x9000u << false;
        QTest::newRow("padded-past-RAM") << 129LL << 0xffffbf80u << 0xffff3000u << 0x9000u << false;
        QTest::newRow("zero-RAM") << 128LL << 0xffff3000u << 0xffff3000u << 0u << false;
        QTest::newRow("RAM-overflows-32bits") << 128LL << 0xfffff000u << 0xfffff000u << 0x2000u << false;
        QTest::newRow("last-32bit-byte") << 128LL << 0xffffff80u << 0xffffff80u << 128u << true;
        QTest::newRow("Qt-int-padding-overflow") << qlonglong((std::numeric_limits<int>::max)()) << 0u << 0u << 0xffffffffu << false;
        QTest::newRow("huge-source") << (std::numeric_limits<qlonglong>::max)() << 0u << 0u << 0xffffffffu << false;
    }
    void layout() {
        QFETCH(qlonglong,size); QFETCH(uint,destination); QFETCH(uint,ramStart); QFETCH(uint,ramLength); QFETCH(bool,valid);
        const auto p = KernelLayout::create(size,destination,ramStart,ramLength);
        QCOMPARE(p.valid(),valid);
        if (valid) {
            QCOMPARE(p.preparedSize(),uint32_t((size+127)/128*128));
            QCOMPARE(p.blockCount(),p.preparedSize()/128);
            uint64_t total=0;
            for (uint32_t i=0;i<p.blockCount();++i) {
                KernelLayout::Block b; QVERIFY(p.block(i,b)); QCOMPARE(b.length,128u);
                QCOMPARE(uint64_t(b.address),uint64_t(destination)+i*128); QCOMPARE(b.offset,i*128);
                QVERIFY(uint64_t(b.address)+b.length <= uint64_t(ramStart)+ramLength); total+=b.length;
            }
            QCOMPARE(total,uint64_t(p.preparedSize()));
        } else { QCOMPARE(p.preparedSize(),0u); QCOMPARE(p.blockCount(),0u); }
        KernelLayout::Block last; last.address=123;
        QVERIFY(!p.block(p.blockCount(),last)); QCOMPARE(last.address,0u); QCOMPARE(last.length,0u);
        QVERIFY(!p.block((std::numeric_limits<uint32_t>::max)(),last));
    }
    void unestablishedContractNeverStarts() {
        Transfer t(QByteArray(128,'K'),plan(128));
        stopped(t,Outcome::Unsupported);
    }
    void invalidPreparedImage_data() {
        QTest::addColumn<int>("bytes"); for(int n:{0,1,127,129,256}) QTest::newRow(qPrintable(QString::number(n)))<<n;
    }
    void invalidPreparedImage() {
        QFETCH(int,bytes); Transfer t(QByteArray(bytes,'K'),plan(128),&profile); stopped(t,Outcome::InvalidLayout);
    }
    void commandTrace_data() {
        QTest::addColumn<int>("bytes"); for(int n:{4,128,129,6428,0x9000}) QTest::newRow(qPrintable(QString::number(n)))<<n;
    }
    void commandTrace() {
        QFETCH(int,bytes); const auto p=plan(bytes); QByteArray prepared(int(p.preparedSize()),'\0');
        for(int i=0;i<prepared.size();++i) prepared[i]=char((i*7+31)&255);
        Transfer t(prepared,p,&profile); QVector<QByteArray> trace; QByteArray transmitted;
        Request r; uint32_t count=0;
        while(t.takeRequest(r)) {
            QCOMPARE(r.bytes.left(4),QByteArray::fromHex("000007e0")); trace.append(r.bytes);
            if(r.phase==Phase::Block) {
                QCOMPARE(r.bytes.size(),136); QCOMPARE(uint8_t(r.bytes.at(4)),uint8_t(0xb6));
                const uint32_t address=(uint8_t(r.bytes.at(5))<<16)|(uint8_t(r.bytes.at(6))<<8)|uint8_t(r.bytes.at(7));
                QCOMPARE(address,(0xffff3000u+count*128)&0x00ffffffu);
                QCOMPARE(r.bytes.mid(8),prepared.mid(int(count*128),128)); transmitted+=r.bytes.mid(8); ++count;
            }
            acknowledge(t,r);
        }
        QVERIFY(t.simulatedReady()); QCOMPARE(count,p.blockCount()); QCOMPARE(trace.size(),int(count)+4);
        QCOMPARE(transmitted,prepared); QCOMPARE(t.acknowledgedBlocks(),count);
        QByteArray download=QByteArray::fromHex("000007e0340433ff3000");
        for(int shift=16;shift>=0;shift-=8) download+=char(p.preparedSize()>>shift);
        QCOMPARE(trace.first(),download);
        QCOMPARE(trace.at(trace.size()-3),QByteArray::fromHex("000007e037"));
        QCOMPARE(trace.at(trace.size()-2),QByteArray::fromHex("000007e03101020202"));
        QCOMPARE(trace.last(),QByteArray::fromHex("000007e0beef000101000000"));
    }
    void replyFault_data() {
        QTest::addColumn<int>("phase"); QTest::addColumn<QString>("fault"); QTest::addColumn<int>("expected");
        const QVector<QPair<QString,Outcome>> cases={
            {"negative",Outcome::Rejected},{"pending",Outcome::Unsupported},{"wrong-negative-service",Outcome::Unrelated},
            {"truncated",Outcome::Malformed},{"oversized",Outcome::Malformed},{"empty",Outcome::Malformed},
            {"unexpected-service",Outcome::Unrelated},{"wrong-CAN-id",Outcome::Malformed},
            {"timeout",Outcome::Timeout},{"driver",Outcome::TransportError},{"driver-with-stale-data",Outcome::TransportError},
            {"cancelled-read",Outcome::Cancelled},{"disconnected",Outcome::Disconnected},
            {"partial-reception",Outcome::Malformed},{"no-correlation",Outcome::Uncorrelated},{"wrong-token",Outcome::OutOfOrder}};
        for(int phase=0;phase<5;++phase) for(const auto &c:cases)
            QTest::newRow(qPrintable(QString::number(phase)+"/"+c.first))<<phase<<c.first<<int(c.second);
    }
    void replyFault() {
        QFETCH(int,phase); QFETCH(QString,fault); QFETCH(int,expected);
        Transfer t(QByteArray(384,'K'),plan(),&profile); QVERIFY(advance(t,Phase(phase)));
        Request r; QVERIFY(t.takeRequest(r)); t.submitted(r.fixtureToken,result(),1);
        const auto before=t.acknowledgedBlocks(); auto value=result(S::Success,SyntheticProfile::reply(r.phase)); auto token=r.fixtureToken;
        if(fault=="negative"||fault=="pending"||fault=="wrong-negative-service") {
            value.bytes=QByteArray::fromHex("000007e87f")+r.bytes.mid(4,1)+char(fault=="pending"?0x78:0x13);
            if(fault=="wrong-negative-service") value.bytes[5]=char(uint8_t(r.bytes.at(4))^1);
        } else if(fault=="truncated") value.bytes.chop(1);
        else if(fault=="oversized") value.bytes+=char(0);
        else if(fault=="empty") value.bytes.clear();
        else if(fault=="unexpected-service") value.bytes[4]=char(uint8_t(value.bytes.at(4))^1);
        else if(fault=="wrong-CAN-id") value.bytes[3]=char(0xe9);
        else if(fault=="timeout") value=result(S::Timeout,{},9);
        else if(fault=="driver") value=result(S::TransportError,{},7);
        else if(fault=="driver-with-stale-data") value=result(S::TransportError,value.bytes,7);
        else if(fault=="cancelled-read") value=result(S::Cancelled,{},0);
        else if(fault=="disconnected") value=result(S::Disconnected,{},2);
        else if(fault=="partial-reception") value=result(S::Malformed,value.bytes,9);
        else if(fault=="no-correlation") token=0;
        else if(fault=="wrong-token") ++token;
        t.received(token,value); QCOMPARE(t.acknowledgedBlocks(),before);
        QCOMPARE(t.lastTransportResult().status,value.status); QCOMPARE(t.lastTransportResult().hasStatus,value.hasStatus);
        stopped(t,Outcome(expected));
    }
    void submissionFault_data() {
        QTest::addColumn<int>("phase"); QTest::addColumn<int>("state"); QTest::addColumn<ulong>("count"); QTest::addColumn<int>("expected");
        for(int phase=0;phase<5;++phase) {
            for(ulong count:{0UL,2UL}) QTest::newRow(qPrintable(QString::number(phase)+"/count"+QString::number(count)))<<phase<<int(S::Success)<<count<<int(count==0 ? Outcome::PartialSubmission : Outcome::Malformed);
            for(ulong count:{0UL,1UL}) QTest::newRow(qPrintable(QString::number(phase)+"/driver"+QString::number(count)))<<phase<<int(S::TransportError)<<count<<int(Outcome::TransportError);
            QTest::newRow(qPrintable(QString::number(phase)+"/timeout"))<<phase<<int(S::Timeout)<<1UL<<int(Outcome::Timeout);
            QTest::newRow(qPrintable(QString::number(phase)+"/cancelled"))<<phase<<int(S::Cancelled)<<0UL<<int(Outcome::Cancelled);
            QTest::newRow(qPrintable(QString::number(phase)+"/disconnected"))<<phase<<int(S::Disconnected)<<0UL<<int(Outcome::Disconnected);
        }
    }
    void submissionFault() {
        QFETCH(int,phase); QFETCH(int,state); QFETCH(ulong,count); QFETCH(int,expected);
        Transfer t(QByteArray(384,'K'),plan(),&profile); QVERIFY(advance(t,Phase(phase)));
        Request r; QVERIFY(t.takeRequest(r)); const auto before=t.acknowledgedBlocks();
        t.submitted(r.fixtureToken,result(S(state),{},state==int(S::Success)?0:9),count);
        t.received(r.fixtureToken,result(S::Success,SyntheticProfile::reply(r.phase)));
        QCOMPARE(t.acknowledgedBlocks(),before); stopped(t,Outcome(expected));
    }
    void cancellation_data() {
        QTest::addColumn<int>("phase"); QTest::addColumn<int>("point");
        for(int phase=0;phase<5;++phase) for(int point=0;point<3;++point)
            QTest::newRow(qPrintable(QString::number(phase)+"/"+QString::number(point)))<<phase<<point;
    }
    void cancellation() {
        QFETCH(int,phase); QFETCH(int,point); Transfer t(QByteArray(384,'K'),plan(),&profile);
        QVERIFY(advance(t,Phase(phase))); Request r;
        if(point>0) QVERIFY(t.takeRequest(r));
        if(point>1) t.submitted(r.fixtureToken,result(),1);
        t.cancel(); stopped(t,Outcome::Cancelled);
    }
    void staleIdenticalAck() {
        Transfer t(QByteArray(384,'K'),plan(),&profile); QVERIFY(advance(t,Phase::Block));
        Request first; QVERIFY(t.takeRequest(first)); acknowledge(t,first);
        Request second; QVERIFY(t.takeRequest(second)); t.submitted(second.fixtureToken,result(),1);
        // Identical wire F6 is distinguishable HERE only by fake out-of-band
        // bookkeeping. The default/unestablished profile cannot do this.
        t.received(first.fixtureToken,result(S::Success,SyntheticProfile::reply(Phase::Block)));
        QCOMPARE(t.acknowledgedBlocks(),1u); stopped(t,Outcome::OutOfOrder);
    }
    void duplicateAck() {
        Transfer t(QByteArray(384,'K'),plan(),&profile); Request r; QVERIFY(t.takeRequest(r)); acknowledge(t,r);
        t.received(r.fixtureToken,result(S::Success,SyntheticProfile::reply(r.phase))); stopped(t,Outcome::OutOfOrder);
    }
    void replyBeforeSubmission() {
        Transfer t(QByteArray(384,'K'),plan(),&profile); Request r; QVERIFY(t.takeRequest(r));
        t.received(r.fixtureToken,result(S::Success,SyntheticProfile::reply(r.phase))); stopped(t,Outcome::OutOfOrder);
    }
    void noOverlappingRequests() {
        Transfer t(QByteArray(384,'K'),plan(),&profile); Request r; QVERIFY(t.takeRequest(r));
        QVERIFY(!t.takeRequest(r)); stopped(t,Outcome::OutOfOrder);
    }
    void preparedBufferOwnership() {
        QByteArray bytes(128,'A'); Transfer t(bytes,plan(128),&profile); bytes.fill('B');
        QVERIFY(advance(t,Phase::Block)); Request r; QVERIFY(t.takeRequest(r)); QCOMPARE(r.bytes.mid(8),QByteArray(128,'A'));
    }
    void finalBlockFailureNeverExits() {
        Transfer t(QByteArray(384,'K'),plan(),&profile); QVERIFY(advance(t,Phase::Block)); Request r;
        for(int i=0;i<2;++i) { QVERIFY(t.takeRequest(r)); acknowledge(t,r); }
        QVERIFY(t.takeRequest(r)); QCOMPARE(int(r.phase),int(Phase::Block));
        t.submitted(r.fixtureToken,result(),1); t.received(r.fixtureToken,result(S::Timeout,{},9));
        QCOMPARE(t.acknowledgedBlocks(),2u); stopped(t,Outcome::Timeout);
    }
    void malformedRequestCannotIndexNegativeParser() {
        Request empty; QCOMPARE(int(profile.validate(empty,QByteArray::fromHex("000007e87f3413"))),int(Outcome::Malformed));
    }
    void profileLifetimeIsNotBorrowed() {
        std::unique_ptr<Transfer> transfer;
        { SyntheticProfile local; transfer.reset(new Transfer(QByteArray(128,'K'),plan(128),&local)); }
        Request r; while(transfer->takeRequest(r)) acknowledge(*transfer,r);
        QVERIFY(transfer->simulatedReady());
    }
    void borrowedPreparedBufferIsCopied() {
        QByteArray backing(128,'A');
        const auto borrowed=QByteArray::fromRawData(backing.constData(),backing.size());
        Transfer t(borrowed,plan(128),&profile); backing.data()[0]='B';
        QVERIFY(advance(t,Phase::Block)); Request r; QVERIFY(t.takeRequest(r));
        QCOMPARE(r.bytes.mid(8),QByteArray(128,'A'));
    }
    void borrowedReplySnapshotIsCopied() {
        Transfer t(QByteArray(128,'A'),plan(128),&profile); Request r; QVERIFY(t.takeRequest(r));
        t.submitted(r.fixtureToken,result(),1);
        auto backing=SyntheticProfile::reply(r.phase);
        const QByteArray expected(backing.constData(),backing.size());
        const auto borrowed=QByteArray::fromRawData(backing.constData(),backing.size());
        t.received(r.fixtureToken,result(S::Success,borrowed)); backing.data()[0]=char(0xff);
        QCOMPARE(t.lastTransportResult().bytes,expected);
    }
};
QTEST_GUILESS_MAIN(KernelTransferTest)
#include "kernel_transfer.moc"
