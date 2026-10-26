#include <QtTest>
#include "../../modules/ecu/subaru_sh7058_kernel_layout.h"
#include <cstdio>
#include <QMessageBox>
#include <QCloseEvent>
#include "serial_port_actions.h"
#include "flash_ecu_subaru_denso_sh7058_can.h"
#include "subaru_sh7058_safety.h"

// Diagnostic formatting is the only FileActions symbol needed by the module.
// Its text is irrelevant to state/command assertions; the application links the
// real implementation. No flash/checksum algorithm is replaced.
QString FileActions::parse_nrc_message(QByteArray response) { return response.toHex(); }

using C = ChecksumEcuSubaruDensoSH7xxx;
static void put32(QByteArray &a, int pos, uint32_t value) {
    for (int i = 0; i < 4; ++i) a[pos + i] = char(value >> (24 - 8*i));
}
static uint32_t get32(const QByteArray &a, int pos) {
    uint32_t value = 0;
    for (int i=0; i<4; ++i) value = (value << 8) | uint8_t(a.at(pos+i));
    return value;
}
static QByteArray image() {
    QByteArray bytes(1024*1024, '\0');
    put32(bytes, 0xFFB88, 0x5AA5A55A); // explicit checksum-disabled sentinel
    return bytes;
}
static QByteArray ack(int command, uint32_t value = 0) {
    int payload=0;
    if(command==SUB_KERNEL_CRC || command==SUB_KERNEL_GET_MAX_MSG_SIZE || command==SUB_KERNEL_GET_MAX_BLK_SIZE || command==SUB_KERNEL_ID) payload=4;
    if(command==SUB_KERNEL_PROG_VOLT) payload=2;
    QByteArray a(13, '\0');
    a[2] = '\7'; a[3] = char(0xE8);
    a[4] = char(SUB_KERNEL_START_COMM >> 8); a[5] = char(SUB_KERNEL_START_COMM);
    a[7] = char(payload+1); a[8] = char(command | 0x40);
    put32(a, 9, value);
    a.resize(9+payload);
    return a;
}

