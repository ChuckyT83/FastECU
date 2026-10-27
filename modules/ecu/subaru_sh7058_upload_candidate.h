#ifndef SUBARU_SH7058_UPLOAD_CANDIDATE_H
#define SUBARU_SH7058_UPLOAD_CANDIDATE_H
#include "../../serial_port/communication_result.h"
#include "subaru_sh7058_kernel_layout.h"
#include <atomic>

namespace SubaruSH7058 {
// This is deliberately not a capability for live programming. The concrete
// checked-serial link always returns Unreviewed. Only an injected deterministic
// test transport can establish ControlledOfflineLoader; no runtime switch exists.
enum class StreamContext { Unreviewed, ControlledOfflineLoader };
class UploadLink {
public:
    virtual ~UploadLink() = default;
    virtual StreamContext context() const noexcept = 0;
    // Local lifecycle fence, not an ECU response correlation field. Zero means
    // no reviewed lease. A changed/replaced channel must get a new generation.
    virtual quint64 generation() const noexcept = 0;
    // Consume the loader lease on every attempt, including successful startup.
    // This does not flush/reset a device or imply an uncertain ECU command stopped.
    virtual void retireContext() noexcept = 0;
    virtual CommunicationResult submit(const QByteArray &bytes) = 0;
    virtual CommunicationResult receive(uint16_t timeout) = 0;
};
class UploadClock {
public:
    virtual ~UploadClock() = default;
    virtual qint64 nowMs() const noexcept = 0; // monotonic; injectable for deterministic tests
};
struct UploadCancellation { std::atomic<bool> requested{false}; };
class UploadCandidate {
public:
    enum class Phase { Entry, Download, Block, Exit, Start, Readiness };
    enum class Outcome { EntryBlocked, InvalidImage, Reused, Cancelled, TransportFailure,
        SubmissionUncertain, Timeout, DeadlineExceeded, Malformed, Unrelated, Rejected, Unsupported,
        OwnershipUncertain, StartupUncertain, ReadyForOfflineReview,
        ReadyAfterUncertainStartReviewRequired };
    enum class Startup { NotRequested, Submitted, Acknowledged, Uncertain, Failed, KernelResponding };
    struct Report {
        Outcome outcome = Outcome::EntryBlocked;
        Phase phase = Phase::Entry;
        Startup startup = Startup::NotRequested;
        bool startupAcknowledged = false;
        bool kernelResponding = false;
        bool commandMayHaveExecuted = false;
        uint32_t acknowledgedBlocks = 0;
        uint32_t submissions = 0;
        CommunicationResult transport;
        CommunicationResult startupTransport; // preserve missing-71 status even if BEEF later responds
        QString detail;
        bool programmingAllowed() const { return false; }
    };
    explicit UploadCandidate(const UploadClock *clock = nullptr) : clock_(clock) {}
    // The caller exclusively owns link throughout run(); generation is a lifecycle
    // fence, not a substitute for synchronization or proof against delayed traffic.
    // Input is the existing prepare_kernel encrypted output, never a new crypto
    // strategy. This candidate is intentionally restricted to the pinned 6528-
    // byte prepared kernel layout (51 blocks). No security/session requests exist.
    Report run(UploadLink &link, const QByteArray &prepared,
               const KernelLayout &layout, uint16_t responseTimeout,
               const UploadCancellation &cancellation);
    static QByteArray kernelIdentityReply();
private:
    const UploadClock *clock_;
    std::atomic<bool> used_{false};
};
}
#endif
