#include <QtTest>
#include <QTemporaryDir>
#include <QLibrary>
#include "../../tools/j2534_diagnostics/operation_policy.h"
#include "../../tools/j2534_diagnostics/reviewed_openport.h"
#include "../../serial_port/j2534_diagnostics.h"
#ifdef Q_OS_WIN
#include "../../tools/j2534_diagnostics/windows_device_api.h"
#include "../../serial_port/J2534_tactrix_win.h"
#endif
using namespace J2534Diagnostics;
class FakeDevice : public DeviceOnlyApi {
public:
    QStringList calls;
    bool custom=false; QJsonObject evidence;
    bool reviewedOpenPort() const override{return custom;}
    QJsonObject lifecycleEvidence() const override{return evidence;}
    bool loadOk=true; long openStatus=0,versionStatus=0,closeStatus=0; int metadata=0;
    bool load(const QString &,QString &error) override {calls<<"load";error="Injected load failure";return loadOk;}
    long open(unsigned long &id) override{calls<<"open";id=0;return openStatus;}
    long version(unsigned long id,char *api,char *dll,char *fw) override {
        calls<<"version";Q_ASSERT(id==0);
        if(metadata==3){std::strcpy(api,"04.04");std::strcpy(dll,"synthetic");std::strcpy(fw,"garbage");}
        else if(metadata==2){std::strcpy(api,"04.04");std::strcpy(dll,"synthetic");std::strcpy(fw,"unavailable");}
        else if(metadata) {std::memset(api,'V',80);std::memset(dll,'V',80);std::memset(fw,'V',80);}
        else {std::strcpy(api,"04.04");std::strcpy(dll,"synthetic");std::strcpy(fw,custom?"1.17.4877":"synthetic");}return versionStatus;
    }
    long close(unsigned long id) override {calls<<"close";Q_ASSERT(id==0);return closeStatus;}
    void unload() override{calls<<"unload";}
};
class DiagnosticsTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    Driver driver;
    static QByteArray pe(unsigned machine) {
        QByteArray bytes(512,'\0');bytes[0]='M';bytes[1]='Z';bytes[60]=64;
        bytes.replace(64,4,QByteArray("PE\0\0",4));bytes[68]=char(machine);bytes[69]=char(machine>>8);
        bytes[70]=1;bytes[84]=char(machine==0x14c?224:240);
        bytes[86]=0;bytes[87]=0x20; // IMAGE_FILE_DLL
        bytes[88]=0x0b;bytes[89]=machine==0x14c?1:2;
        return bytes;
    }
    void write(const QByteArray &bytes) {QFile file(driver.path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));}
