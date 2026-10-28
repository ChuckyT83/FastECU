// Native Windows: actual exclusive production backend + only our mock DLL.
#include <QtTest>
#include <QLibrary>
#include "../../serial_port/sh7058_identification_session.h"
using namespace SubaruSH7058;
class IdentitySessionTest : public QObject {
    Q_OBJECT
    QLibrary mock;
    using Reset=void(PT_CALL*)();using Fail=void(PT_CALL*)(const char*);
    using Metadata=void(PT_CALL*)(int);using Trace=const char*(PT_CALL*)();
    using Read=void(PT_CALL*)(long,unsigned long,unsigned long,unsigned long,unsigned long);
    using Bytes=void(PT_CALL*)(long,unsigned long,const unsigned char*,unsigned long,unsigned long);
    using Write=void(PT_CALL*)(long,unsigned long);using Value=unsigned long(PT_CALL*)(int);
    Reset reset=nullptr;Fail fail=nullptr;Metadata metadata=nullptr;Trace trace=nullptr;
    Read read=nullptr;Bytes bytes=nullptr;Write write=nullptr;Value value=nullptr,byte=nullptr;
    QString path;
    QByteArray ea() const {return QByteArray::fromHex("000007e8eaa21011a62a907107")+QByteArray(96,0);}
    QByteArray cal() const {return QByteArray::fromHex("000007e8490401")+"ZE5T201V"+QByteArray(8,0);}
    void response(QByteArray b) {bytes(0,1,reinterpret_cast<const unsigned char*>(b.constData()),b.size(),0);}
    void queue() {read(ERR_BUFFER_EMPTY,0,0,0,0);response(ea());read(ERR_BUFFER_EMPTY,0,0,0,0);response(cal());}
private slots:
    void initTestCase() {
        path=QDir::current().absoluteFilePath("mock-j2534.dll");mock.setFileName(path);QVERIFY(mock.load());
#define GET(v,T,n) v=reinterpret_cast<T>(mock.resolve(n));QVERIFY(v)
        GET(reset,Reset,"MockReset");GET(fail,Fail,"MockSetFailure");GET(metadata,Metadata,"MockSetMetadata");GET(trace,Trace,"MockTrace");
        GET(read,Read,"MockTransportRead");GET(bytes,Bytes,"MockTransportReadBytes");GET(write,Write,"MockTransportWrite");GET(value,Value,"MockTransportValue");GET(byte,Value,"MockTransportByte");
#undef GET
    }
    void init(){reset();qApp->setProperty("guiSmokeTest",false);}
    void success() {
        queue();SH7058IdentificationSession session(path);std::atomic<bool> cancel{false};
        const auto r=identify(session,cancel,2000);
        QVERIFY2(r.complete,qPrintable(r.detail));QVERIFY(r.cleanupConfirmed);QCOMPARE(r.identity.source,Identification::Source::ObservedDiagnostic);
        QCOMPARE(r.identity.ecu,QString("A62A907107"));QCOMPARE(r.identity.calibration,QString("ZE5T201V"));QVERIFY(r.identity.firmware.isEmpty());
        QCOMPARE(value(0),2UL);QCOMPARE(value(5),0UL);QCOMPARE(value(2),0UL); // No voltage or periodic operation.
        QByteArray last;for(unsigned long i=0;i<value(7);++i)last+=char(byte(int(i)));QCOMPARE(last,identityRequest(true));
        QCOMPARE(QString::fromLatin1(trace()).count("disconnect:99;close:41;"),1);QCOMPARE(session.generation(),quint64(0));QVERIFY(!session.acquire());
    }
    void whitelist() {
        SH7058IdentificationSession session(path);QVERIFY(session.acquire());
        for(const auto &frame:{QByteArray::fromHex("000007e01003"),QByteArray::fromHex("000007e02701"),QByteArray::fromHex("000007e01101"),QByteArray::fromHex("000007e0beef000101000000"),QByteArray::fromHex("000007e134"),QByteArray::fromHex("000007e0aa00")})
            QCOMPARE(session.submit(frame).state,CommunicationResult::State::Unsupported);
        QCOMPARE(value(0),0UL);QVERIFY(session.release());
    }
    void partialAcquisition_data(){QTest::addColumn<QString>("operation");for(const auto *op:{"open","connect","config","filter"})QTest::newRow(op)<<QString(op);}
    void partialAcquisition() {
        QFETCH(QString,operation);fail(operation.toLatin1().constData());SH7058IdentificationSession session(path);std::atomic<bool> cancel{false};
        const auto r=identify(session,cancel,2000);QVERIFY(!r.complete);QVERIFY(!r.acquired);QCOMPARE(value(0),0UL);
        const QString calls=QString::fromLatin1(trace());QCOMPARE(calls.count("close:41;"),operation=="open"?0:1);
        QCOMPARE(calls.count("disconnect:99;"),(operation=="config"||operation=="filter")?1:0);
    }
    void incompleteMetadata_data(){QTest::addColumn<int>("mode");for(int mode:{1,2,3,4})QTest::newRow(qPrintable(QString::number(mode)))<<mode;}
    void incompleteMetadata(){QFETCH(int,mode);metadata(mode);SH7058IdentificationSession session(path);QVERIFY(!session.acquire());QCOMPARE(value(0),0UL);QCOMPARE(QString::fromLatin1(trace()).count("close:41;"),1);}
    void cleanupFailure_data(){QTest::addColumn<QString>("operation");QTest::newRow("disconnect")<<QString("disconnect");QTest::newRow("close")<<QString("close");}
    void cleanupFailure(){QFETCH(QString,operation);queue();fail(operation.toLatin1().constData());SH7058IdentificationSession session(path);std::atomic<bool> cancel{false};const auto r=identify(session,cancel,2000);QVERIFY(!r.complete);QVERIFY(!r.cleanupConfirmed);QVERIFY(!session.cleanup().confirmed());QVERIFY(!session.release());QCOMPARE(QString::fromLatin1(trace()).count("close:41;"),1);}
    void zeroCount(){queue();write(0,0);SH7058IdentificationSession session(path);std::atomic<bool> cancel{false};const auto r=identify(session,cancel,2000);QVERIFY(!r.complete);QVERIFY(r.cleanupConfirmed);QCOMPARE(value(0),1UL);}
    void malformed(){read(ERR_BUFFER_EMPTY,0,0,0,0);response(ea().left(10));SH7058IdentificationSession session(path);std::atomic<bool> cancel{false};const auto r=identify(session,cancel,2000);QVERIFY(!r.complete);QVERIFY(r.cleanupConfirmed);QCOMPARE(value(0),1UL);}
    void guiSmokeRefuses(){qApp->setProperty("guiSmokeTest",true);SH7058IdentificationSession session(path);QVERIFY(!session.acquire());QVERIFY(QString::fromLatin1(trace()).isEmpty());QVERIFY(!launchSH7058Identification("fake","fake")["ok"].toBool());}
};
QTEST_MAIN(IdentitySessionTest)
#include "identity_session.moc"
