#include <QtTest>
#include "../../gui/j2534_adapter_manager.h"
#include "../../serial_port/j2534_adapter_selection.h"
class AdapterGuiTest:public QObject {
    Q_OBJECT
private slots:
    void classification() {
        J2534Diagnostics::Driver d;d.description="OBDX Pro VX";d.api="04.04";d.path="C:/driver/obdx.dll";d.machine=0x8664;
        const QJsonObject official{{"company_name","Envyous Customs"},{"product_name","OBDX Pro VX"},{"sha256",QString(64,'a')}};
        QVERIFY(J2534Adapters::classify(d,official).supported());
        QVERIFY(!J2534Adapters::classify(d,{}).supported()); // registry name alone is insufficient
        d.issue="DLL architecture does not match this process";
        QVERIFY(!J2534Adapters::classify(d,official).supported());
        d.issue.clear();d.description="Tactrix OpenPort 2.0";
        QVERIFY(!J2534Adapters::classify(d,official).supported());
        d.description="OBDX Pro VX";d.api="05.00";QVERIFY(!J2534Adapters::classify(d,official).supported());
    }
    void presentation_data() {
        QTest::addColumn<QJsonObject>("calls");QTest::addColumn<QString>("text");
        QTest::newRow("open")<<QJsonObject{{"open_status",7}}<<"initialization failed";
        QTest::newRow("version")<<QJsonObject{{"version_called",true},{"version_ok",false}}<<"Version information";
        QTest::newRow("close")<<QJsonObject{{"close_status",7}}<<"close cleanly";
        QTest::newRow("handshake")<<QJsonObject{{"primary_error_stage","initialization_evidence"}}<<"handshake";
    }
    void presentation() {QFETCH(QJsonObject,calls);QFETCH(QString,text);const auto v=J2534Adapters::presentation({{"device_result",QJsonObject{{"device_result",calls}}}});QVERIFY(v["status"].toString().contains(text));}
    void dialogVersionsAndAvailability() {
        J2534AdapterController c;
        c.adapters=QJsonArray{QJsonObject{{"id","custom"},{"name","OpenPort 2.0 (Custom x64)"},{"available",true}}};
        c.lastReport={{"ok",true},{"device_result",QJsonObject{{"device_result",QJsonObject{{"firmware_version","1.2.3456"},{"dll_version","3.0"},{"api_version","04.04"}}}}}};
        J2534AdapterManager dialog(&c);dialog.show();
        QCOMPARE(dialog.findChild<QLineEdit*>("adapterFirmware")->text(),QString("1.2.3456"));
        QCOMPARE(dialog.findChild<QLineEdit*>("adapterApiVersion")->text(),QString("04.04"));
        QVERIFY(dialog.findChild<QLabel*>("adapterStatus")->text().contains("Connected successfully"));
        QVERIFY(dialog.findChild<QPushButton*>("adapterTest")->isEnabled());
        c.adapters=QJsonArray{QJsonObject{{"id","bad"},{"name","OpenPort"},{"available",false},{"reason","USB driver dependency missing"}}};
        emit c.changed();
        QVERIFY(!dialog.findChild<QPushButton*>("adapterTest")->isEnabled());
        QVERIFY(dialog.findChild<QLabel*>("adapterStatus")->text().contains("dependency missing"));
    }
    void missingHelperHasUsefulError() {
        J2534AdapterController c(nullptr,"C:/missing/FastECU-no-helper.exe");c.test("unused");
        QTRY_VERIFY(!c.busy());QVERIFY(c.lastReport["error"].toString().contains("component is missing"));
    }
    void duplicateTestIsNotStarted() {
        J2534AdapterController c(nullptr,QCoreApplication::applicationFilePath());QSignalSpy changes(&c,&J2534AdapterController::changed);
        c.test("first");QVERIFY(c.busy());const int before=changes.count();
        c.test("second");QCOMPARE(changes.count(),before);
        QTRY_VERIFY(!c.busy()); // Synthetic helper returns without loading any driver.
    }
    void activeTransportBlocksTesting() {
        J2534AdapterController c;c.transportBusy=[] {return true;};c.test("unused");
        QVERIFY(!c.busy());QVERIFY(c.lastReport["error"].toString().contains("already owns"));
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    if(app.arguments().contains("--adapter-manager-test")) {
        QThread::msleep(250);
        QTextStream(stdout)<<"{\"ok\":true}";
        return 0;
    }
    AdapterGuiTest test;return QTest::qExec(&test,argc,argv);
}
#include "adapter_gui.moc"
