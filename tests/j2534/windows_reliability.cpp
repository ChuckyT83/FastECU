// Native Windows integration: production connection code + production DLL loader
// + mock DLL. The test never enumerates or opens a physical adapter.
#include <QtTest>
#include <QLibrary>
#include "../../serial_port/serial_port_actions_direct.h"
class J2534ReliabilityTest : public QObject {
    Q_OBJECT
    QLibrary mock;
    using Reset = void (PT_CALL *)();
    using SetFailure = void (PT_CALL *)(const char *);
    using SetMetadata = void (PT_CALL *)(int);
    using SetHandles = void (PT_CALL *)(unsigned long, unsigned long);
    using Trace = const char *(PT_CALL *)();
    Reset reset = nullptr;
    SetFailure setFailure = nullptr;
    SetMetadata setMetadata = nullptr;
    Trace trace = nullptr;
    SetHandles setHandles = nullptr;
    QString path;
    void configure(SerialPortActionsDirect &port, int p = ISO15765) {
        port.j2534->setDllName(path.toLocal8Bit().constData());
        port.is_iso15765_connection = p == ISO15765;
        port.is_can_connection = p == CAN;
        port.is_iso14230_connection = p == ISO14230;
    }
    QString calls() { return QString::fromLatin1(trace()); }
private slots:
    void initTestCase() {
        path = QDir::current().absoluteFilePath("mock-j2534.dll");
        mock.setFileName(path);
        QVERIFY2(mock.load(), qPrintable(mock.errorString()));
        reset = reinterpret_cast<Reset>(mock.resolve("MockReset"));
        setFailure = reinterpret_cast<SetFailure>(mock.resolve("MockSetFailure"));
        setMetadata = reinterpret_cast<SetMetadata>(mock.resolve("MockSetMetadata"));
        trace = reinterpret_cast<Trace>(mock.resolve("MockTrace"));
        setHandles = reinterpret_cast<SetHandles>(mock.resolve("MockSetHandles"));
        QVERIFY(reset && setFailure && setMetadata && trace && setHandles);
    }
    void init() { reset(); }
    void successAndRepeatedCleanup() {
        { SerialPortActionsDirect port; configure(port);
          QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
          QCOMPARE(port.devID, 41UL); QCOMPARE(port.chanID, 99UL);
          QVERIFY(port.J2534_init_ok);
          QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
          QCOMPARE(calls().count("disconnect:99;close:41;"), 1);
        }
        QCOMPARE(calls().count("disconnect:99;close:41;"), 2);
    }
    void failure_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<int>("protocol");
        for (int p : {ISO15765, CAN, ISO9141, ISO14230}) {
            for (const char *op : {"open", "version", "connect", "config", "filter"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(p).arg(op))) << QString(op) << p;
        }
        QTest::newRow("filter-clear") << QString("clearFilters") << int(ISO15765);
    }
    void failure() {
        QFETCH(QString, operation); QFETCH(int, protocol);
        SerialPortActionsDirect port; configure(port, protocol);
        setFailure(operation.toLatin1().constData());
        QSignalSpy errors(&port, &SerialPortActionsDirect::LOG_E);
        QCOMPARE(port.init_j2534_connection(), STATUS_ERROR);
        QVERIFY(!port.J2534_init_ok && !port.J2534_open_ok && !port.J2534_connect_ok);
        QVERIFY(!port.j2534->valid());
        QCOMPARE(calls().count("close:41;"), operation == "open" ? 0 : 1);
        QCOMPARE(calls().count("disconnect:99;"), (operation == "config" || operation == "filter" || operation == "clearFilters") ? 1 : 0);
        QVERIFY(!errors.isEmpty());
        QVERIFY(errors.first().first().toString().contains("status 0x7"));
        setFailure("");
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
    }
    void metadata_data() {
        QTest::addColumn<int>("mode");
        for (int mode = 0; mode <= 5; ++mode) QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void metadata() {
        QFETCH(int, mode);
        SerialPortActionsDirect port; configure(port);
        setMetadata(mode);
        QSignalSpy logs(&port, &SerialPortActionsDirect::LOG_D);
        QCOMPARE(port.init_j2534_connection(), mode >= 4 ? STATUS_ERROR : STATUS_SUCCESS);
        if (mode == 0) {
            QStringList text; for (const auto &entry : logs) text << entry.first().toString();
            QVERIFY(text.contains("J2534 API Version: 04.04"));
            QVERIFY(text.contains("Device Firmware Version: 9.8.7"));
        }
        char value[2] = {'x', 'y'};
        QVERIFY(!port.get_serial_num(value, 1));
        QCOMPARE(value[0], '\0'); QCOMPARE(value[1], 'y');
    }
    void zeroHandlesAreOwned() {
        SerialPortActionsDirect port; configure(port);
        setHandles(0, 0);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        QVERIFY(port.J2534_open_ok && port.J2534_connect_ok);
        port.close_j2534_serial_port();
        QCOMPARE(calls().count("disconnect:0;close:0;"), 1);
    }
    void cleanupFailures_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("disconnect") << QString("disconnect");
        QTest::newRow("close") << QString("close");
    }
    void cleanupFailures() {
        QFETCH(QString, operation);
        SerialPortActionsDirect port; configure(port);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
        setFailure(operation.toLatin1().constData());
        port.close_j2534_serial_port();
        QVERIFY(!port.J2534_init_ok && !port.J2534_open_ok && !port.J2534_connect_ok);
        QVERIFY(!port.j2534->valid());
        QCOMPARE(calls().count("disconnect:99;"), 1);
        QCOMPARE(calls().count("close:41;"), 1);
        port.close_j2534_serial_port();
        QCOMPARE(calls().count("close:41;"), 1);
        setFailure("");
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
    }
    void publicSelectionUsesDllPath() {
        SerialPortActionsDirect port;
        configure(port);
        port.installed_drivers["Mock vendor display name"] = path;
        port.serial_port_list << "Mock vendor display name";
        QCOMPARE(port.open_serial_port(), QString("Mock vendor display name"));
        QVERIFY(port.is_serial_port_open());
        QCOMPARE(calls().count("open:0;"), 1); // no probing double-open
        port.reset_connection();
        QCOMPARE(calls().count("disconnect:99;close:41;"), 1);
    }
    void publicFailureDoesNotFallBackToSerial() {
        SerialPortActionsDirect port;
        configure(port);
        port.installed_drivers["Mock vendor display name"] = path;
        port.serial_port_list << "Mock vendor display name";
        setFailure("connect");
        QVERIFY(port.open_serial_port().isEmpty());
        QVERIFY(!port.serial->isOpen());
        QVERIFY(!port.J2534_init_ok && !port.j2534->valid());
        QCOMPARE(calls().count("close:41;"), 1);
    }
    void missingDllAndExport() {
        SerialPortActionsDirect port; configure(port);
        port.j2534->setDllName("missing-j2534.dll");
        QCOMPARE(port.init_j2534_connection(), STATUS_ERROR);
        QVERIFY(!port.j2534->valid());
        port.j2534->setDllName(QDir::current().absoluteFilePath("incomplete-j2534.dll").toLocal8Bit().constData());
        QCOMPARE(port.init_j2534_connection(), STATUS_ERROR);
        QVERIFY(!port.j2534->valid());
        QVERIFY(QString::fromLatin1(port.j2534->getLastError()).contains("PassThruIoctl"));
        configure(port);
        QCOMPARE(port.init_j2534_connection(), STATUS_SUCCESS);
    }
};
QTEST_MAIN(J2534ReliabilityTest)
#include "windows_reliability.moc"