private slots:
    void init(){driver={};driver.path=directory.filePath("fixture.dll");driver.api="04.04";write(pe(nativeMachine()));}
    void inspect_data() {
        QTest::addColumn<QString>("fault");
        for(const char *f:{"valid","missing","relative","empty","nul","not-pe","bad-offset","bad-signature","not-dll","wrong-machine","inconsistent","optional-size","sections","truncated","unsupported"})QTest::newRow(f)<<QString(f);
    }
    void inspect() {
        QFETCH(QString,fault);auto bytes=pe(nativeMachine());
        if(fault=="missing")driver.path+=".missing";
        else if(fault=="relative")driver.path="relative.dll";
        else if(fault=="empty")driver.path.clear();
        else if(fault=="nul")driver.path+=QChar('\0');
        else if(fault=="not-pe")write("not a DLL");
        else if(fault=="bad-offset"){bytes[63]=char(0xff);write(bytes);}
        else if(fault=="bad-signature"){bytes[64]='X';write(bytes);}
        else if(fault=="not-dll"){bytes[87]=0;write(bytes);}
        else if(fault=="wrong-machine")write(pe(nativeMachine()==0x8664?0x14c:0x8664));
        else if(fault=="inconsistent"){bytes[89]=0;write(bytes);}
        if(fault=="optional-size"){bytes[84]=1;write(bytes);}
        if(fault=="sections"){bytes[70]=char(97);write(bytes);}
        if(fault=="truncated")write(bytes.left(100));
        if(fault=="unsupported"){bytes[68]=char(0xff);bytes[69]=char(0xff);write(bytes);}
        J2534Diagnostics::inspect(driver);QCOMPARE(driver.issue.isEmpty(),fault=="valid");
    }
    void registryEntries() {
        QSettings fixture(directory.filePath("registry.ini"),QSettings::IniFormat);fixture.clear();
        fixture.setValue("valid/Name","Synthetic J2534");fixture.setValue("valid/FunctionLibrary","\""+driver.path+"\"");
        fixture.setValue("missing/FunctionLibrary",driver.path+".missing");fixture.setValue("malformed/FunctionLibrary",123);
        const auto entries=readRegistry(fixture,"04.04","fixture");QCOMPARE(entries.size(),3);
        int valid=0;for(const auto &e:entries)if(e.issue.isEmpty()){++valid;QCOMPARE(e.path,driver.path);QCOMPARE(e.description,QString("Synthetic J2534"));}
        QCOMPARE(valid,1);
    }
    void registryEmpty() {QSettings fixture(directory.filePath("empty.ini"),QSettings::IniFormat);fixture.clear();QVERIFY(readRegistry(fixture,"04.04","fixture").isEmpty());}
    void device_data() {
        QTest::addColumn<QString>("fault");
        for(const char *f:{"success","not-requested","vehicle-connected","load","open","version","malformed-version","close","api-5","missing","bitness"})QTest::newRow(f)<<QString(f);
    }
    void device() {
        QFETCH(QString,fault);FakeDevice api;
        if(fault=="load")api.loadOk=false;
        if(fault=="open")api.openStatus=7;
        if(fault=="version")api.versionStatus=1;
        if(fault=="malformed-version")api.metadata=1;
        if(fault=="close")api.closeStatus=7;
        if(fault=="api-5")driver.api="05.00";
        if(fault=="missing")driver.path+=".missing";
        if(fault=="bitness")write(pe(nativeMachine()==0x8664?0x14c:0x8664));
        const auto report=validateDevice(driver,fault!="not-requested",fault!="vehicle-connected",api);
        const bool ok=fault=="success"||fault=="version"||fault=="malformed-version";
        QCOMPARE(report["ok"].toBool(),ok);QVERIFY(!report["ecu_communication"].toBool());QVERIFY(!report["channel_opened"].toBool());
        if(ok || fault=="close")QCOMPARE(api.calls,QStringList({"load","open","version","close","unload"}));
        else if(fault=="open")QCOMPARE(api.calls,QStringList({"load","open","unload"}));
        else if(fault=="load")QCOMPARE(api.calls,QStringList({"load","unload"}));
        else QVERIFY(api.calls.isEmpty());
        if(fault=="close")QVERIFY(!report["cleanup_confirmed"].toBool());
        if(fault=="version"||fault=="malformed-version")QVERIFY(report.contains("metadata_warning"));
    }
    void processArchitecture() {const auto expected=QSysInfo::buildCpuArchitecture();QCOMPARE(nativeMachine(),expected=="x86_64"?0x8664u:expected=="i386"?0x14cu:expected=="arm64"?0xaa64u:0u);}
    void metadataAndPrivacy() {
        const auto metadata=DiagnosticReport::fileInfo(driver.path);QVERIFY(metadata["exists"].toBool());
        QCOMPARE(metadata["sha256"].toString(),QString::fromLatin1(QCryptographicHash::hash(pe(nativeMachine()),QCryptographicHash::Sha256).toHex()));
        const QJsonObject raw{{"dll_path","C:/Users/SyntheticOwner/private/fixture.dll"},{"device_instance_id","USB\\VID_1234&PID_5678\\PRIVATE-SERIAL"}};
        const auto redacted=DiagnosticReport::redact(raw,false).toObject();QVERIFY(!QJsonDocument(redacted).toJson().contains("SyntheticOwner"));QVERIFY(!QJsonDocument(redacted).toJson().contains("PRIVATE-SERIAL"));
        QCOMPARE(DiagnosticReport::redact(raw,true).toObject(),raw);
        QVERIFY(!DiagnosticReport::fileInfo(directory.path())["exists"].toBool());
    }
    void executableInspection() {auto bytes=pe(nativeMachine());bytes[87]=0;write(bytes);J2534Diagnostics::inspect(driver,nativeMachine(),false);QVERIFY(driver.issue.isEmpty());QVERIFY(!driver.isDll);}
    void networkPathsNotAccessed() {driver.path="//unreachable.invalid/share/driver.dll";J2534Diagnostics::inspect(driver);QVERIFY(driver.issue.contains("Network"));QVERIFY(!DiagnosticReport::fileInfo(driver.path)["exists"].toBool());}
    void devicePolicy_data() {
        QTest::addColumn<QString>("kind");QTest::addColumn<bool>("disconnected");QTest::addColumn<bool>("reviewed");QTest::addColumn<QString>("hash");QTest::addColumn<bool>("allowed");
        QTest::newRow("explicit-reviewed-obdx")<<QString("obdx-pro-vx")<<true<<true<<QString(64,'a')<<true;
        QTest::newRow("clone-blocked")<<QString("openport-clone")<<true<<true<<QString(64,'a')<<false;
        QTest::newRow("unreviewed-blocked")<<QString("unreviewed")<<true<<true<<QString(64,'a')<<false;
        QTest::newRow("vehicle-ack-missing")<<QString("obdx-pro-vx")<<false<<true<<QString(64,'a')<<false;
        QTest::newRow("firmware-behavior-unknown")<<QString("obdx-pro-vx")<<true<<false<<QString(64,'a')<<false;
        QTest::newRow("hash-missing")<<QString("obdx-pro-vx")<<true<<true<<QString()<<false;
        QTest::newRow("hash-invalid")<<QString("obdx-pro-vx")<<true<<true<<QString(64,'x')<<false;
    }
    void devicePolicy() {QFETCH(QString,kind);QFETCH(bool,disconnected);QFETCH(bool,reviewed);QFETCH(QString,hash);QFETCH(bool,allowed);QCOMPARE(DiagnosticReport::devicePolicy(driver,kind,disconnected,reviewed,hash).isEmpty(),allowed);}
    void openPortAlwaysPassive() {driver.description="Tactrix OpenPort 2.0";QVERIFY(!DiagnosticReport::devicePolicy(driver,"obdx-pro-vx",true,true,QString(64,'a')).isEmpty());}
    void customLifecycle_data() {
        QTest::addColumn<QString>("fault");
        for(const char *f:{"success","no-ati","no-ata","shutdown","release","handle","context","close-marker","unavailable","bad-firmware","version-failure","close-failure","version-and-close","open-failure"})QTest::newRow(f)<<QString(f);
    }
    void customLifecycle() {
        QFETCH(QString,fault);FakeDevice api;api.custom=true;
        QByteArray log="USBQUAL/1 begin=1\nUSBQUAL/1 ati_ack=1\nUSBQUAL/1 ata_ack=1\nUSBQUAL/1 open_complete=1\nUSBQUAL/1 shutdown_ack=1\nUSBQUAL/1 release_status=0\nUSBQUAL/1 handle_close_returned=1\nUSBQUAL/1 context_exit_returned=1\nUSBQUAL/1 close_status=0\n";
        if(fault=="no-ati")log.replace("ati_ack=1","ati_ack=0");
        if(fault=="no-ata")log.replace("ata_ack=1","ata_ack=0");
        if(fault=="shutdown")log.replace("shutdown_ack=1","shutdown_ack=0");
        if(fault=="release")log.replace("release_status=0","release_status=-6");
        if(fault=="handle")log.replace("handle_close_returned=1","handle_close_returned=0");
        if(fault=="context")log.replace("context_exit_returned=1","context_exit_returned=0");
        if(fault=="close-marker")log.replace("close_status=0","close_status=9");
        if(fault=="unavailable")api.metadata=2;
        if(fault=="bad-firmware")api.metadata=3;
        if(fault=="version-failure")api.versionStatus=7;
        if(fault=="close-failure")api.closeStatus=9;
        if(fault=="open-failure")api.openStatus=9;
        if(fault=="version-and-close"){api.versionStatus=7;api.closeStatus=9;}
        api.evidence=DiagnosticReport::openPortLifecycleEvidence(log);
        const auto report=validateDevice(driver,true,true,api);
        QCOMPARE(report["ok"].toBool(),fault=="success");
        QCOMPARE(api.calls.count("open"),1);
        QCOMPARE(api.calls.count("close"),fault=="open-failure"?0:1);
        if(fault=="no-ati" || fault=="no-ata"){QVERIFY(!api.calls.contains("version"));QVERIFY(report["version_status"].isNull());}
        if(fault=="version-and-close"){QCOMPARE(report["primary_error_stage"].toString(),QString("version"));QVERIFY(report.contains("cleanup_error"));}
        QVERIFY(!report["ecu_communication"].toBool());QVERIFY(!report["channel_opened"].toBool());
        if(fault=="shutdown" || fault=="release" || fault=="handle" || fault=="context")QVERIFY(!report["cleanup_confirmed"].toBool());
    }
    void malformedLifecycleEvidence() {
        for(const auto &log:{QByteArray(),QByteArray("USBQUAL/1 begin=1\nUSBQUAL/1 begin=1\n"),QByteArray("USBQUAL/1 begin=garbage\n"),QByteArray(65537,'X')}) {
            const auto evidence=DiagnosticReport::openPortLifecycleEvidence(log);
            QVERIFY(!evidence["initialization_acknowledged"].toBool());
            QVERIFY(!evidence["kernel_handle_release_independently_observed"].toBool());
        }
    }
    void reviewedOpenPortPolicyIsNarrow() {
        if(nativeMachine()!=0x8664)QSKIP("Reviewed OpenPort is x64 only");
        const auto dependency=QFileInfo(driver.path).dir().filePath("libusb-1.0.dll");
        QFile dep(dependency);QVERIFY(dep.open(QIODevice::WriteOnly));dep.write(pe(0x8664));dep.close();
        const auto expected=DiagnosticReport::fileInfo(driver.path)["sha256"].toString();
        const auto depHash=DiagnosticReport::fileInfo(dependency)["sha256"].toString();
        qputenv("FASTECU_USBQUAL_TEST_HASH",expected.toLatin1());qputenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH",depHash.toLatin1());
        J2534Diagnostics::inspect(driver);
        QVERIFY2(DiagnosticReport::reviewedOpenPortPolicy(driver,expected,true,true).isEmpty(),qPrintable(DiagnosticReport::reviewedOpenPortPolicy(driver,expected,true,true)));
        QVERIFY(!DiagnosticReport::reviewedOpenPortPolicy(driver,expected,false,true).isEmpty());
        QVERIFY(!DiagnosticReport::reviewedOpenPortPolicy(driver,expected,true,false).isEmpty());
        QVERIFY(!DiagnosticReport::reviewedOpenPortPolicy(driver,QString(64,'a'),true,true).isEmpty());
        QVERIFY(!DiagnosticReport::devicePolicy(driver,"openport-clone",true,true,expected).isEmpty());
        QVERIFY(dep.open(QIODevice::Append));dep.write("changed");dep.close();
        QVERIFY(!DiagnosticReport::reviewedOpenPortPolicy(driver,expected,true,true).isEmpty());
        qunsetenv("FASTECU_USBQUAL_TEST_HASH");qunsetenv("FASTECU_USBQUAL_TEST_DEPENDENCY_HASH");
    }
    void identificationBlocked() {const auto plan=identificationPlan();QVERIFY(!plan["ok"].toBool());QVERIFY(!plan["ecu_communication"].toBool());QVERIFY(plan["allowed_requests"].toArray().isEmpty());}
    void sinkDisabled() {J2534Trace trace;QVERIFY(!trace.enabled());trace.record("test");QVERIFY(!trace.failed());}
    void sinkOversizedRecord() {
        const auto path=directory.filePath("oversized-record.jsonl");J2534Trace trace;QVERIFY(trace.start(path));
        trace.record("oversized",{{"payload",QString(40000,'X')}});trace.finish();QVERIFY(trace.dropped()>0);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.size()<32768);
        const auto bytes=file.readAll();QVERIFY(!bytes.contains(QByteArray(40000,'X')));QVERIFY(bytes.contains("dropped_total"));
    }
    void sinkConcurrentBounded() {
        const auto path=directory.filePath("concurrent.jsonl");J2534Trace trace;QVERIFY(trace.start(path));
        std::vector<std::thread> producers;
        for(int i=0;i<4;++i)producers.emplace_back([&]{for(int n=0;n<1500;++n)trace.record("synthetic",{{"payload",QString(8000,'K')}});});
        for(auto &t:producers)t.join();
        trace.finish();
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.size()<=16*1024*1024);
        quint64 last=0;while(!file.atEnd()){const auto o=QJsonDocument::fromJson(file.readLine()).object();QVERIFY(!o.isEmpty());if(o.contains("sequence")){const auto seq=o["sequence"].toString().toULongLong();QVERIFY(seq>last);last=seq;}}
    }