class SubaruSafetyTest : public QObject {
    Q_OBJECT
    FileActions::EcuCalDefStructure cal;
    SerialPortActions port;
    FlashEcuSubaruDensoSH7058Can *module = nullptr;
    int crcRequests = 0;
    QVector<int> commands() const {
        QVector<int> result;
        for (const auto &a : port.writes) if (a.size() >= 9) result.append(uint8_t(a.at(8)));
        return result;
    }
    void cancel() { QCloseEvent e; module->closeEvent(&e); }
    int postMismatches = 0, badCrc = -1, shortCrc = -1;
    bool initialDifference = true;
    void setupReplies() {
        port.respond = [this](const QByteArray &a) {
            int cmd = uint8_t(a.at(8));
            if (cmd == SUB_KERNEL_CRC) {
                const int request = crcRequests++;
                if (request == badCrc) return QByteArray();
                if (request == shortCrc) return ack(cmd).left(12);
                const uint32_t addr = get32(a,9), size = get32(a,13);
                uint32_t crc = module->crc32(reinterpret_cast<const uint8_t *>(cal.FullRomData.constData())+addr, size);
                if ((request < 16 && initialDifference && request == 0) ||
                    (request >= 16 && request - 16 < postMismatches)) crc ^= 1;
                return ack(cmd,crc);
            }
            if (cmd == SUB_KERNEL_GET_MAX_MSG_SIZE) return ack(cmd, 0x1000);
            if (cmd == SUB_KERNEL_GET_MAX_BLK_SIZE) return ack(cmd, 0x1000);
            if (cmd == SUB_KERNEL_PROG_VOLT) return ack(cmd, 0x02580000);
            return ack(cmd);
        };
    }
private slots:
    void init() {
        port.direct=true; port.writeState=CommunicationResult::State::Success;
        port.readState=CommunicationResult::State::Success; port.checkedReads=0; port.beforeRead={};
        cal.FullRomData = image(); cal.McuType = "SH7058";
        cal.Sh7058PreparedImageHash = SubaruSH7058Safety::fingerprint(cal.FullRomData);
        module = new FlashEcuSubaruDensoSH7058Can(&port, &cal, "write");
        module->mcu_type_index = 0;
        while (QString::fromLatin1(flashdevices[module->mcu_type_index].name) != "SH7058") ++module->mcu_type_index;
        module->flashbytescount = 4096;
        port.writes.clear(); port.setupCalls = 0; port.pending.clear();
        port.afterRead = {}; port.failWrite = {};
        crcRequests = 0; postMismatches=0; badCrc=-1; shortCrc=-1; initialDifference=true;
        setupReplies();
    }
    void cleanup() { delete module; module=nullptr; }
    void verification_data() {
        QTest::addColumn<int>("mismatches"); QTest::addColumn<int>("bad"); QTest::addColumn<int>("shortReply"); QTest::addColumn<int>("expected");
        QTest::newRow("all-match-after-program") << 0 << -1 << -1 << 0;
        QTest::newRow("one-post-program-mismatch") << 1 << -1 << -1 << 2;
        QTest::newRow("multiple-post-program-mismatches") << 3 << -1 << -1 << 2;
        QTest::newRow("failed-final-crc-request") << 0 << 16 << -1 << 1;
        QTest::newRow("malformed-final-crc-reply") << 0 << -1 << 16 << 1;
    }
    void verification() {
        QFETCH(int,mismatches); QFETCH(int,bad); QFETCH(int,shortReply); QFETCH(int,expected);
        postMismatches=mismatches; badCrc=bad; shortCrc=shortReply;
        const int result = module->write_mem(false);
        QCOMPARE(result,expected);
        QCOMPARE(module->operation_succeeded(result),expected==0);
        QCOMPARE(commands().count(SUB_KERNEL_BLANK_PAGE),1);
        QCOMPARE(commands().count(SUB_KERNEL_COMMIT_FLASH_BUFFER),1);
    }
    void uiResult_data() {
        QTest::addColumn<int>("mismatches"); QTest::addColumn<bool>("failure");
        QTest::newRow("success-dialog") << 0 << false;
        QTest::newRow("mismatch-failure-dialog") << 1 << false;
        QTest::newRow("crc-communication-failure-dialog") << 0 << true;
    }
    void uiResult() {
        QFETCH(int,mismatches); QFETCH(bool,failure);
        postMismatches=mismatches; if(failure) badCrc=16;
        QString outcome;
        QTimer timer; connect(&timer,&QTimer::timeout,[&]{
            for(auto *w:QApplication::topLevelWidgets()) if(auto *b=qobject_cast<QMessageBox*>(w)) {
                if(b->windowTitle()=="ECU Operation") outcome=b->text();
                b->button(QMessageBox::Ok)->click();
            }
        }); timer.start(1);
        module->run();
        QVERIFY(!outcome.isEmpty());
        QCOMPARE(module->operation_result(),failure ? 1 : mismatches ? 2 : 0);
        QCOMPARE(outcome.contains("was succesful"),mismatches==0 && !failure);
        if(mismatches) QVERIFY(outcome.contains("Verification failed"));
        if(failure) QVERIFY(outcome.contains("operation failed"));
    }
    void noChanges() {
        initialDifference=false;
        QCOMPARE(module->write_mem(false),0);
        QCOMPARE(commands().size(),16);
        for (int cmd : commands()) QCOMPARE(cmd,int(SUB_KERNEL_CRC));
    }
    void crcBounds_data() {
        QTest::addColumn<int>("size");
        for (int i=0;i<13;++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void crcBounds() {
        QFETCH(int,size);
        port.respond=[size](const QByteArray &){ return ack(SUB_KERNEL_CRC).left(size); };
        int modified=123;
        QCOMPARE(module->check_romcrc(reinterpret_cast<const uint8_t*>(cal.FullRomData.constData()),0,4096,&modified),1);
        QCOMPARE(modified,123);
    }
    void failedCrcWrite() {
        port.failWrite=[](const QByteArray &){return true;}; int modified=123;
        QCOMPARE(module->check_romcrc(reinterpret_cast<const uint8_t*>(cal.FullRomData.constData()),0,4096,&modified),1);
        QCOMPARE(modified,123);
    }
    void preflight_data() {
        QTest::addColumn<int>("size"); QTest::addColumn<QString>("mcu"); QTest::addColumn<bool>("definition"); QTest::addColumn<bool>("checksum"); QTest::addColumn<bool>("valid");
        QTest::newRow("empty") << 0 << QString("SH7058") << true << true << false;
        QTest::newRow("short") << 1048575 << QString("SH7058") << true << true << false;
        QTest::newRow("exact") << 1048576 << QString("SH7058") << true << true << true;
        QTest::newRow("oversized") << 1048577 << QString("SH7058") << true << true << false;
        QTest::newRow("missing-definition") << 1048576 << QString("SH7058") << false << true << false;
        QTest::newRow("missing-checksum") << 1048576 << QString("SH7058") << true << false << false;
        QTest::newRow("unknown-mcu") << 1048576 << QString("unknown") << true << true << false;
        QTest::newRow("wrong-mcu") << 1048576 << QString("SH7055") << true << true << false;
        QTest::newRow("short-no-definition") << 1048575 << QString("SH7058") << false << true << false;
        QTest::newRow("oversized-no-checksum") << 1048577 << QString("SH7058") << true << false << false;
    }
    void preflight() {
        QFETCH(int,size); QFETCH(QString,mcu); QFETCH(bool,definition); QFETCH(bool,checksum); QFETCH(bool,valid);
        cal.FullRomData.resize(size); cal.McuType=mcu;
        const auto original = cal.FullRomData;
        const auto prepared=SubaruSH7058Safety::prepare(original,mcu,definition,checksum);
        QCOMPARE(prepared.usable(),valid); QCOMPARE(cal.FullRomData,original);
        cal.Sh7058PreparedImageHash=prepared.usable() ? SubaruSH7058Safety::fingerprint(prepared.image) : QByteArray();
        if (!valid) {
            // Actual run() must reject before even opening/configuring a port.
            QTimer timer; connect(&timer,&QTimer::timeout,[]{
                for (auto *w : QApplication::topLevelWidgets())
                    if (auto *box=qobject_cast<QMessageBox*>(w)) box->accept();
            }); timer.start(1);
            module->run();
            QCOMPARE(port.setupCalls,0); QVERIFY(port.writes.isEmpty());
        } else { QCOMPARE(prepared.image.size(),1048576); }
    }
    void stalePreparation() {
        cal.FullRomData[0]=1;
        QTimer timer; connect(&timer,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets()) if(auto *b=qobject_cast<QMessageBox*>(w)) b->accept();}); timer.start(1);
        module->run(); QCOMPARE(port.setupCalls,0); QVERIFY(port.writes.isEmpty());
    }
    void inconsistentMap() {
        const auto *map=SubaruSH7058Safety::memoryMap("SH7058");
        QVERIFY(map); QVERIFY(SubaruSH7058Safety::validMap(map));
        flashblock blocks[16]; std::copy(map->fblocks,map->fblocks+16,blocks);
        flashdev_t bad={"SH7058",SH7058,1048576,16,blocks,map->rblocks,map->kblocks,map->eblocks};
        blocks[15].len+=4096; QVERIFY(!SubaruSH7058Safety::validMap(&bad));
        QCOMPARE(module->reflash_block(reinterpret_cast<const uint8_t*>(cal.FullRomData.constData()),&bad,0,false),1);
        QVERIFY(port.writes.isEmpty());
    }
    void definitionDefaults() {
        FileActions::EcuCalDefStructure fresh;
        QVERIFY(!fresh.use_romraider_definition); QVERIFY(!fresh.use_ecuflash_definition);
        QVERIFY(fresh.Sh7058PreparedImageHash.isEmpty());
    }
    void protocolRoutes() {
        for (const char *name : {"sub_ecu_denso_sh7058_can", "sub_ecu_denso_sh7058_can_ecutek",
                "sub_ecu_denso_sh7058_can_ecutek_racerom", "sub_ecu_denso_sh7058_can_ecutek_racerom_alt",
                "sub_ecu_denso_sh7058_can_cobb"}) QVERIFY(SubaruSH7058Safety::usesModule(name));
        QVERIFY(!SubaruSH7058Safety::usesModule("sub_ecu_denso_sh7058_can_diesel"));
        QVERIFY(!SubaruSH7058Safety::usesModule("sub_ecu_denso_sh7058"));
    }
    void sourceBounds() {
        const auto *src=reinterpret_cast<const uint8_t*>(cal.FullRomData.constData()); int modified=0;
        QCOMPARE(module->check_romcrc(src,1048575,2,&modified),1);
        QCOMPARE(module->flash_block(src,1048576,4096),1);
        cal.FullRomData.chop(1);
        QCOMPARE(module->write_mem(false),1); QVERIFY(port.writes.isEmpty());
    }
    void checksumDisabled() {
        const auto before=image(); const auto result=C::evaluate(before,0xFFB80,204);
        QCOMPARE(int(result.state),int(C::State::Disabled)); QCOMPARE(result.image.size(),before.size());
        QCOMPARE(SubaruSH7058Safety::fingerprint(result.image),SubaruSH7058Safety::fingerprint(before));
        const auto second=C::evaluate(result.image,0xFFB80,204); QCOMPARE(second.image,before);
    }
    void checksumInvalid_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("missing-table") << 0; QTest::newRow("short-table") << 1;
        QTest::newRow("reversed-range") << 2; QTest::newRow("out-of-bounds") << 3;
        QTest::newRow("unaligned") << 4;
    }
    void checksumInvalid() {
        QFETCH(int,kind); auto bytes=image(); uint32_t length=204;
        if(kind==0) length=0;
        if(kind==1) length=203;
        if(kind>=2) { put32(bytes,0xFFB80,kind==2?8:4); put32(bytes,0xFFB84,kind==2?4:kind==3?1048580:9); put32(bytes,0xFFB88,0); }
        const auto before=bytes; auto result=C::evaluate(bytes,0xFFB80,length);
        QCOMPARE(int(result.state),int(C::State::Invalid)); QVERIFY(!result.usable()); QVERIFY(result.image.isEmpty()); QCOMPARE(bytes,before);
        if(kind>=2) {
            cal.FullRomData=bytes;
            QVERIFY(!SubaruSH7058Safety::prepare(bytes,"SH7058",true,true).usable());
        }
    }
    void preparedChecksumCorrection() {
        auto bytes=image();
        put32(bytes,4,1); put32(bytes,8,2);
        for(int i=0;i<17;++i) { put32(bytes,0xFFB80+i*12,0); put32(bytes,0xFFB84+i*12,0); put32(bytes,0xFFB88+i*12,0x5AA5A55A); }
        put32(bytes,0xFFB80,4); put32(bytes,0xFFB84,12); put32(bytes,0xFFB88,0);
        const auto result=SubaruSH7058Safety::prepare(bytes,"SH7058",true,true);
        QCOMPARE(int(result.state),int(C::State::Corrected));
        auto expected=bytes; put32(expected,0xFFB88,0x5AA5A557);
        QCOMPARE(result.image,expected); QCOMPARE(result.image.size(),1048576);
        const auto valid=SubaruSH7058Safety::prepare(expected,"SH7058",true,true);
        QCOMPARE(int(valid.state),int(C::State::Valid)); QCOMPARE(valid.image,expected);
    }
    void invalidChecksumBlocksRun() {
        put32(cal.FullRomData,0xFFB80,4); put32(cal.FullRomData,0xFFB84,1048580); put32(cal.FullRomData,0xFFB88,0);
        const auto prepared=SubaruSH7058Safety::prepare(cal.FullRomData,"SH7058",true,true);
        QVERIFY(!prepared.usable()); cal.Sh7058PreparedImageHash.clear();
        QTimer timer; connect(&timer,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets()) if(auto *b=qobject_cast<QMessageBox*>(w)) b->accept();}); timer.start(1);
        module->run(); QCOMPARE(port.setupCalls,0); QVERIFY(port.writes.isEmpty());
    }
    void legacyChecksumPreservesImage() {
        auto before=image();
        QTimer timer; connect(&timer,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets()) if(auto *b=qobject_cast<QMessageBox*>(w)) b->accept();}); timer.start(1);
        QCOMPARE(C::calculate_checksum(before,0xFFB80,204),before);
        // Legacy callers also retain bytes on invalid configuration; SH7058
        // programming uses the typed result and blocks instead of accepting it.
        QCOMPARE(C::calculate_checksum(before,0xFFB80,0),before);
    }
    void checksumArithmetic() {
        QByteArray bytes(64,'\0'); put32(bytes,4,1); put32(bytes,8,2);
        put32(bytes,32,4); put32(bytes,36,12); put32(bytes,40,0);
        const auto corrected=C::evaluate(bytes,32,12);
        QCOMPARE(int(corrected.state),int(C::State::Corrected));
        auto expected=bytes; put32(expected,40,0x5AA5A557);
        QCOMPARE(corrected.image,expected); QCOMPARE(corrected.image.size(),bytes.size());
        const auto valid=C::evaluate(expected,32,12);
        QCOMPARE(int(valid.state),int(C::State::Valid)); QCOMPARE(valid.image,expected);
    }
    void cancellation_data() {
        QTest::addColumn<QString>("phase"); QTest::addColumn<bool>("testWrite");
        for (const char *p : {"before-compare","final-compare-delay","before-init","before-erase","erase-submit","before-flash-enable","after-erase","during-buffer","before-commit","commit-submit"})
            QTest::newRow(p) << QString(p) << false;
        QTest::newRow("before-validate") << QString("before-commit") << true;
        QTest::newRow("validate-submit") << QString("commit-submit") << true;
    }
    void cancellation() {
        QFETCH(QString,phase); QFETCH(bool,testWrite); module->test_write=testWrite;
        int buffers=0; QVector<QByteArray> atCancel;
        auto observe=[&]{ cancel(); atCancel=port.writes; };
        connect(module,&FlashEcuSubaruDensoSH7058Can::LOG_I,module,[&](QString text,bool,bool){
            if ((phase=="erase-submit" && text=="Erasing flash page...") ||
                (phase=="commit-submit" && (text.startsWith("Committ flash addr:") || text.startsWith("Validate flash addr:")))) observe();
        });
        port.afterRead=[&](const QByteArray &a){
            int cmd=uint8_t(a.at(8));
            if (phase=="final-compare-delay" && cmd==SUB_KERNEL_CRC && crcRequests==16)
                QTimer::singleShot(0,module,[&]{observe();});
            if ((phase=="before-flash-enable" && cmd==SUB_KERNEL_GET_MAX_BLK_SIZE) ||
                (phase=="before-erase" && cmd==SUB_KERNEL_PROG_VOLT) ||
                (phase=="after-erase" && cmd==SUB_KERNEL_BLANK_PAGE)) observe();
            if (cmd==SUB_KERNEL_WRITE_FLASH_BUFFER) {
                ++buffers;
                if ((phase=="during-buffer" && buffers==2) || (phase=="before-commit" && buffers==8)) observe();
            }
        };
        int result;
        if(phase=="before-compare") {
            observe(); result=module->write_mem(testWrite);
        } else if(phase=="before-init") {
            observe(); result=module->init_flash_write();
        } else result=module->write_mem(testWrite);
        QVERIFY(module->kill_process); QVERIFY(!module->operation_succeeded(result)); QVERIFY(result!=0);
        QCOMPARE(port.writes,atCancel); // absolutely no commands after cancellation
        if(phase=="final-compare-delay" || phase=="before-erase" || phase=="before-init" || phase=="before-compare" || phase=="erase-submit" || phase=="before-flash-enable")
            QCOMPARE(commands().count(SUB_KERNEL_BLANK_PAGE),0);
        QCOMPARE(commands().count(SUB_KERNEL_COMMIT_FLASH_BUFFER),0);
        QCOMPARE(commands().count(SUB_KERNEL_VALIDATE_FLASH_BUFFER),0);
    }
    void normalSequence_data() { QTest::addColumn<bool>("testWrite"); QTest::newRow("commit")<<false; QTest::newRow("validate")<<true; }
    void normalSequence() {
        QFETCH(bool,testWrite); module->test_write=testWrite;
        QCOMPARE(module->write_mem(testWrite),0);
        QVector<int> expected; for(int i=0;i<16;++i) expected<<SUB_KERNEL_CRC;
        expected<<SUB_KERNEL_GET_MAX_MSG_SIZE<<SUB_KERNEL_GET_MAX_BLK_SIZE
                <<(testWrite?SUB_KERNEL_FLASH_DISABLE:SUB_KERNEL_FLASH_ENABLE)<<SUB_KERNEL_PROG_VOLT<<SUB_KERNEL_BLANK_PAGE;
        for(int i=0;i<8;++i) expected<<SUB_KERNEL_WRITE_FLASH_BUFFER;
        expected<<(testWrite?SUB_KERNEL_VALIDATE_FLASH_BUFFER:SUB_KERNEL_COMMIT_FLASH_BUFFER);
        for(int i=0;i<16;++i) expected<<SUB_KERNEL_CRC;
        QCOMPARE(commands(),expected);
        QByteArray trace;
        for (const auto &a:port.writes) {
            QByteArray length(4, '\0'); put32(length,0,uint32_t(a.size()));
            trace += length; trace += a;
        }
        // Captured by compiling the Task 004 production module against this
        // same fake and synthetic fixture. Covers every submitted byte/order.
        QCOMPARE(SubaruSH7058Safety::fingerprint(trace).toHex(), QByteArray(testWrite ?
            "a7f629163a60e21569202a7ebe6bd74a99353826ed6238b076abaca251a85703" :
            "313959741b6a29c3182871eb3ee854976018c7c7cd8ad2231b9b0e3244788adf"));
        for(const auto &a:port.writes) {
            QCOMPARE(a.left(4),QByteArray::fromHex("000007e0"));
            if(uint8_t(a.at(8))==SUB_KERNEL_WRITE_FLASH_BUFFER) {
                QCOMPARE(a.size(),525); auto addr=get32(a,9);
                QCOMPARE(a.mid(13),cal.FullRomData.mid(addr,512));
            }
        }
    }
