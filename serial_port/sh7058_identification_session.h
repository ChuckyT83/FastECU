#ifndef SH7058_IDENTIFICATION_SESSION_H
#define SH7058_IDENTIFICATION_SESSION_H
#include "../modules/ecu/subaru_sh7058_preflight.h"
#include <QJsonObject>
#include <QCommandLineParser>
#ifdef Q_OS_WIN
#include "serial_port_actions_direct.h"
// A private backend owned exclusively by one isolated worker process. It cannot
// submit arbitrary requests or expose its channel to MainWindow/background I/O.
class SH7058IdentificationSession final : public SubaruSH7058::IdentificationLink {
    SerialPortActionsDirect port_;
    QString library_;
    bool used_=false;
    quint64 epoch_=0;
    QStringList errors_;
public:
    explicit SH7058IdentificationSession(const QString &library);
    QStringList errors() const {return errors_;}
    bool offlineFixture() const noexcept override {return false;}
    bool isolatedDiagnostic() const noexcept override {return true;}
    bool acquire() override;
    quint64 generation() const noexcept override;
    CommunicationResult submit(const QByteArray &) override;
    CommunicationResult receive(uint16_t) override;
    bool release() noexcept override;
    SerialPortActionsDirect::CleanupResult cleanup() const {return port_.last_cleanup();}
};
#endif
void addSH7058IdentificationOptions(QCommandLineParser &);
int runSH7058IdentificationWorker(const QCommandLineParser &);
QJsonObject launchSH7058Identification(const QString &driverId,const QString &hash);
#endif
