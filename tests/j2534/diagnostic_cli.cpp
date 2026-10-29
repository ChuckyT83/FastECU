#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QCryptographicHash>
#include <QUuid>
#include <QStandardPaths>

class DiagnosticCliTest:public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString tool=qEnvironmentVariable("FASTECU_DIAGNOSTIC_TOOL");
    int exitCode=-1;
    QJsonObject run(const QStringList &args,const QString &program=QString()) {
        QProcess process;process.setProgram(program.isEmpty()?tool:program);process.setArguments(args);
        process.start();if(!process.waitForStarted(5000) || !process.waitForFinished(40000)){process.kill();process.waitForFinished();exitCode=-1;return {};}
        exitCode=process.exitCode();return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    }
    static QString hash(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex());}
#ifdef Q_OS_WIN
    QString root;
    QString view=sizeof(void*)==8?"x64":"x86";
    QString mock(const QString &name="mock-j2534.dll") const{return QDir::current().absoluteFilePath(name);}
    void registration(const QString &file,const QString &name="Synthetic diagnostic fixture") {
        QSettings registry(root+"\\"+view+"-04.04",sizeof(void*)==8?QSettings::Registry64Format:QSettings::Registry32Format);
        registry.setValue("synthetic/Name",name);registry.setValue("synthetic/FunctionLibrary",file);registry.sync();
    }
    QStringList deviceArgs(const QString &file) const {return {"--validate-device",view+":04.04:synthetic","--adapter-kind","obdx-pro-vx","--vehicle-disconnected","--reviewed-open-behavior","--driver-sha256",hash(file)};}
#endif
private slots:
    void initTestCase(){QVERIFY(!tool.isEmpty());QVERIFY(QFileInfo(tool).isFile());}
    void init(){
#ifdef Q_OS_WIN
        root="HKEY_CURRENT_USER\\Software\\FastECU-Diagnostics-Test-"+QUuid::createUuid().toString(QUuid::Id128);
        qputenv("FASTECU_DIAGNOSTICS_TEST_REGISTRY",root.toLocal8Bit());
        qputenv("FASTECU_MOCK_TRACE_FILE",directory.filePath("trace.txt").toLocal8Bit());QFile::remove(directory.filePath("trace.txt"));
#endif
    }
    void cleanup(){
#ifdef Q_OS_WIN
        for(auto view:{QSettings::Registry32Format,QSettings::Registry64Format}){QSettings r(root,view);r.clear();r.sync();}
        qunsetenv("FASTECU_DIAGNOSTICS_TEST_REGISTRY");qunsetenv("FASTECU_MOCK_TRACE_FILE");
#endif
    }
    void identificationBlocked(){const auto r=run({"--identification-plan"});QCOMPARE(exitCode,2);QVERIFY(!r["ecu_communication"].toBool());QVERIFY(!r["programming_available"].toBool());}
    void exportAndNoOverwrite(){
        const auto path=directory.filePath("export-"+QUuid::createUuid().toString(QUuid::Id128)+".json");
        const auto r=run({"--identification-plan","--output",path});QCOMPARE(exitCode,2);QVERIFY(r.contains("schema"));
        QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));const auto original=f.readAll();f.close();QVERIFY(!original.isEmpty());
        const auto again=run({"--identification-plan","--output",path});QCOMPARE(exitCode,1);QVERIFY(again.contains("export_error"));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),original);
    }
    void failedExport(){const auto r=run({"--identification-plan","--output",directory.filePath("missing/directory/report.json")});QCOMPARE(exitCode,1);QVERIFY(r.contains("export_error"));}
    void fileInspection(){
        const auto path=directory.filePath("untrusted-installer.exe");QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("synthetic, not executable");f.close();
        const auto r=run({"--inspect-file",path});QCOMPARE(exitCode,0);QCOMPARE(r["file_info"].toObject()["sha256"].toString(),hash(path));QVERIFY(r["file"].toString().startsWith("<redacted-directory>"));QVERIFY(!r["device_opened"].toBool());
        const auto raw=run({"--inspect-file",path,"--include-private-details"});QCOMPARE(raw["file"].toString(),path);
    }
    void readableSummaryAndPrivacy(){
        const auto path=directory.filePath("summary-"+QUuid::createUuid().toString(QUuid::Id128)+".txt");
        const auto r=run({"--identification-plan","--summary",path});QCOMPARE(exitCode,2);
        QVERIFY(r["request_refused"].toBool());QVERIFY(r["human_summary"].toString().contains("REFUSED"));
        QFile f(path);QVERIFY(f.open(QIODevice::ReadOnly));const auto original=f.readAll();f.close();
        QCOMPARE(original,r["human_summary"].toString().toUtf8());
        const auto again=run({"--identification-plan","--summary",path});QCOMPARE(exitCode,1);
        QVERIFY(again.contains("summary_export_error"));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),original);
    }
    void missingFileSummary(){const auto r=run({"--inspect-file",directory.filePath("not-present.dll")});QCOMPARE(exitCode,1);QVERIFY(r["human_summary"].toString().contains("missing"));QVERIFY(!r["human_summary"].toString().contains(directory.path()));}
    void customModeRejectsConflictingRequests() {
        for(const auto &args:{QStringList{"--inspect-file","missing.dll"},QStringList{"--validate-device","x64:04.04:fake"},QStringList{"--adapter-kind","obdx-pro-vx"},QStringList{"--collect",QString(32,'a')}}) {
            const auto r=run(QStringList{"--qualify-openport-usb-only","C:/missing.dll"}+args);
            QCOMPARE(exitCode,2);QVERIFY(r["request_refused"].toBool());QVERIFY(!r["device_opened"].toBool());
        }
    }
    void originalCustomDriverNotAdmitted() {
        const auto r=run({"--qualify-openport-usb-only",directory.filePath("not-present.dll"),"--vehicle-disconnected","--reviewed-open-behavior","--driver-sha256","5db9cbce45baac236e2ee155062b74960adfdb29303c979868c39bcbfe806aa3"});
        QCOMPARE(exitCode,2);QVERIFY(!r["device_opened"].toBool());
    }
    void invalidCollectToken(){const auto r=run({"--collect","../outside"});QCOMPARE(exitCode,2);QVERIFY(r.contains("error"));}
