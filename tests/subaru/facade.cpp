#include <QtTest>
#include "serial_port_actions.h"

class SubaruFacadeTest : public QObject {
    Q_OBJECT
private slots:
    void sensitivePreview_data(){QTest::addColumn<QByteArray>("service");for(const char *s:{"27","67","0902","4902"})QTest::newRow(s)<<QByteArray::fromHex(s);}
    void sensitivePreview() {
        QFETCH(QByteArray,service);SerialPortActions facade;facade.set_is_iso15765_connection(true);
        QSignalSpy logs(&facade,&SerialPortActions::LOG_D);
        const auto bytes=QByteArray::fromHex("000007e0")+service+QByteArray("SECRET");
        const auto original=bytes;facade.write_checked(bytes);QCOMPARE(bytes,original);
        QVERIFY(!logs.isEmpty());const auto text=logs.first().first().toString();
        QVERIFY(text.contains("redacted"));QVERIFY(!text.contains("SECRET"));QVERIFY(!text.contains("53 45 43"));
    }
    void localUnsupported() {
        SerialPortActions facade;
        QVERIFY(facade.isDirectConnection());
        const auto result=facade.write_checked(QByteArray::fromHex("000007e0aa"));
        QCOMPARE(int(result.state),int(CommunicationResult::State::Unsupported));
        QVERIFY(!facade.get_is_comm_busy());
    }
    void localDisconnected() {
        SerialPortActions facade;
        facade.set_use_openport2_adapter(true); // no open/load/driver call
        const auto write=facade.write_checked(QByteArray::fromHex("000007e0aa"));
        const auto read=facade.read_checked(5);
        QCOMPARE(int(write.state),int(CommunicationResult::State::Disconnected));
        QCOMPARE(int(read.state),int(CommunicationResult::State::Disconnected));
        QVERIFY(!write.hasStatus); QVERIFY(!read.hasStatus);
        QVERIFY(!facade.get_is_comm_busy());
    }
    void remoteDenied_data() {
        QTest::addColumn<QString>("scenario");
        QTest::newRow("source-unavailable")<<QString("unavailable");
        QTest::newRow("would-time-out")<<QString("timeout");
        QTest::newRow("endpoint-disconnected")<<QString("disconnected");
    }
    void remoteDenied() {
        QFETCH(QString,scenario);
        // Real facade + real invalid QtRO replica, only a nonexistent local IPC
        // endpoint. No ECU, hardware, vendor DLL or external network endpoint.
        SerialPortActions facade("local:codex-offline-no-source-"+scenario);
        QVERIFY(!facade.isDirectConnection());
        const auto write=facade.write_checked(QByteArray::fromHex("000007e0aa"));
        const auto read=facade.read_checked(5);
        QCOMPARE(int(write.state),int(CommunicationResult::State::Unsupported));
        QCOMPARE(int(read.state),int(CommunicationResult::State::Unsupported));
        QVERIFY(!write.hasStatus); QVERIFY(!read.hasStatus);
        QVERIFY(write.bytes.isEmpty()); QVERIFY(read.bytes.isEmpty());
        // No slot_sync/default QVariant conversion is involved in either call.
    }
};
QTEST_MAIN(SubaruFacadeTest)
#include "facade.moc"