private:
    // Test helpers use the existing private friend seam.
    static QByteArray diagnosticRequest(QByteArray bytes) { return QByteArray::fromHex("000007e0")+bytes; }
    static QByteArray diagnosticResponse(QByteArray bytes) { return QByteArray::fromHex("000007e8")+bytes; }
    void diagnosticReplies() {
        port.respond=[](const QByteArray &request) {
            const auto data=request.mid(4);
            if(data.startsWith(QByteArray::fromHex("beef"))) return QByteArray(); // kernel absent, ordinary timeout
            if(data==QByteArray::fromHex("aa")) return diagnosticResponse(QByteArray::fromHex("ea0000000102030405"));
            if(data==QByteArray::fromHex("0902")) return diagnosticResponse(QByteArray::fromHex("490201")+QByteArray("12345678901234567"));
            if(data==QByteArray::fromHex("0904")) return diagnosticResponse(QByteArray::fromHex("490401")+QByteArray("CALTEST1"));
            if(data==QByteArray::fromHex("0906")) return diagnosticResponse(QByteArray::fromHex("49060111223344"));
            if(data==QByteArray::fromHex("1043")) return diagnosticResponse(QByteArray::fromHex("7f1012"));
            if(data.startsWith(QByteArray::fromHex("10"))) return diagnosticResponse(QByteArray::fromHex("50")+data.mid(1));
            if(data==QByteArray::fromHex("2701")) return diagnosticResponse(QByteArray::fromHex("670101020304"));
            if(data.startsWith(QByteArray::fromHex("2702"))) return diagnosticResponse(QByteArray::fromHex("6702"));
            return QByteArray();
        };
    }
