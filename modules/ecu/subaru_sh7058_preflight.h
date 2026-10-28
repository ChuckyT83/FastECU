#ifndef SUBARU_SH7058_PREFLIGHT_H
#define SUBARU_SH7058_PREFLIGHT_H
#include "subaru_sh7058_safety.h"
#include "../../serial_port/communication_result.h"
#include <QMap>
#include <QJsonObject>
#include <QUuid>
#include <atomic>
namespace SubaruSH7058 {
// Candidate application requests from historical FastECU; ordinary-state target
// handlers were checked in the pinned firmware; they do not enter a new session.
struct Identification {
    QString ecu, calibration, firmware; // Raw OBD CAL-ID is not the full custom firmware label.
    QByteArray ecuReply, calibrationReply;
    enum class Source { Missing, OfflineReplay, ObservedDiagnostic } source = Source::Missing;
    quint64 generation = 0;
    QUuid observationContext; // Unique completed observation, not programming authority.
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
    // Only an offline fixture or the separately owned identity-only backend is admitted.
    virtual bool offlineFixture() const noexcept = 0;
    virtual bool isolatedDiagnostic() const noexcept { return false; }
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
    QMap<QString,QByteArray> documentHashByXmlId;
};
// Parse only identity metadata, not calibration tables. XML bytes remain private.
// Resolve actual included parent definitions; never equate a parent ID to a child.
Definition resolveDefinition(const QVector<QByteArray> &documents, const QString &selectedXmlId);
// Compiled review references, not user XML/configuration or a force-compatibility
// option. The optional dependency is used by synthetic unit fixtures; the GUI
// always uses the pinned public Alpha definition and bundled kernel below.
// Public EcuFlash standard AE5T201V.xml at TD-D/SubaruDefs Alpha
// 0b0c0c42acea0d89ffd8b50378ae13ae32b5b43d (Task 006N evidence).
struct ReviewedReferences {
    QByteArray stockDefinitionHash = QByteArray::fromHex("432328ab094160180fa0f4cffebc9528dd4a4dd432fe6d2634d4f6d69694e33a");
    QByteArray kernelHash = QByteArray::fromHex("5fb9cb8973c6a3746e0e6a3afa328e9db57c66dd8d227ab3872cd2f80946b82e");
};
struct Configuration {
    QString protocol, mcu, checksumEnabled, transport;
    const flashdev_t *map = nullptr;
    uint32_t kernelAddress = 0;
};
// No GUI/CLI producer exists. A future qualified capture must retain original
// bytes/hash and its actual observation generation. An offline reference is not
// evidence of what is currently in an ECU. This never authorizes programming.
struct CurrentImageEvidence {
    enum class Source { Unavailable, OfflineReference, QualifiedEcuCapture };
    Source source = Source::Unavailable;
    QByteArray bytes, capturedHash;
    quint64 observationGeneration = 0;
    QUuid observationContext;
};
enum class HardwareState { Compatible, Incompatible, InsufficientEvidence };
enum class IntegrityState { Valid, Invalid, Incomplete, Unsupported };
enum class TransitionState { SameComparableIdentity, DifferentComparableIdentity, PartiallyKnown, Unknown };
enum class ReadinessState { SectorRequirementsUnknown, ProtectedChangesUnsupported, ProgrammingDisabled, InvalidComparison };
template<class State> struct Assessment {
    explicit Assessment(State initial):state(initial) {}
    State state;
    QStringList reasons;
    QJsonObject evidence;
};
struct Compatibility {
    Assessment<HardwareState> hardware{HardwareState::InsufficientEvidence};
    Assessment<IntegrityState> integrity{IntegrityState::Valid};
    Assessment<TransitionState> transition{TransitionState::Unknown};
    Assessment<ReadinessState> readiness{ReadinessState::SectorRequirementsUnknown};
    QByteArray originalHash, finalHash;
    QJsonObject identifiers;
    QVector<unsigned> requiredSectors;
    bool sectorsKnown = false, checksumCorrectionRequired = false;
    bool programmingAllowed() const { return false; }
    void recordFileRecheck(const QByteArray &hash); // Empty hash means unreadable.
    QString summary() const;
    QJsonObject json() const;
};
Compatibility checkCompatibility(const QByteArray &image, const QByteArray &originalHash,
    const QByteArray &kernel, const Definition &, const Configuration &, const Identification &,
    const ReviewedReferences &references = ReviewedReferences(),
    const CurrentImageEvidence &current = CurrentImageEvidence());
// No admission token exists for the historical programming-related ROM Read.
inline bool legacyReadAdmitted() { return false; }
}
#endif
