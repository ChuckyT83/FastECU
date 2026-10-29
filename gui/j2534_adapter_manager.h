#ifndef FASTECU_GUI_J2534_ADAPTER_MANAGER_H
#define FASTECU_GUI_J2534_ADAPTER_MANAGER_H
#include <QDialog>
#include <QProcess>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <functional>

// Application-owned controller survives closing/reopening the dialog. It never
// loads native adapter code into the GUI or retries a hardware operation.
class J2534AdapterController : public QObject {
    Q_OBJECT
public:
    explicit J2534AdapterController(QObject *parent=nullptr);
    bool busy() const {return active || uncertain;}
    QJsonArray adapters;QJsonObject lastReport;
    void refresh();
    void test(const QString &id);
    std::function<bool()> transportBusy;
signals:
    void changed();
private:
    QProcess process;QTimer watchdog,collection;
    bool active=false,uncertain=false;QString operation,token;
    void launch(const QStringList &arguments,const QString &mode);
    void finish();
};
class J2534AdapterManager : public QDialog {
    Q_OBJECT
public:
    explicit J2534AdapterManager(J2534AdapterController *controller,QWidget *parent=nullptr);
private:
    J2534AdapterController *controller;
    QComboBox *adapters;QPushButton *refresh,*test;QLabel *status;
    QLineEdit *firmware,*dll,*api;QPlainTextEdit *details;
    void updateState();
};
#endif
