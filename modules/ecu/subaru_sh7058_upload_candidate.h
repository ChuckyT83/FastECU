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
    virtual StreamContext context() const = 0;
    virtual CommunicationResult submit(const QByteArray &bytes) = 0;
    virtual CommunicationResult receive(uint16_t timeout) = 0;
};
struct UploadCancellation { std::atomic<bool> requested{false}; };
class UploadCandidate {
public:
    enum class Phase { Entry, Download, Block, Exit, Start, Readiness };
    enum class Outcome { EntryBlocked, InvalidImage, Reused, Cancelled, TransportFailure,
        SubmissionUncertain, Timeout, Malformed, Unrelated, Rejected, Unsupported,
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
    // Input is the existing prepare_kernel encrypted output, never a new crypto
    // strategy. This candidate is intentionally restricted to the pinned 6528-
    // byte prepared kernel layout (51 blocks). No security/session requests exist.
    Report run(UploadLink &link, const QByteArray &prepared,
               const KernelLayout &layout, uint16_t responseTimeout,
               const UploadCancellation &cancellation);
    static QByteArray kernelIdentityReply();
private:
    std::atomic<bool> used_{false};
};
}
#endif