#ifdef Q_OS_WIN
    void nativeMockDevice_data(){QTest::addColumn<QString>("fault");for(const char *f:{"success","open","close","version","missing-exports","invalid-dll","no-ioctl"})QTest::newRow(f)<<QString(f);}
    void nativeMockDevice() {
        QFETCH(QString,fault);const auto file=fault=="no-ioctl"?"incomplete-j2534.dll":fault=="missing-exports"?"incomplete-device-j2534.dll":"mock-j2534.dll";
        QLibrary mock(QDir::current().absoluteFilePath(file));QVERIFY(mock.load());
        auto reset=reinterpret_cast<void(PT_CALL*)()>(mock.resolve("MockReset"));
        auto fail=reinterpret_cast<void(PT_CALL*)(const char*)>(mock.resolve("MockSetFailure"));
        auto calls=reinterpret_cast<const char*(PT_CALL*)()>(mock.resolve("MockTrace"));QVERIFY(reset&&fail&&calls);reset();
        const auto f=fault.toLatin1();if(fault!="success")fail(f.constData());
        if(fault!="invalid-dll")driver.path=mock.fileName();
        WindowsDeviceApi api;const auto report=validateDevice(driver,true,true,api);
        QCOMPARE(report["ok"].toBool(),fault=="success"||fault=="version"||fault=="no-ioctl");
        const QByteArray trace(calls());QVERIFY(!trace.contains("connect"));QVERIFY(!trace.contains("filter"));
        QCOMPARE(trace.count("close:"),fault=="open"||fault=="missing-exports"||fault=="invalid-dll"?0:1);
    }
    void nativeRegistryViews_data(){QTest::addColumn<bool>("view64");QTest::newRow("32-bit-view")<<false;QTest::newRow("64-bit-view")<<true;}
    void nativeRegistryViews() {
        QFETCH(bool,view64);const auto root="HKEY_CURRENT_USER\\Software\\FastECU-Diagnostics-"+QUuid::createUuid().toString(QUuid::Id128);
        QSettings registry(root,view64?QSettings::Registry64Format:QSettings::Registry32Format);
        struct Cleanup{QSettings &r;~Cleanup(){r.clear();r.sync();}}cleanup{registry};
        registry.setValue("synthetic/Name","Mock driver");registry.setValue("synthetic/FunctionLibrary",driver.path);registry.sync();
        const auto entries=readRegistry(registry,"04.04",view64?"x64":"x86");QCOMPARE(entries.size(),1);QCOMPARE(entries.first().path,driver.path);QVERIFY(entries.first().issue.isEmpty());
    }
#endif
};
QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "diagnostics.moc"