#ifdef Q_OS_WIN
    void emptyRegistry(){const auto r=run({});QCOMPARE(exitCode,0);QVERIFY(r["drivers"].toArray().isEmpty());QCOMPARE(r["status"].toString(),QString("no_drivers_registered"));QVERIFY(!r["device_opened"].toBool());QVERIFY(r["test_build"].toBool());QVERIFY(r["human_summary"].toString().contains("not installed/registered"));}
    void passiveProblems_data(){
        QTest::addColumn<QString>("kind");QTest::addColumn<QString>("issue");
        QTest::newRow("missing")<<QString("missing")<<QString("missing or inaccessible");
        QTest::newRow("malformed")<<QString("malformed")<<QString("Not a Windows PE DLL");
        QTest::newRow("wrong-bitness")<<QString("wrong-bitness")<<QString("architecture does not match");
    }
    void passiveProblems(){
        QFETCH(QString,kind);QFETCH(QString,issue);
        const auto path=directory.filePath("passive-"+kind+".dll");
        if(kind!="missing") {
            QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QByteArray bytes(512,0);
            if(kind=="wrong-bitness") {
                bytes.replace(0,2,"MZ");bytes[60]=64;bytes.replace(64,4,QByteArray("PE\0\0",4));
                const unsigned machine=sizeof(void*)==8?0x14c:0x8664;
                bytes[68]=char(machine&255);bytes[69]=char(machine>>8);bytes[70]=1;
                bytes[84]=char(sizeof(void*)==8?96:112);bytes[87]=0x20;
                bytes[88]=0x0b;bytes[89]=char(sizeof(void*)==8?1:2);
            }
            f.write(bytes);f.close();
        }
        registration(path);const auto r=run({});QCOMPARE(exitCode,0);
        QCOMPARE(r["drivers"].toArray().size(),1);QVERIFY(r["human_summary"].toString().contains(issue));
        QVERIFY(!r["human_summary"].toString().contains(directory.path()));
        QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));QVERIFY(!r["device_opened"].toBool());
    }
    void passiveDoesNotCallDriver(){registration(mock());const auto r=run({});QCOMPARE(exitCode,0);QCOMPARE(r["drivers"].toArray().size(),1);QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));QVERIFY(!r["device_opened"].toBool());QCOMPARE(r["process_architecture"].toString(),QSysInfo::buildCpuArchitecture());}
    void cloneBlocked(){registration(mock(),"Tactrix OpenPort clone fixture");const auto r=run(deviceArgs(mock()));QCOMPARE(exitCode,2);QVERIFY(r.contains("error"));QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));}
    void missingReviewBlocked(){registration(mock());auto args=deviceArgs(mock());args.removeAll("--reviewed-open-behavior");const auto r=run(args);QCOMPARE(exitCode,2);QVERIFY(r.contains("error"));QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));}
    void wrongHashBlocked(){registration(mock());auto args=deviceArgs(mock());args[args.size()-1]=QString(64,'0');const auto r=run(args);QCOMPARE(exitCode,1);QVERIFY(r["error"].toString().contains("hash"));QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));}
    void isolatedDevice_data(){QTest::addColumn<QString>("dll");QTest::addColumn<bool>("ok");QTest::newRow("normal")<<QString("mock-j2534.dll")<<true;QTest::newRow("open-error")<<QString("open-error-j2534.dll")<<false;QTest::newRow("version-error")<<QString("version-error-j2534.dll")<<false;QTest::newRow("close-error")<<QString("close-error-j2534.dll")<<false;QTest::newRow("missing-export")<<QString("incomplete-device-j2534.dll")<<false;}
    void isolatedDevice(){QFETCH(QString,dll);QFETCH(bool,ok);const auto file=mock(dll);QVERIFY(QFileInfo(file).exists());registration(file);const auto r=run(deviceArgs(file));QCOMPARE(exitCode,ok?0:1);QCOMPARE(r["ok"].toBool(),ok);const auto child=r["device_result"].toObject();QVERIFY(child["isolated_process"].toBool());QVERIFY(!child["ecu_communication"].toBool());const auto calls=child["device_result"].toObject();if(ok){QCOMPARE(calls["device_handle"].toString(),QString("41"));QVERIFY(calls["cleanup_confirmed"].toBool());QVERIFY(calls["version_ok"].toBool());}QFile trace(directory.filePath("trace.txt"));if(trace.open(QIODevice::ReadOnly)){const auto bytes=trace.readAll();QVERIFY(!bytes.contains("connect"));QVERIFY(!bytes.contains("filter"));QVERIFY(!bytes.contains("write"));QVERIFY(!bytes.contains("ioctl"));}}
    QStringList customArgs(const QString &file) {
        qputenv("FASTECU_USBQUAL_TEST_HASH",hash(file).toLatin1());
        qputenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH",hash(mock("libusb-1.0.dll")).toLatin1());
        return {"--qualify-openport-usb-only",file,"--driver-sha256",hash(file),"--vehicle-disconnected","--reviewed-open-behavior"};
    }
    void customUsbLifecycle_data() {
        QTest::addColumn<QString>("dll");QTest::addColumn<bool>("ok");
        QTest::newRow("acknowledged")<<QString("usbqual-j2534.dll")<<true;
        QTest::newRow("missing-ack")<<QString("usbqual-no-ack.dll")<<false;
        QTest::newRow("unavailable-firmware")<<QString("usbqual-unavailable.dll")<<false;
        QTest::newRow("release-failed")<<QString("usbqual-release-failure.dll")<<false;
    }
    void customUsbLifecycle() {
        if(sizeof(void*)!=8)QSKIP("Reviewed custom OpenPort is x64 only");
        QFETCH(QString,dll);QFETCH(bool,ok);
        const auto r=run(customArgs(mock(dll)));QCOMPARE(exitCode,ok?0:1);
        QCOMPARE(r["ok"].toBool(),ok);const auto worker=r["device_result"].toObject();
        QVERIFY(worker["isolated_process"].toBool());const auto result=worker["device_result"].toObject();
        QVERIFY(!result["ecu_communication"].toBool());QVERIFY(!result["channel_opened"].toBool());
        if(ok){QVERIFY(result["initialization_acknowledged"].toBool());QVERIFY(result["cleanup_confirmed"].toBool());}
        if(dll.contains("no-ack"))QVERIFY(!result["version_called"].toBool());
        if(dll.contains("release")){const auto life=result["lifecycle"].toObject();QVERIFY(life["adapter_shutdown_confirmed"].toBool());QVERIFY(!life["usb_interface_release_confirmed"].toBool());}
        QFile trace(directory.filePath("trace.txt"));QVERIFY(trace.open(QIODevice::ReadOnly));const auto bytes=trace.readAll();
        QCOMPARE(bytes.count("open:"),1);QCOMPARE(bytes.count("close:"),1);
        for(const char *forbidden:{"connect:","filter:","write:","read:","config:","ioctl:","voltage:","periodic:"})QVERIFY(!bytes.contains(forbidden));
        qunsetenv("FASTECU_USBQUAL_TEST_HASH");qunsetenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    }
    void customModeFixtureCannotOverrideProductionHash() {
        if(sizeof(void*)!=8)QSKIP("Reviewed custom OpenPort is x64 only");
        const auto production=qEnvironmentVariable("FASTECU_DIAGNOSTIC_PRODUCTION_TOOL");
        QVERIFY2(!production.isEmpty(),"Native runner must supply the production executable for hash-override refusal");
        const auto r=run(customArgs(mock("usbqual-j2534.dll")),production);
        QCOMPARE(exitCode,2);QVERIFY(!r["device_opened"].toBool());
        qunsetenv("FASTECU_USBQUAL_TEST_HASH");qunsetenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    }
    void customExclusivePendingWorker() {
        if(sizeof(void*)!=8)QSKIP("Reviewed custom OpenPort is x64 only");
        auto args=customArgs(mock("usbqual-delayed.dll"));args<<"--wait-ms"<<"100";
        const auto pending=run(args);QCOMPARE(exitCode,1);
        const auto child=pending["device_result"].toObject();QVERIFY(!child["worker_terminated"].toBool());
        const auto token=child["worker_result_token"].toString();QVERIFY(!token.isEmpty());
        const auto second=run(customArgs(mock("usbqual-delayed.dll")));QCOMPARE(exitCode,1);
        QVERIFY(!second["device_opened"].toBool());
        const auto result=QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("fastecu-device-"+token+"/result.json");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(result),5000);
        const auto late=run({"--collect",token});QCOMPARE(exitCode,1);QVERIFY(late["prior_parent_timeout"].toBool());
        qunsetenv("FASTECU_USBQUAL_TEST_HASH");qunsetenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    }
    void managerCustomLifecycle() {
        if(sizeof(void*)!=8)QSKIP("Custom OpenPort is x64 only");
        const auto file=mock("usbqual-j2534.dll");customArgs(file);registration(file,"OpenPort custom fixture");
        const auto list=run({"--adapter-manager-list"});QCOMPARE(exitCode,0);
        bool found=false;for(const auto &v:list["adapters"].toArray())if(v.toObject()["id"].toString()==file){found=true;QVERIFY(v.toObject()["available"].toBool());}
        QVERIFY(found);
        const auto result=run({"--adapter-manager-test",file});QCOMPARE(exitCode,0);QVERIFY(result["ok"].toBool());
        qunsetenv("FASTECU_USBQUAL_TEST_HASH");qunsetenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    }
    void managerObdxLifecycle() {
        const auto file=mock();registration(file,"OBDX Pro VX");
        const auto result=run({"--adapter-manager-test",file});QCOMPARE(exitCode,0);QVERIFY(result["ok"].toBool());
        QCOMPARE(result["adapter_label"].toString(),QString("obdx-pro-vx"));
    }
    void managerMissingDependency() {
        if(sizeof(void*)!=8)QSKIP("Custom OpenPort is x64 only");
        QTemporaryDir temporary;const auto file=temporary.filePath("j2534.dll");
        QVERIFY(QFile::copy(mock("usbqual-j2534.dll"),file));customArgs(file);registration(file,"OpenPort custom fixture");
        const auto result=run({"--adapter-manager-test",file});QCOMPARE(exitCode,2);
        QVERIFY(result["error"].toString().contains("dependency",Qt::CaseInsensitive));
        QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));
        qunsetenv("FASTECU_USBQUAL_TEST_HASH");qunsetenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    }
    void managerRefusesProprietaryName() {
        const auto file=mock();registration(file,"Tactrix OpenPort 2.0");
        const auto result=run({"--adapter-manager-test",file});QCOMPARE(exitCode,2);QVERIFY(!result["device_opened"].toBool());
        QVERIFY(!QFileInfo::exists(directory.filePath("trace.txt")));
    }
    void pendingCallNotTerminated(){
        const auto file=mock("delayed-j2534.dll");registration(file);auto args=deviceArgs(file);args<<"--wait-ms"<<"100";
        const auto r=run(args);QCOMPARE(exitCode,1);const auto pending=r["device_result"].toObject();QVERIFY(!pending["worker_terminated"].toBool());QCOMPARE(pending["validation_level"].toString(),QString("device_call_pending_or_uncertain"));const auto token=pending["worker_result_token"].toString();QVERIFY(!token.isEmpty());
        const auto result=QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("fastecu-device-"+token+"/result.json");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(result),5000);
        const auto late=run({"--collect",token});QCOMPARE(exitCode,1);QVERIFY(late["prior_parent_timeout"].toBool());QVERIFY(late["device_result"].toObject()["device_result"].toObject()["cleanup_confirmed"].toBool());QVERIFY(!late["ok"].toBool());
    }
#endif
};
QTEST_GUILESS_MAIN(DiagnosticCliTest)
#include "diagnostic_cli.moc"
