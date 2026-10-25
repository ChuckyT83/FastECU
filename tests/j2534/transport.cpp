#include <QtTest>
#include <QLibrary>
#include <QTemporaryDir>
#include <QFileInfo>
#include <memory>
#include "../../serial_port/serial_port_actions_direct.h"
#ifndef Q_OS_WIN
#include "mock_j2534.h"
#define PT_CALL
extern "C" {
void MockTransportReset();
void MockTransportDelay(unsigned long);
unsigned long MockTransportByte(int);
void MockTransportWrite(long, unsigned long);
void MockTransportRead(long,unsigned long,unsigned long,unsigned long,unsigned long);
void MockTransportOther(long,long,long);
void MockTransportFive(unsigned long,int);
unsigned long MockTransportValue(int);
}
#endif
class J2534TransportTest : public QObject {
    Q_OBJECT
    QLibrary library;
    using Reset = void (PT_CALL *)();
    using Write = void (PT_CALL *)(long,unsigned long);
    using Read = void (PT_CALL *)(long,unsigned long,unsigned long,unsigned long,unsigned long);
    using Other = void (PT_CALL *)(long,long,long);
    using Five = void (PT_CALL *)(unsigned long,int);
    using Value = unsigned long (PT_CALL *)(int);
    using Delay = void (PT_CALL *)(unsigned long);
    Delay delayRead = nullptr;
    Value payloadByte = nullptr;
    Reset reset = nullptr;
    Write tx = nullptr;
    Read rx = nullptr;
    Other other = nullptr;
    Five five = nullptr;
    Value value = nullptr;
    std::unique_ptr<SerialPortActionsDirect> port;
    QByteArray message = QByteArray::fromHex("000007e001020304");
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        library.setFileName(QDir::current().absoluteFilePath("mock-j2534.dll"));
        QVERIFY2(library.load(),qPrintable(library.errorString()));
        reset = reinterpret_cast<Reset>(library.resolve("MockTransportReset"));
        tx = reinterpret_cast<Write>(library.resolve("MockTransportWrite"));
        rx = reinterpret_cast<Read>(library.resolve("MockTransportRead"));
        other = reinterpret_cast<Other>(library.resolve("MockTransportOther"));
        five = reinterpret_cast<Five>(library.resolve("MockTransportFive"));
        value = reinterpret_cast<Value>(library.resolve("MockTransportValue"));
        delayRead = reinterpret_cast<Delay>(library.resolve("MockTransportDelay"));
        payloadByte = reinterpret_cast<Value>(library.resolve("MockTransportByte"));
#else
        reset = MockTransportReset; tx = MockTransportWrite; rx = MockTransportRead;
        other = MockTransportOther; five = MockTransportFive; value = MockTransportValue;
        delayRead = MockTransportDelay; payloadByte = MockTransportByte;
#endif
        QVERIFY(reset && tx && rx && other && five && value && delayRead && payloadByte);
    }
    void init() {
#ifndef Q_OS_WIN
        MockJ2534::reset();
#endif
        reset();
        port.reset(new SerialPortActionsDirect);
#ifdef Q_OS_WIN
        port->j2534->setDllName(library.fileName().toLocal8Bit().constData());
#else
        port->serial_port = "mock";
#endif
        port->is_iso15765_connection = true;
        QCOMPARE(port->init_j2534_connection(), STATUS_SUCCESS);
        port->use_openport2_adapter = true;
        reset();
    }
    void cleanup() { port.reset(); }
    void transmit_data() {
        QTest::addColumn<long>("status"); QTest::addColumn<ulong>("count"); QTest::addColumn<bool>("ok");
        QTest::newRow("success") << long(0) << ulong(1) << true;
        QTest::newRow("error") << long(ERR_FAILED) << ulong(0) << false;
        QTest::newRow("timeout-with-transfer") << long(ERR_TIMEOUT) << ulong(1) << false;
        QTest::newRow("zero") << long(0) << ulong(0) << false;
        QTest::newRow("excess-count") << long(0) << ulong(2) << false;
        QTest::newRow("disconnected") << long(ERR_INVALID_CHANNEL_ID) << ulong(0) << false;
    }
    void transmit() {
        QFETCH(long,status); QFETCH(ulong,count); QFETCH(bool,ok);
        tx(status,count);
        QCOMPARE(port->write_j2534_data(message),ok ? STATUS_SUCCESS : STATUS_ERROR);
        QCOMPARE(value(0),1UL); QCOMPARE(value(6),100UL);
        QCOMPARE(value(7),ulong(message.size()));
        for(int i=0;i<message.size();++i) QCOMPARE(payloadByte(i),ulong(static_cast<unsigned char>(message.at(i))));
        QCOMPARE(value(8),ulong(ISO15765_FRAME_PAD));
        QCOMPARE(port->transport_state(),ok ? SerialPortActionsDirect::TransportState::Success : SerialPortActionsDirect::TransportState::Error);
        if (status == ERR_INVALID_CHANNEL_ID) {
            QCOMPARE(port->write_j2534_data(message),STATUS_ERROR);
            QCOMPARE(value(0),1UL);
        }
    }
    void invalidTransmitLengths_data() {
        QTest::addColumn<int>("length");
        for (int n : {0,1,3,PASSTHRU_MSG_DATA_SIZE+1}) QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void invalidTransmitLengths() {
        QFETCH(int,length);
        QCOMPARE(port->write_j2534_data(QByteArray(length,'x')),STATUS_ERROR);
        QCOMPARE(value(0),0UL);
    }
    void transmitBoundary() {
        QCOMPARE(port->write_j2534_data(QByteArray(PASSTHRU_MSG_DATA_SIZE,'x')),STATUS_SUCCESS);
        QCOMPARE(value(7),ulong(PASSTHRU_MSG_DATA_SIZE));
    }
    void publicWriteFailure() {
        tx(ERR_FAILED,0);
        QCOMPARE(port->write_serial_data(message),QByteArray(1,char(STATUS_ERROR)));
        QCOMPARE(port->write_serial_data_echo_check(message),QByteArray(1,char(STATUS_ERROR)));
        tx(0,1); QVERIFY(port->write_serial_data(message).isEmpty());
    }
    void receive_data() {
        QTest::addColumn<long>("status"); QTest::addColumn<ulong>("count"); QTest::addColumn<ulong>("size"); QTest::addColumn<ulong>("extra"); QTest::addColumn<int>("state");
        using S=SerialPortActionsDirect::TransportState;
        QTest::newRow("success") << long(0) << ulong(1) << ulong(8) << ulong(0) << int(S::Success);
        QTest::newRow("timeout") << long(ERR_TIMEOUT) << ulong(0) << ulong(0) << ulong(0) << int(S::NoMessage);
        QTest::newRow("empty") << long(ERR_BUFFER_EMPTY) << ulong(0) << ulong(0) << ulong(0) << int(S::NoMessage);
        QTest::newRow("zero-success") << long(0) << ulong(0) << ulong(0) << ulong(0) << int(S::NoMessage);
        QTest::newRow("driver-error") << long(ERR_FAILED) << ulong(1) << ulong(8) << ulong(0) << int(S::Error);
        QTest::newRow("partial-timeout") << long(ERR_TIMEOUT) << ulong(1) << ulong(8) << ulong(0) << int(S::Partial);
        QTest::newRow("boundary") << long(0) << ulong(1) << ulong(PASSTHRU_MSG_DATA_SIZE) << ulong(PASSTHRU_MSG_DATA_SIZE) << int(S::Success);
        QTest::newRow("oversized") << long(0) << ulong(1) << ulong(PASSTHRU_MSG_DATA_SIZE+1) << ulong(0) << int(S::Error);
        QTest::newRow("short-address") << long(0) << ulong(1) << ulong(3) << ulong(0) << int(S::Error);
        QTest::newRow("excess-count") << long(0) << ulong(2) << ulong(8) << ulong(0) << int(S::Error);
        QTest::newRow("bad-extra") << long(0) << ulong(1) << ulong(8) << ulong(9) << int(S::Error);
        QTest::newRow("invalid-channel") << long(ERR_INVALID_CHANNEL_ID) << ulong(0) << ulong(0) << ulong(0) << int(S::Error);
    }
    void receive() {
        QFETCH(long,status); QFETCH(ulong,count); QFETCH(ulong,size); QFETCH(ulong,extra); QFETCH(int,state);
        rx(status,count,size,0,extra);
        const QByteArray data=port->read_j2534_data(25);
        QCOMPARE(int(port->transport_state()),state);
        const bool payload=state==int(SerialPortActionsDirect::TransportState::Success)||state==int(SerialPortActionsDirect::TransportState::Partial);
        QCOMPARE(data,payload ? QByteArray(int(size),char(0x5a)) : QByteArray());
        QCOMPARE(value(1),1UL); QVERIFY(value(6)<=25UL);
    }
    void notificationThenPayload() {
        rx(0,1,0,TX_DONE,0); rx(0,1,0,START_OF_MESSAGE,0); rx(0,1,8,0,0);
        QCOMPARE(port->read_j2534_data(100),QByteArray(8,char(0x5a)));
        QCOMPARE(value(1),3UL); QVERIFY(value(6)<=100UL);
    }
    void followupErrorDoesNotReturnStaleData() {
        rx(0,1,8,TX_DONE,0); rx(ERR_FAILED,0,0,0,0);
        QVERIFY(port->read_j2534_data(30).isEmpty());
        QCOMPARE(port->transport_state(),SerialPortActionsDirect::TransportState::Error);
        QCOMPARE(value(1),2UL);
    }
    void sharedTimeoutBudget() {
        delayRead(20);
        rx(0,1,0,TX_DONE,0); rx(0,1,8,0,0);
        QCOMPARE(port->read_j2534_data(100),QByteArray(8,char(0x5a)));
        QCOMPARE(value(1),2UL); QVERIFY(value(6)<=80UL);
    }
    void fastInitBounds() {
        QCOMPARE(port->fast_init(QByteArray(PASSTHRU_MSG_DATA_SIZE+1,'x')),STATUS_ERROR);
        QCOMPARE(value(4),0UL);
    }
    void boundedNotifications() {
        for(int i=0;i<5;++i) rx(0,1,0,START_OF_MESSAGE,0);
        QVERIFY(port->read_j2534_data(0).isEmpty()); QCOMPARE(value(1),3UL);
    }
    void disconnectedAndRetry() {
        port->close_j2534_serial_port();
        QCOMPARE(port->write_serial_data(message),QByteArray(1,char(STATUS_ERROR)));
        QCOMPARE(port->write_serial_data_echo_check(message),QByteArray(1,char(STATUS_ERROR)));
        QCOMPARE(port->write_j2534_data(message),STATUS_ERROR); QVERIFY(port->read_j2534_data(0).isEmpty());
        QCOMPARE(value(0),0UL); QCOMPARE(value(1),0UL);
        QCOMPARE(port->init_j2534_connection(),STATUS_SUCCESS);
        QCOMPARE(port->write_j2534_data(message),STATUS_SUCCESS);
    }
    void periodicLifecycle() {
        QCOMPARE(port->send_periodic_j2534_data(message,50),STATUS_SUCCESS);
        QVERIFY(port->periodicActive); QCOMPARE(port->msgID,0UL);
        QCOMPARE(port->send_periodic_j2534_data(message,50),STATUS_ERROR);
        QCOMPARE(value(2),1UL);
        other(0,ERR_FAILED,0);
        QCOMPARE(port->stop_periodic_j2534_data(),STATUS_ERROR); QVERIFY(port->periodicActive);
        other(0,0,0);
        QCOMPARE(port->stop_periodic_j2534_data(),STATUS_SUCCESS); QVERIFY(!port->periodicActive);
        QCOMPARE(port->stop_periodic_j2534_data(),STATUS_ERROR); QCOMPARE(value(3),2UL);
    }
    void periodicFailure() {
        other(ERR_FAILED,0,0);
        QCOMPARE(port->send_periodic_j2534_data(message,50),STATUS_ERROR); QVERIFY(!port->periodicActive);
        QCOMPARE(port->send_periodic_j2534_data(message,0),STATUS_ERROR);
        QCOMPARE(port->send_periodic_j2534_data(QByteArray(PASSTHRU_MSG_DATA_SIZE+1,'x'),1),STATUS_ERROR);
        QCOMPARE(value(2),1UL);
    }
    void ioctlAndVoltageFailures() {
        other(0,0,ERR_NOT_SUPPORTED);
        QCOMPARE(port->set_j2534_ioctl(LOOPBACK,0),STATUS_ERROR);
        QCOMPARE(port->clear_rx_buffer(),STATUS_ERROR);
        QCOMPARE(port->clear_tx_buffer(),STATUS_ERROR);
        QCOMPARE(port->line_end_check_1_toggled(port->requestToSendDisabled),STATUS_ERROR);
        QCOMPARE(port->line_end_check_2_toggled(port->dataTerminalDisabled),STATUS_ERROR);
        QCOMPARE(value(5),2UL); QCOMPARE(value(9),41UL);
    }
    void voltagePulseFailureStillAttemptsOff() {
        other(0,0,ERR_FAILED);
        QCOMPARE(port->pulse_lec_1_line(0),STATUS_ERROR);
        QCOMPARE(value(5),2UL); QCOMPARE(value(9),41UL);
        QCOMPARE(value(10),ulong(J1962_PIN_11));
        QCOMPARE(value(12),12000UL); QCOMPARE(value(11),static_cast<ulong>(-2));
        QCOMPARE(port->pulse_lec_2_line(0),STATUS_ERROR);
        QCOMPARE(value(5),4UL); QCOMPARE(value(10),ulong(J1962_PIN_9));
        QCOMPARE(value(11),static_cast<ulong>(-2));
    }
    void invalidChannelIoctlStopsFurtherUse() {
        other(0,0,ERR_INVALID_CHANNEL_ID);
        QCOMPARE(port->clear_rx_buffer(),STATUS_ERROR);
        QCOMPARE(value(4),1UL);
        QCOMPARE(port->clear_tx_buffer(),STATUS_ERROR);
        QCOMPARE(port->write_j2534_data(message),STATUS_ERROR);
        QCOMPARE(value(4),1UL); QCOMPARE(value(0),0UL);
    }
    void fiveBaudBounds() {
#ifdef Q_OS_WIN
        port->j2534->debug(true); // malformed output must also be safe in diagnostics
#endif
        QVERIFY(port->five_baud_init(QByteArray()).isEmpty());
        QVERIFY(port->five_baud_init(QByteArray(21,'x')).isEmpty()); QCOMPARE(value(4),0UL);
        QCOMPARE(port->five_baud_init(QByteArray(1,char(0x33))),QByteArray(2,char(0x55)));
        five(21,0); QVERIFY(port->five_baud_init(QByteArray(1,'x')).isEmpty());
        five(2,1); QVERIFY(port->five_baud_init(QByteArray(1,'x')).isEmpty());
        other(0,0,ERR_FAILED); QVERIFY(port->five_baud_init(QByteArray(1,'x')).isEmpty());
    }
#ifdef Q_OS_WIN
    void incompatibleDllArchitecture() {
        const QString path=qEnvironmentVariable("WINDIR")+"/SysWOW64/kernel32.dll";
        QVERIFY(QFileInfo(path).isFile()); // Windows-hosted runner's 32-bit fixture
        port->close_j2534_serial_port();
        port->j2534->setDllName(path.toLocal8Bit().constData());
        QVERIFY(!port->j2534->init()); QVERIFY(!port->j2534->valid());
        const QString error=QString::fromLocal8Bit(port->j2534->getLastError());
        QVERIFY2(error.contains("193"),qPrintable(error));
        QVERIFY(error.contains("64-bit"));
    }
    void registryViewAndValues() {
#ifdef _WIN64
        QCOMPARE(port->driver_registry_format(),QSettings::Registry64Format);
#else
        QCOMPARE(port->driver_registry_format(),QSettings::Registry32Format);
#endif
        const QString root="HKEY_CURRENT_USER\\Software\\FastECU-offline-test-"+QUuid::createUuid().toString(QUuid::Id128);
        QSettings registry(root,port->driver_registry_format());
        struct Cleanup { QSettings &r; ~Cleanup() {r.clear();r.sync();} } cleanup{registry};
        QVERIFY(port->read_j2534_registry(registry).isEmpty());
        const QString path=library.fileName();
        registry.setValue("valid/FunctionLibrary",path);
        QVERIFY(port->read_j2534_registry(registry).contains("valid"));
        qputenv("FASTECU_TEST_DLL",path.toLocal8Bit());
        registry.setValue("expanded/FunctionLibrary","%FASTECU_TEST_DLL%");
        registry.setValue("quoted/FunctionLibrary",'"'+path+'"');
        registry.setValue("missing/Name","no function library");
        registry.setValue("empty/FunctionLibrary","");
        registry.setValue("number/FunctionLibrary",42);
        registry.setValue("list/FunctionLibrary",QStringList{"one","two"});
        registry.setValue("nul/FunctionLibrary",path+QChar(0)+"bad");
        registry.setValue("relative/FunctionLibrary","mock-j2534.dll");
        registry.setValue("absent/FunctionLibrary","C:/missing-offline-driver.dll");
        registry.sync(); QCOMPARE(registry.status(),QSettings::NoError);
        const auto found=port->read_j2534_registry(registry);
        qunsetenv("FASTECU_TEST_DLL");
        QCOMPARE(found.size(),3); QCOMPARE(found.value("expanded"),path); QCOMPARE(found.value("valid"),path); QCOMPARE(found.value("quoted"),path);
    }
#endif
};
QTEST_MAIN(J2534TransportTest)
#include "transport.moc"
