#include <QtTest>
#include "../../serial_port/serial_port_actions_direct.h"
#include "mock_j2534.h"
class J2534ReliabilityTest : public QObject {
    Q_OBJECT
    static void configure(SerialPortActionsDirect &port, int protocol = ISO15765) {
        port.serial_port = "mock";
        port.is_iso15765_connection = protocol == ISO15765;
        port.is_can_connection = protocol == CAN;
        port.is_iso14230_connection = protocol == ISO14230;
    }
private slots:
    void init() { MockJ2534::reset(); }
    void guiSmokeCannotOpenHardware() {
        SerialPortActionsDirect port; configure(port);
        qApp->setProperty("guiSmokeTest", true);
        const auto initResult = port.init_j2534_connection();
        const auto opened = port.open_serial_port();
        qApp->setProperty("guiSmokeTest", false);
        QCOMPARE(initResult, STATUS_ERROR);QVERIFY(opened.isEmpty());QVERIFY(MockJ2534::calls.isEmpty());
        QVERIFY(!port.is_serial_port_open());
    }
    void success_data() {
        QTest::addColumn<int>("protocol");
        for (int p : {ISO15765, CAN, ISO9141, ISO14230}) QTest::newRow(qPrintable(QString::number(p))) << p;
    }
    void success() {
        QFETCH(int, protocol);
        SerialPortActionsDirect port; configure(port, protocol);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        QVERIFY(port.is_serial_port_open());
        QVERIFY(port.J2534_open_ok && port.J2534_connect_ok && port.J2534_init_ok);
        QCOMPARE(port.devID, 41UL);
        QCOMPARE(port.chanID, static_cast<unsigned long>(protocol)); // native serial backend contract
        port.close_j2534_serial_port();
        QCOMPARE(MockJ2534::calls.count("disconnect:" + QString::number(protocol)), 1);
        QCOMPARE(MockJ2534::calls.count("close:41"), 1);
        QVERIFY(MockJ2534::calls.indexOf("disconnect:" + QString::number(protocol)) < MockJ2534::calls.indexOf("close:41"));
        QVERIFY(!port.is_serial_port_open());
        QCOMPARE(port.chanID, 0UL);
        port.close_j2534_serial_port(); // no duplicate releases
        QCOMPARE(MockJ2534::calls.count("close:41"), 1);
    }
    void failures_data() {
        QTest::addColumn<QString>("failure");
        QTest::addColumn<bool>("opened"); QTest::addColumn<bool>("connected");
        QTest::addColumn<int>("protocol");
        for (int p : {ISO15765, CAN, ISO9141, ISO14230}) {
            for (const char *op : {"open", "version", "connect", "config", "filter"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(p).arg(op)))
                    << QString(op) << (QString(op) != "open")
                    << (QString(op) == "config" || QString(op) == "filter") << p;
        }
        QTest::newRow("required-filter-clear") << QString("clearFilters") << true << true << int(ISO15765);
    }
    void failures() {
        QFETCH(QString, failure); QFETCH(bool, opened); QFETCH(bool, connected); QFETCH(int, protocol);
        SerialPortActionsDirect port; configure(port, protocol);
        QSignalSpy errors(&port, &SerialPortActionsDirect::LOG_E);
        MockJ2534::fail = failure;
        QCOMPARE(port.init_j2534_connection(), STATUS_ERROR);
        QVERIFY(!port.J2534_init_ok && !port.J2534_open_ok && !port.J2534_connect_ok);
        QVERIFY(!port.is_serial_port_open());
        QCOMPARE(MockJ2534::calls.count("close:41"), int(opened));
        QCOMPARE(MockJ2534::calls.count("disconnect:" + QString::number(protocol)), int(connected));
        QVERIFY(!errors.isEmpty());
        QVERIFY(errors.first().first().toString().contains("status 0x7")); // ERR_FAILED
        const auto calls = MockJ2534::calls;
        port.clear_rx_buffer(); port.clear_tx_buffer(); port.read_j2534_data(0);
        port.write_j2534_data("x"); port.set_j2534_ioctl(LOOPBACK, 0);
        port.send_periodic_j2534_data("x", 1); port.stop_periodic_j2534_data();
        QCOMPARE(MockJ2534::calls, calls); // no invalid channel calls after failure
        MockJ2534::fail.clear();
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS); // retry recovers
    }
    void requiredBufferIoctls_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("clearTx") << QString("clearTx");
        QTest::newRow("clearRx") << QString("clearRx");
    }
    void requiredBufferIoctls() {
        QFETCH(QString, operation);
        SerialPortActionsDirect port; configure(port, ISO14230);
        port.J2534_is_denso_dsti = true;
        MockJ2534::fail = operation;
        QCOMPARE(port.init_j2534_connection(), STATUS_ERROR);
        QCOMPARE(MockJ2534::calls.count("disconnect:4"), 1);
        QCOMPARE(MockJ2534::calls.count("close:41"), 1);
    }
    void disconnectUsesChannel() {
        SerialPortActionsDirect port; configure(port);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        QCOMPARE(port.unset_j2534_can(), STATUS_SUCCESS);
        QVERIFY(!port.J2534_init_ok && !port.is_serial_port_open());
        QCOMPARE(MockJ2534::calls.count("disconnect:6"), 1);
        QVERIFY(!MockJ2534::calls.contains("disconnect:41"));
        port.close_j2534_serial_port();
        QCOMPARE(MockJ2534::calls.count("disconnect:6"), 1);
        QCOMPARE(MockJ2534::calls.count("close:41"), 1);
    }
    void repeatedAndDestructor() {
        { SerialPortActionsDirect port; configure(port);
          QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
          QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
          QCOMPARE(MockJ2534::calls.count("close:41"), 1);
        }
        QCOMPARE(MockJ2534::calls.count("close:41"), 2);
        QCOMPARE(MockJ2534::calls.count("disconnect:6"), 2);
    }
    void cleanupFailures() {
        SerialPortActionsDirect port; configure(port);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        MockJ2534::fail = "disconnect";
        port.close_j2534_serial_port();
        QCOMPARE(MockJ2534::calls.count("close:41"), 1);
        QCOMPARE(MockJ2534::calls.count("transportClose"), 1);
        port.close_j2534_serial_port();
        QCOMPARE(MockJ2534::calls.count("disconnect:6"), 1);
        MockJ2534::fail.clear();
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        MockJ2534::fail = "close";
        port.close_j2534_serial_port();
        QCOMPARE(MockJ2534::calls.count("close:41"), 2);
        QVERIFY(!MockJ2534::loaded);
    }
    void metadata_data() {
        QTest::addColumn<int>("mode"); QTest::addColumn<bool>("accepted");
        QTest::newRow("normal") << 0 << true;
        QTest::newRow("unsupported") << 1 << true;
        QTest::newRow("empty") << 2 << true;
        QTest::newRow("boundary-79") << 3 << true;
        QTest::newRow("unterminated-80") << 4 << false;
        QTest::newRow("unterminated-256") << 5 << false;
    }
    void metadata() {
        QFETCH(int, mode); QFETCH(bool, accepted);
        SerialPortActionsDirect port; configure(port);
        QSignalSpy logs(&port, &SerialPortActionsDirect::LOG_D);
        MockJ2534::metadata = mode;
        QCOMPARE(port.init_j2534_connection(), accepted ? STATUS_SUCCESS : STATUS_ERROR);
        if (!accepted) QCOMPARE(MockJ2534::calls.count("close:41"), 1);
        if (mode == 0) {
            QStringList text; for (const auto &entry : logs) text << entry.first().toString();
            QVERIFY(text.contains("J2534 API Version: 04.04"));
            QVERIFY(text.contains("J2534 DLL Version: 1.2.3"));
            QVERIFY(text.contains("Device Firmware Version: 9.8.7"));
        }
        char serial[2] = {'x', 'y'};
        QVERIFY(!port.get_serial_num(serial, 1));
        QCOMPARE(serial[0], '\0'); QCOMPARE(serial[1], 'y');
        QVERIFY(!port.get_serial_num(nullptr, 0));
    }
    void zeroDeviceHandle() {
        MockJ2534::device = 0; // zero is valid if the API returned success
        SerialPortActionsDirect port; configure(port);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        port.close_j2534_serial_port();
        QCOMPARE(MockJ2534::calls.count("connect:0"), 1);
        QCOMPARE(MockJ2534::calls.count("close:0"), 1);
    }
    void unavailableErrorText() {
        SerialPortActionsDirect port; configure(port);
        MockJ2534::fail = "connect"; MockJ2534::errorTextStatus = ERR_NOT_SUPPORTED;
        QSignalSpy errors(&port, &SerialPortActionsDirect::LOG_E);
        QCOMPARE(port.init_j2534_connection(), STATUS_ERROR);
        QVERIFY(errors.first().first().toString().contains("PassThruConnect failed (status 0x7)"));
        QVERIFY(errors.first().first().toString().contains("text unavailable"));
    }
};
QTEST_MAIN(J2534ReliabilityTest)
#include "reliability.moc"
