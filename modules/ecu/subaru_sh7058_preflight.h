#ifndef SUBARU_SH7058_PREFLIGHT_H
#define SUBARU_SH7058_PREFLIGHT_H
#include "subaru_sh7058_safety.h"
#include "../../serial_port/communication_result.h"
#include <QMap>
#include <QJsonObject>
#include <atomic>
namespace SubaruSH7058 {
// Candidate application requests from historical FastECU; ordinary-state target
// support/effects have NOT been established from the unavailable private firmware.
struct Identification {
    QString ecu, calibration;
    QByteArray ecuReply, calibrationReply;
    enum class Source { Missing, OfflineReplay, ObservedDiagnostic } source = Source::Missing;
    quint64 generation = 0;
};
QByteArray identityRequest(bool calibration);
bool parseIdentityReply(bool calibration, const QByteArray &frame, QString &identifier, QString &error);
class IdentificationClock {
public:
    virtual ~IdentificationClock() = default;
    virtual qint64 nowMs() const noexcept = 0;
};
class IdentificationLink {
public:
    virtual ~IdentificationLink() = default;
    // The only admitted context is an injected offline fixture, never live J2534.
    virtual bool offlineFixture() const noexcept = 0;
    virtual bool acquire() = 0; // Exclusive device/channel/receive queue ownership.
    virtual quint64 generation() const noexcept = 0;
    virtual CommunicationResult submit(const QByteArray &) = 0;
    virtual CommunicationResult receive(uint16_t) = 0;
    virtual bool release() noexcept = 0; // Release only what acquire owned; no ECU reset/session switch.
};
class UnavailableIdentificationLink final : public IdentificationLink {
public:
    bool offlineFixture() const noexcept override { return false; }
    bool acquire() override { return false; }
    quint64 generation() const noexcept override { return 0; }
    CommunicationResult submit(const QByteArray &) override { return {}; }
    CommunicationResult receive(uint16_t) override { return {}; }
    bool release() noexcept override { return false; }
};
struct IdentificationResult {
    Identification identity;
    CommunicationResult transport;
    bool complete = false, cleanupConfirmed = false, acquired = false;
    QString detail;
};
IdentificationResult identify(IdentificationLink &, const std::atomic<bool> &cancel,
                              uint16_t timeoutMs, const IdentificationClock *clock = nullptr);
struct Definition {
    bool valid = false;
    QString error, xmlId;
    QStringList ancestry, documentHashes;
    QMap<QString,QString> fields;
};
// Parse only identity metadata, not calibration tables. XML bytes remain private.
// Resolve actual included parent definitions; never equate a parent ID to a child.
Definition resolveDefinition(const QVector<QByteArray> &documents, const QString &selectedXmlId);
struct Target {
    QString ecu = "A62A907107", calibration = "ZE5T201V_DM24000", parent = "AE5T201V";
    QByteArray imageHash = QByteArray::fromHex("15102939df167a4d76be9f33c20966fa4f707bb171aff846c7117108cc9c875c");
    QByteArray kernelHash = QByteArray::fromHex("5fb9cb8973c6a3746e0e6a3afa328e9db57c66dd8d227ab3872cd2f80946b82e");
};
struct Configuration {
    QString protocol, mcu, checksumEnabled, transport;
    const flashdev_t *map = nullptr;
    uint32_t kernelAddress = 0;
};
struct Compatibility {
    enum class State { CompatibleEvidence, Incompatible, MissingEvidence, Unsupported, HardwareQualificationRequired };
    State state = State::MissingEvidence;
    QByteArray originalHash, finalHash;
    QStringList reasons;
    bool checksumCorrectionRequired = false;
    bool programmingAllowed() const { return false; }
    QJsonObject json() const;
};
Compatibility checkCompatibility(const QByteArray &image, const QByteArray &originalHash,
    const QByteArray &kernel, const Definition &, const Configuration &, const Identification &,
    const Target &target = Target());
// No admission token exists for the historical programming-related ROM Read.
inline bool legacyReadAdmitted() { return false; }
}
#endif
