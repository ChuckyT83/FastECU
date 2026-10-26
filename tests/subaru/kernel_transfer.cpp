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
    void ownerFirmwareReplies_data() {
        QTest::addColumn<int>("phase"); QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("expected");
        const auto firmware = ReplyProfile::ownerBaseFirmware();
        const Phase phases[] = {Phase::Download, Phase::Block, Phase::Exit, Phase::Start};
        const char *names[] = {"download-1b3c", "block-1c6e", "exit-1c10", "start-1a72"};
        const int services[] = {0x34, 0xb6, 0x37, 0x31};
        for (int i = 0; i < 4; ++i) {
            const auto positive = firmware.expectedReply(phases[i]);
            auto row = [&](const char *suffix, const QByteArray &bytes, Outcome expected) {
                QTest::newRow(qPrintable(QString(names[i]) + suffix)) << int(phases[i]) << bytes << int(expected);
            };
            row("-exact", positive, Outcome::Running);
            row("-empty", {}, Outcome::Malformed);
            row("-truncated", positive.left(positive.size()-1), Outcome::Malformed);
            row("-extra-application-byte", positive + char(0), Outcome::Malformed);
            auto unrelated = positive; unrelated[4] = char(uint8_t(unrelated.at(4)) ^ 1);
            row("-unrelated-service", unrelated, Outcome::Unrelated);
            auto wrongChannel = positive; wrongChannel[3] = char(0xe9);
            row("-wrong-can-id", wrongChannel, Outcome::Malformed);
            for (int nrc : {0x13, 0x22}) {
                auto negative = QByteArray::fromHex("000007e87f");
                negative += char(services[i]); negative += char(nrc);
                const auto suffix = QString("-nrc-%1").arg(nrc,2,16,QLatin1Char('0'));
                row(qPrintable(suffix), negative, Outcome::Rejected);
            }
        }
        // Synthetic 74 20 omits the two bytes in the actual 0x1FA0 descriptor.
        QTest::newRow("download-reject-old-synthetic-prefix") << int(Phase::Download)
            << QByteArray::fromHex("000007e87420") << int(Outcome::Malformed);
        QTest::newRow("download-format-nrc-12-1b72") << int(Phase::Download)
            << QByteArray::fromHex("000007e87f3412") << int(Outcome::Rejected);
        QTest::newRow("start-routine-nrc-12-1ac4") << int(Phase::Start)
            << QByteArray::fromHex("000007e87f3112") << int(Outcome::Rejected);
    }
    void ownerFirmwareReplies() {
        QFETCH(int, phase); QFETCH(QByteArray, bytes); QFETCH(int, expected);
        Request request; request.phase=Phase(phase);
        const char *services[] = {"34", "b6", "37", "31", "beef"};
        request.bytes=QByteArray::fromHex("000007e0")+QByteArray::fromHex(services[phase]);
        QCOMPARE(int(ReplyProfile::ownerBaseFirmware().validate(request,bytes)),expected);
    }
    void ownerFirmwareDownloadBounds_data() {
        QTest::addColumn<qlonglong>("size"); QTest::addColumn<uint>("base"); QTest::addColumn<bool>("accepted");
        // 0x1B9A..0x1BB4 is narrower than the generic 36-KiB runtime RAM map.
        QTest::newRow("one-block") << 128LL << 0xffff3000u << true;
        QTest::newRow("bundled-kernel-prepared") << 6528LL << 0xffff3000u << true;
        QTest::newRow("OEM-download-upper-bound") << 8192LL << 0xffff3000u << true;
        QTest::newRow("past-OEM-bound") << 8320LL << 0xffff3000u << false;
        QTest::newRow("runtime-RAM-not-download-permission") << 0x9000LL << 0xffff3000u << false;
        QTest::newRow("wrong-start-four") << 128LL << 0xffff3004u << false;
        QTest::newRow("wrong-start-block") << 128LL << 0xffff3080u << false;
        QTest::newRow("empty-rejected-by-host-policy") << 0LL << 0xffff3000u << false;
    }
    void ownerFirmwareDownloadBounds() {
        QFETCH(qlonglong,size); QFETCH(uint,base); QFETCH(bool,accepted);
        const auto layout=KernelLayout::create(size,base,0xffff3000u,0x9000u);
        const auto firmware=ReplyProfile::ownerBaseFirmware();
        Transfer transfer(QByteArray(layout.preparedSize(),'K'),layout,&firmware);
        Request request;
        QCOMPARE(transfer.takeRequest(request),accepted);
        if (!accepted) QCOMPARE(int(transfer.outcome()),int(Outcome::InvalidLayout));
    }
    void ownerFirmwareBundledLayoutTrace() {
        const auto firmware=ReplyProfile::ownerBaseFirmware();
        const auto layout=plan(6428);
        // Synthetic encrypted bytes: never embed the owner's firmware/kernel.
        const QByteArray prepared(6528,'K');
        Transfer transfer(prepared,layout,&firmware);
        int requests=0, blocks=0;
        while (transfer.outcome()==Outcome::Running && requests<60) {
            Request request; QVERIFY(transfer.takeRequest(request)); ++requests;
            if (request.phase==Phase::Download)
                QCOMPARE(request.bytes,QByteArray::fromHex("000007e0340433ff3000001980"));
            if (request.phase==Phase::Block) {
                QCOMPARE(request.bytes.size(),136); // CAN-ID prefix + B6/address + 128
                const uint32_t address=0xffff3000u+uint32_t(blocks)*128;
                QByteArray expected=QByteArray::fromHex("000007e0b6");
                for (int shift=16;shift>=0;shift-=8) expected+=char(address>>shift);
                expected+=QByteArray(128,'K'); QCOMPARE(request.bytes,expected); ++blocks;
            }
            transfer.submitted(request.fixtureToken,result(),1);
            transfer.received(request.fixtureToken,result(S::Success,firmware.expectedReply(request.phase)));
        }
        QCOMPARE(blocks,51); QCOMPARE(requests,55);
        QCOMPARE(int(transfer.outcome()),int(Outcome::SimulatedReady));
        Request extra; QVERIFY(!transfer.takeRequest(extra));
    }
    void ownerFirmwareDoesNotInventCorrelation() {
        const auto firmware=ReplyProfile::ownerBaseFirmware();
        Transfer transfer(QByteArray(128,'K'),plan(128),&firmware);
        Request request; QVERIFY(transfer.takeRequest(request));
        transfer.submitted(request.fixtureToken,result(),1);
        transfer.received(request.fixtureToken,result(S::Success,firmware.expectedReply(request.phase)));
        QVERIFY(transfer.takeRequest(request)); QCOMPARE(int(request.phase),int(Phase::Block));
        transfer.submitted(request.fixtureToken,result(),1);
        // Firmware 0x1D30..0x1D3A emits no address/sequence correlation field.
        transfer.received(0,result(S::Success,firmware.expectedReply(Phase::Block)));
        stopped(transfer,Outcome::Uncorrelated);
        QCOMPARE(transfer.acknowledgedBlocks(),0u);
    }
    void ownerFirmwareRejectsEmptyHistoricalBlock() {
        const auto firmware=ReplyProfile::ownerBaseFirmware();
        Transfer transfer(QByteArray(128,'K'),plan(128),&firmware);
        Request request; QVERIFY(transfer.takeRequest(request));
        transfer.submitted(request.fixtureToken,result(),1);
        transfer.received(request.fixtureToken,result(S::Success,firmware.expectedReply(request.phase)));
        QVERIFY(transfer.takeRequest(request));
        transfer.submitted(request.fixtureToken,result(),1);
        // 0x1C9A..0x1CA6: anything other than 131 parameter bytes -> 7F B6 13.
        transfer.received(request.fixtureToken,result(S::Success,QByteArray::fromHex("000007e87fb613")));
        stopped(transfer,Outcome::Rejected); QCOMPARE(transfer.acknowledgedBlocks(),0u);
    }
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