private slots:
    void writeFailurePhases_data() {
        QTest::addColumn<int>("index");
        for(int index : {0,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,45})
            QTest::newRow(qPrintable(QString("submission-%1").arg(index))) << index;
    }
    void writeFailurePhases() {
        QFETCH(int,index); int attempted=0, readsAtFailure=-1;
        port.failWrite=[&](const QByteArray &){
            if(attempted++==index) { readsAtFailure=port.checkedReads; port.pending=ack(SUB_KERNEL_COMMIT_FLASH_BUFFER); return true; }
            return false;
        };
        QCOMPARE(module->write_mem(false),1);
        QCOMPARE(port.writes.size(),index+1); QCOMPARE(port.checkedReads,readsAtFailure);
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::TransportError));
        QVERIFY(module->communication.hasStatus); QCOMPARE(module->communication.status,7L);
        if(index<=20) QCOMPARE(commands().count(SUB_KERNEL_WRITE_FLASH_BUFFER),0);
        if(index<=28) QCOMPARE(commands().count(SUB_KERNEL_COMMIT_FLASH_BUFFER),0);
    }
    void diagnosticFailure_data() {
        QTest::addColumn<int>("index");
        for(int i=0;i<10;++i) QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void diagnosticFailure() {
        QFETCH(int,index); diagnosticReplies(); int attempted=0, readsAtFailure=-1;
        port.failWrite=[&](const QByteArray &){ if(attempted++==index) {readsAtFailure=port.checkedReads; return true;} return false;};
        QCOMPARE(module->connect_bootloader(),1);
        QCOMPARE(port.writes.size(),index+1); QCOMPARE(port.checkedReads,readsAtFailure);
        QVERIFY(!module->kernel_alive);
    }
    void diagnosticNormal() {
        diagnosticReplies(); QCOMPARE(module->connect_bootloader(),0);
        QCOMPARE(port.writes.size(),10);
        QCOMPARE(port.writes.last(),diagnosticRequest(QByteArray::fromHex("1002")));
    }
    void sessionAmbiguity_data() {QTest::addColumn<bool>("both");QTest::newRow("both-accepted")<<true;QTest::newRow("neither-accepted")<<false;}
    void sessionAmbiguity() {
        QFETCH(bool,both); diagnosticReplies(); auto original=port.respond;
        port.respond=[&](const QByteArray &a){
            if(uint8_t(a.at(4))==0x10) return diagnosticResponse(both ? QByteArray::fromHex("50")+a.mid(5) : QByteArray::fromHex("7f1012"));
            return original(a);
        };
        QCOMPARE(module->connect_bootloader(),1); QCOMPARE(port.writes.size(),7);
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::Malformed));
    }
    void optionalMetadata_data() {QTest::addColumn<bool>("negative");QTest::newRow("unsupported")<<true;QTest::newRow("no-response")<<false;}
    void optionalMetadata() {
        QFETCH(bool,negative); diagnosticReplies(); auto original=port.respond;
        port.respond=[&](const QByteArray &a){const auto sid=uint8_t(a.at(4));
            if(sid==0xAA || sid==9) return negative ? diagnosticResponse(QByteArray::fromHex("7f")+a.mid(4,1)+QByteArray::fromHex("12")) : QByteArray();
            return original(a);
        };
        QCOMPARE(module->connect_bootloader(),0); QCOMPARE(port.writes.size(),10);
    }
    void pendingDoesNotAdvance_data() {QTest::addColumn<int>("stage");QTest::newRow("metadata")<<1;QTest::newRow("session")<<5;}
    void pendingDoesNotAdvance() {
        QFETCH(int,stage); diagnosticReplies(); auto original=port.respond; int step=0;
        port.respond=[&](const QByteArray &a){if(step++==stage) return diagnosticResponse(QByteArray::fromHex("7f")+a.mid(4,1)+QByteArray::fromHex("78"));return original(a);};
        QCOMPARE(module->connect_bootloader(),1); QCOMPARE(port.writes.size(),stage+1);
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::Unsupported));
    }
    void readOutcome_data() {
        QTest::addColumn<int>("state");
        using S=CommunicationResult::State;
        QTest::newRow("timeout")<<int(S::Timeout); QTest::newRow("driver-error")<<int(S::TransportError);
        QTest::newRow("disconnected")<<int(S::Disconnected); QTest::newRow("partial")<<int(S::Malformed);
    }
    void readOutcome() {
        QFETCH(int,state); port.readState=CommunicationResult::State(state);
        QCOMPARE(module->write_mem(false),1); QCOMPARE(port.writes.size(),1);
        QCOMPARE(int(module->communication.state),state);
        QVERIFY(module->communication.hasStatus); QCOMPARE(module->communication.status,7L);
    }
    void remoteRejected_data() {
        QTest::addColumn<QString>("condition");
        QTest::newRow("invalid-reply")<<QString("invalid");
        QTest::newRow("RPC-timeout")<<QString("timeout");
        QTest::newRow("endpoint-disconnected")<<QString("disconnected");
    }
    void remoteRejected() {
        QFETCH(QString,condition); Q_UNUSED(condition);
        // No RPC is issued in any remote state; thus no invalid/default reply
        // can be reinterpreted as a successful empty write.
        port.direct=false;
        QVERIFY(!module->submit(diagnosticRequest(QByteArray::fromHex("aa"))));
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::Unsupported));
        QTimer timer; connect(&timer,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets()) if(auto *b=qobject_cast<QMessageBox*>(w)) b->accept();}); timer.start(1);
        module->run(); QCOMPARE(module->operation_result(),1); QCOMPARE(port.setupCalls,0); QVERIFY(port.writes.isEmpty()); QCOMPARE(port.checkedReads,0);
    }
    void parserFrames_data() {
        QTest::addColumn<QByteArray>("request"); QTest::addColumn<QByteArray>("response"); QTest::addColumn<int>("expected");
        using S=CommunicationResult::State;
        auto add=[&](const QString &name,const QByteArray &request,const QByteArray &valid,bool kernel) {
            auto row=[&](QString suffix,QByteArray reply,S result){QTest::newRow(qPrintable(name+suffix))<<request<<reply<<int(result);};
            row("/valid",valid,S::Success);
            QVector<int> lengths={0,1,4,5,6,7,8};
            if(!lengths.contains(valid.size()-1)) lengths.append(valid.size()-1);
            for(int n : lengths) if(n<valid.size()) row(QString("/short-%1").arg(n),valid.left(n),S::Malformed);
            auto bad=valid; bad[3]=char(0xE9); row("/wrong-address",bad,S::Malformed);
            bad=valid; bad[4]=char(0x7E); row("/wrong-service",bad,S::Malformed);
            if(kernel) {
                bad=valid; bad[8]=char(uint8_t(bad.at(8))^1); row("/wrong-command",bad,S::Malformed);
                bad=valid; bad[7]=char(uint8_t(bad[7])+1); row("/bad-declared-length",bad,S::Malformed);
                row("/trailing",valid+QByteArray(1,'x'),S::Malformed);
                bad=valid+QByteArray(1,'x'); const int length=bad.size()-8; bad[6]=char(length>>8);bad[7]=char(length);
                row("/declared-extra",bad,uint8_t(request.at(8))==SUB_KERNEL_ID ? S::Success : S::Malformed);
                bad=valid.left(8)+QByteArray::fromHex("7f")+request.mid(8,1)+QByteArray::fromHex("22"); bad[6]=0;bad[7]=3;
                row("/negative",bad,S::Malformed);
            } else {
                row("/negative",diagnosticResponse(QByteArray::fromHex("7f")+request.mid(4,1)+QByteArray::fromHex("22")),S::Rejected);
                row("/pending",diagnosticResponse(QByteArray::fromHex("7f")+request.mid(4,1)+QByteArray::fromHex("78")),S::Unsupported);
                row("/unrelated-negative",diagnosticResponse(QByteArray::fromHex("7f0022")),S::Malformed);
                const auto sid=uint8_t(request.at(4));
                const bool variable=sid==0xAA || sid==0x10 || (sid==9 && uint8_t(request.at(5))==4);
                row("/trailing",valid+QByteArray(1,'x'),variable ? S::Success : S::Malformed);
                if(sid==9 || sid==0x10 || sid==0x27) {bad=valid;bad[5]=char(uint8_t(bad.at(5))+1);row("/wrong-subfunction",bad,S::Malformed);}
            }
        };
        for(int cmd : {SUB_KERNEL_ID,SUB_KERNEL_CRC,SUB_KERNEL_GET_MAX_MSG_SIZE,SUB_KERNEL_GET_MAX_BLK_SIZE,SUB_KERNEL_PROG_VOLT,
              SUB_KERNEL_FLASH_ENABLE,SUB_KERNEL_FLASH_DISABLE,SUB_KERNEL_BLANK_PAGE,SUB_KERNEL_WRITE_FLASH_BUFFER,
              SUB_KERNEL_VALIDATE_FLASH_BUFFER,SUB_KERNEL_COMMIT_FLASH_BUFFER,SUB_KERNEL_READ_AREA}) {
            QByteArray request=QByteArray::fromHex("000007e0beef0001");request.append(char(cmd));
            auto valid=ack(cmd,0x1000);
            if(cmd==SUB_KERNEL_READ_AREA) {request.append(QByteArray::fromHex("000000000400"));valid=ack(cmd)+QByteArray(1024,'R');valid[6]=4;valid[7]=1;}
            add(QString("kernel-%1").arg(cmd),request,valid,true);
        }
        const QVector<QPair<QByteArray,QByteArray>> frames={
            {QByteArray::fromHex("aa"),QByteArray::fromHex("ea0000000102030405")},
            {QByteArray::fromHex("0902"),QByteArray::fromHex("490201")+QByteArray(17,'V')},
            {QByteArray::fromHex("0904"),QByteArray::fromHex("490401")+QByteArray("C")},
            {QByteArray::fromHex("0906"),QByteArray::fromHex("49060111223344")},
            {QByteArray::fromHex("1003"),QByteArray::fromHex("5003")},
            {QByteArray::fromHex("1043"),QByteArray::fromHex("5043")},
            {QByteArray::fromHex("2701"),QByteArray::fromHex("670101020304")},
            {QByteArray::fromHex("270200000000"),QByteArray::fromHex("6702")},
            {QByteArray::fromHex("a800000001000002000003000004"),QByteArray::fromHex("e811223344")}};
        for(const auto &pair:frames) add("diagnostic-"+pair.first.toHex(),diagnosticRequest(pair.first),diagnosticResponse(pair.second),false);
    }
    void parserFrames() {
        QFETCH(QByteArray,request); QFETCH(QByteArray,response); QFETCH(int,expected);
        QCOMPARE(int(module->validate_response(request,response)),expected);
    }
    void malformedStopsDestruction() {
        auto original=port.respond;
        port.respond=[&](const QByteArray &a){if(uint8_t(a.at(8))==SUB_KERNEL_BLANK_PAGE) return ack(SUB_KERNEL_COMMIT_FLASH_BUFFER); return original(a);};
        QCOMPARE(module->write_mem(false),1);
        QCOMPARE(commands().count(SUB_KERNEL_WRITE_FLASH_BUFFER),0);
        QCOMPARE(commands().count(SUB_KERNEL_COMMIT_FLASH_BUFFER),0);
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::Malformed));
    }
    void completeRead_data() {
        QTest::addColumn<QString>("fault");
        for(const char *p : {"none","missing","short","header-only","oversized","driver","cancel","cancel-final-progress"}) QTest::newRow(p)<<QString(p);
    }
    void completeRead() {
        QFETCH(QString,fault); const auto before=cal.FullRomData;
        int pages=0;
        port.respond=[&](const QByteArray &a){
            const int page=pages++;
            if(page==10 && fault=="missing") return QByteArray();
            if(page==10 && fault=="driver") {port.readState=CommunicationResult::State::TransportError; return QByteArray();}
            if(page==10 && fault=="cancel") cancel();
            if(page==1023 && fault=="cancel-final-progress") QTimer::singleShot(0,module,[&]{cancel();});
            int length=1024;
            if(page==10 && fault=="short") length=1023;
            if(page==10 && fault=="header-only") length=0;
            if(page==10 && fault=="oversized") length=1025;
            auto response=ack(SUB_KERNEL_READ_AREA)+QByteArray(length,char(page%127));
            const unsigned declared=response.size()-8;response[6]=char(declared>>8);response[7]=char(declared);
            return response;
        };
        const int result=module->read_mem(0,1048576);
        if(fault=="none") {
            QCOMPARE(result,0); QCOMPARE(pages,1024); QCOMPARE(cal.FullRomData.size(),1048576);
            for(int i=0;i<1024;++i) QCOMPARE(cal.FullRomData.mid(i*1024,1024),QByteArray(1024,char(i%127)));
            for(int i=0;i<1024;++i) { QCOMPARE(port.writes[i].size(),15); QCOMPARE(get32(port.writes[i],9),uint32_t(i*1024)); }
        } else {
            QVERIFY(result!=0); QCOMPARE(pages,fault=="cancel-final-progress" ? 1024 : 11); QCOMPARE(cal.FullRomData,before);
            if(fault.startsWith("cancel")) QCOMPARE(result,3);
        }
    }
    void failedWriteUi() {
        port.failWrite=[](const QByteArray &a){return a.size()>=9 && uint8_t(a.at(8))==SUB_KERNEL_BLANK_PAGE;};
        QString outcome;
        QTimer timer; connect(&timer,&QTimer::timeout,[&]{for(auto *w:QApplication::topLevelWidgets()) if(auto *b=qobject_cast<QMessageBox*>(w)) {
            if(b->windowTitle()=="ECU Operation") outcome=b->text();
            b->button(QMessageBox::Ok)->click();
        }}); timer.start(1);
        module->run(); QCOMPARE(module->operation_result(),1); QVERIFY(outcome.contains("operation failed")); QVERIFY(!outcome.contains("was succesful"));
        QCOMPARE(commands().count(SUB_KERNEL_WRITE_FLASH_BUFFER),0);
    }
    void malformedDiagnosticStops_data() { QTest::addColumn<int>("stage"); for(int i=1;i<10;++i) QTest::newRow(qPrintable(QString::number(i)))<<i; }
    void malformedDiagnosticStops() {
        QFETCH(int,stage); diagnosticReplies(); auto reply=port.respond; int step=0;
        port.respond=[&](const QByteArray &a){ auto bytes=reply(a); if(step++==stage) bytes.resize(5); return bytes; };
        QCOMPARE(module->connect_bootloader(),1); QCOMPARE(port.writes.size(),stage+1);
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::Malformed));
    }
    void readModelRejected_data() {QTest::addColumn<QString>("mcu");QTest::newRow("unknown")<<QString("unknown");QTest::newRow("wrong-model")<<QString("SH7055");}
    void readModelRejected() {
        QFETCH(QString,mcu);cal.McuType=mcu;module->cmd_type="read";
        QTimer timer;connect(&timer,&QTimer::timeout,[]{for(auto *w:QApplication::topLevelWidgets())if(auto *b=qobject_cast<QMessageBox*>(w))b->accept();});timer.start(1);
        module->run();QCOMPARE(module->operation_result(),1);QCOMPARE(port.setupCalls,0);QVERIFY(port.writes.isEmpty());
    }
    void kernelPreparation_data() {
        QTest::addColumn<int>("length"); QTest::addColumn<uint>("destination"); QTest::addColumn<bool>("valid");
        for(int n : {0,1,3,4,128,129,6428,0x9000,0x9001}) QTest::newRow(qPrintable(QString::number(n)))<<n<<uint(0xFFFF3000)<<bool(n>=4 && n<=0x9000);
        QTest::newRow("below-RAM")<<128<<uint(0xFFFF2F80)<<false;
        QTest::newRow("past-RAM")<<128<<uint(0xFFFFC000)<<false;
        QTest::newRow("unaligned")<<128<<uint(0xFFFF3001)<<false;
    }
    void kernelPreparation() {
        QFETCH(int,length); QFETCH(uint,destination); QFETCH(bool,valid);
        QByteArray output; const QByteArray input(length,'K');
        QCOMPARE(module->prepare_kernel(input,destination,output),valid);
        if(valid) {
            QCOMPARE(output.size(),((length+127)/128)*128); QVERIFY(!output.isEmpty()); QCOMPARE(output.size()%128,0);
            const auto layout=SubaruSH7058::KernelLayout::create(length,destination,0xFFFF3000,0x9000);
            QVERIFY(layout.valid()); QCOMPARE(layout.preparedSize(),uint32_t(output.size()));
            QByteArray reassembled;
            for(uint32_t i=0;i<layout.blockCount();++i) {
                SubaruSH7058::KernelLayout::Block block; QVERIFY(layout.block(i,block));
                QCOMPARE(block.length,128u); reassembled+=output.mid(int(block.offset),int(block.length));
            }
            QCOMPARE(reassembled,output);
            SubaruSH7058::KernelLayout::Block extra; QVERIFY(!layout.block(layout.blockCount(),extra));
            // Captured from actual Task006A upload_kernel using only synthetic
            // K-filled files; concatenated nonempty transfer payloads.
            const QMap<int,QByteArray> expected={
                {4,"422b4f871c3ed2192020620437fb242078df0ceebf6b45d81fbf0a2c87e4882f"},
                {128,"627d8c2cf046b7b2ca50d4635b9a2e40ce32cf90ea5ac80d64291c7a7a31d3f6"},
                {129,"8c3d1e13522a7f4509a9778248ec44537f25fbc0cba8569649f0c7371bf3910f"},
                {6428,"26aef047094d69c844ae0a90db8db478821435a2bae9c8c0092777ef0cabcb1b"},
                {0x9000,"63a61a52a372f1579779f139beee42f80148dddd6e3efa4d03e68516bf578c1c"}};
            QCOMPARE(SubaruSH7058Safety::fingerprint(output).toHex(),expected.value(length));
        }
        else QVERIFY(output.isEmpty());
        QVERIFY(port.writes.isEmpty());
    }
    void kernelUploadGated_data() {
        QTest::addColumn<int>("size");
        for(int n : {4,128,129,6428,0x9000}) QTest::newRow(qPrintable(QString::number(n)))<<n;
    }
    void kernelUploadGated() {
        QFETCH(int,size);
        QTemporaryFile file; QVERIFY(file.open()); QCOMPARE(file.write(QByteArray(size,'K')),qint64(size));file.flush();file.close();
        QCOMPARE(module->upload_kernel(file.fileName(),0xFFFF3000),1);
        QCOMPARE(int(module->communication.state),int(CommunicationResult::State::Unsupported));
        QVERIFY(port.writes.isEmpty());
        cancel(); QCOMPARE(module->upload_kernel(file.fileName(),0xFFFF3000),3); QVERIFY(port.writes.isEmpty());
    }

};
int main(int argc, char **argv) {
    // Preserve the last test name even if the process terminates abnormally.
    std::setvbuf(stdout,nullptr,_IONBF,0);
    std::setvbuf(stderr,nullptr,_IONBF,0);
    std::fprintf(stderr,"Safety harness phase: QApplication construction\n");
    QApplication app(argc,argv);
    app.setAttribute(Qt::AA_Use96Dpi,true);
    std::fprintf(stderr,"Safety harness phase: fixture construction\n");
    SubaruSafetyTest test;
    std::fprintf(stderr,"Safety harness phase: QtTest execution\n");
    return QTest::qExec(&test,argc,argv);
}
#include "safety.moc"
