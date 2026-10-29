#include "j2534_adapter_manager.h"
#include "../serial_port/j2534_adapter_selection.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QSettings>
#include <QSignalBlocker>

J2534AdapterController::J2534AdapterController(QObject *parent,const QString &program):QObject(parent),helper(program.isEmpty()?QDir(QCoreApplication::applicationDirPath()).filePath("diagnostics/x64/fastecu-j2534-diagnostics.exe"):program) {
    connect(&process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int,QProcess::ExitStatus){finish();});
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error!=QProcess::FailedToStart)return;
        active=false;watchdog.stop();lastReport={{"error","Internal adapter diagnostic component is missing or could not start. Re-extract the complete application package."}};emit changed();
    });
    watchdog.setSingleShot(true);watchdog.setInterval(20000);
    connect(&watchdog,&QTimer::timeout,this,[this]{
        if(operation=="test")uncertain=true;
        lastReport={{"error","Adapter diagnostic process is still pending; no operation was terminated or retried."}};
        emit changed();
    });
    collection.setSingleShot(true);collection.setInterval(1000);
    connect(&collection,&QTimer::timeout,this,[this]{if(!active && !token.isEmpty())launch({"--collect",token},"collect");});
}
void J2534AdapterController::launch(const QStringList &args,const QString &mode) {
    if(active)return;
    active=true;operation=mode;
    process.setProgram(helper);
    process.setArguments(args);watchdog.start();emit changed();process.start();
}
void J2534AdapterController::refresh() {if(!busy())launch({"--adapter-manager-list"},"list");}
void J2534AdapterController::test(const QString &id) {
    if(busy())return;
    if(transportBusy && transportBusy()) {lastReport={{"error","FastECU already owns a transport. Stop the existing connection before testing an adapter."}};emit changed();return;}
    lastReport={{"error","Testing adapter USB connection…"}};
    launch({"--adapter-manager-test",id},"test");
}
void J2534AdapterController::finish() {
    watchdog.stop();active=false;
    const auto bytes=process.readAllStandardOutput();
    auto report=QJsonDocument::fromJson(bytes).object();
    if(report.isEmpty()) {
        if(operation=="test")uncertain=true;
        report={{"error",QString("Adapter diagnostic process exited without a usable result (%1). %2").arg(process.exitCode()).arg(QString::fromLocal8Bit(process.readAllStandardError()).left(4000))}};
    }
    if(operation=="list") {adapters=report["adapters"].toArray();if(report.contains("adapters"))report={};}
    else {
        const auto view=J2534Adapters::presentation(report);
        if(operation=="test" && view["pending"].toBool()) {
            uncertain=true;token=view["token"].toString();
        }
        if(operation=="collect" && report.contains("device_result")) {
            // Collect only observes an already completed worker; never reopens.
            uncertain=false;token.clear();
            report["error"]="Adapter test completed after the wait limit. Review its late result in technical details.";
        }
    }
    lastReport=report;emit changed();
    if(uncertain && !token.isEmpty())collection.start();
}
J2534AdapterManager::J2534AdapterManager(J2534AdapterController *c,QWidget *parent):QDialog(parent),controller(c) {
    setWindowTitle(tr("J2534 Adapter Manager"));resize(650,330);
    auto *layout=new QVBoxLayout(this);auto *form=new QFormLayout;
    adapters=new QComboBox;adapters->setObjectName("adapterSelection");form->addRow(tr("Adapter:"),adapters);
    status=new QLabel(tr("Select an adapter to test its USB connection."));status->setObjectName("adapterStatus");status->setWordWrap(true);form->addRow(tr("Status:"),status);
    auto field=[](const char *name){auto *f=new QLineEdit;f->setObjectName(name);f->setReadOnly(true);return f;};
    firmware=field("adapterFirmware");dll=field("adapterDllVersion");api=field("adapterApiVersion");
    form->addRow(tr("Firmware:"),firmware);form->addRow(tr("DLL version:"),dll);form->addRow(tr("J2534 API:"),api);layout->addLayout(form);
    auto *buttons=new QHBoxLayout;refresh=new QPushButton(tr("Refresh"));refresh->setObjectName("adapterRefresh");test=new QPushButton(tr("Test Connection"));test->setObjectName("adapterTest");
    buttons->addWidget(refresh);buttons->addWidget(test);buttons->addStretch();layout->addLayout(buttons);
    auto *toggle=new QToolButton;toggle->setText(tr("Technical details"));toggle->setCheckable(true);toggle->setArrowType(Qt::RightArrow);toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);layout->addWidget(toggle);
    details=new QPlainTextEdit;details->setReadOnly(true);details->hide();layout->addWidget(details);
    connect(toggle,&QToolButton::toggled,this,[this,toggle](bool shown){details->setVisible(shown);toggle->setArrowType(shown?Qt::DownArrow:Qt::RightArrow);});
    auto *note=new QLabel(tr("USB adapter test only. No ECU channel, vehicle requests or programming."));note->setWordWrap(true);layout->addWidget(note);
    connect(refresh,&QPushButton::clicked,controller,&J2534AdapterController::refresh);
    connect(test,&QPushButton::clicked,this,[this]{controller->test(adapters->currentData().toString());});
    connect(adapters,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){
        QSettings().setValue("J2534/selectedAdapter",adapters->currentData().toString());controller->lastReport={};updateState();
    });
    connect(controller,&J2534AdapterController::changed,this,&J2534AdapterManager::updateState);updateState();
}
void J2534AdapterManager::updateState() {
    const auto selected=QSettings().value("J2534/selectedAdapter").toString();
    if(adapters->count()!=controller->adapters.size() || adapters->property("snapshot").toByteArray()!=QJsonDocument(controller->adapters).toJson()) {
        QSignalBlocker block(adapters);adapters->clear();int preferred=-1,firstAvailable=-1;
        for(const auto &v:controller->adapters) {const auto a=v.toObject();const int i=adapters->count();adapters->addItem(a["name"].toString()+(a["available"].toBool()?QString():tr(" — unavailable")),a["id"].toString());
            adapters->setItemData(i,a["reason"].toString(),Qt::ToolTipRole);
            if(firstAvailable<0 && a["available"].toBool())firstAvailable=i;
            if(a["id"].toString()==selected && a["available"].toBool())preferred=i;
        }
        adapters->setCurrentIndex(preferred>=0?preferred:firstAvailable>=0?firstAvailable:0);
        adapters->setProperty("snapshot",QJsonDocument(controller->adapters).toJson());
    }
    const auto a=(adapters->currentIndex()>=0 && adapters->currentIndex()<controller->adapters.size()?controller->adapters.at(adapters->currentIndex()).toObject():QJsonObject());
    refresh->setEnabled(!controller->busy());adapters->setEnabled(!controller->busy());test->setEnabled(!controller->busy() && a["available"].toBool());
    const auto view=J2534Adapters::presentation(controller->lastReport);
    status->setText(controller->busy()?tr("Adapter operation pending. Overlapping tests are disabled."):controller->adapters.isEmpty()?tr("No adapters found. Refresh to discover installed drivers."):!a["available"].toBool()?a["reason"].toString():controller->lastReport.isEmpty()?tr("Ready to test USB connection."):view["status"].toString());
    firmware->setText(view["firmware"].toString());dll->setText(view["dll"].toString());api->setText(view["api"].toString());
    details->setPlainText(QString::fromUtf8(QJsonDocument(controller->lastReport).toJson(QJsonDocument::Indented)));
}
