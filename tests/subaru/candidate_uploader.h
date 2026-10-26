#ifndef SUBARU_OFFLINE_CANDIDATE_UPLOADER_H
#define SUBARU_OFFLINE_CANDIDATE_UPLOADER_H
#ifndef FASTECU_OFFLINE_KERNEL_TESTS
#error "Candidate uploader is restricted to offline tests"
#endif
#include "experimental_kernel_transfer.h"
#include "../../modules/ecu/subaru_sh7058_safety.h"
#include <QVector>

// Independently written, software-only candidate. There is no transport
// interface to implement with hardware, no erase/write hook, and no production
// target includes this header. The selected reply profile is either the original
// synthetic fixture policy or the hash-bound, offline Task 006J firmware grammar.
namespace OfflineUploadCandidate {
using namespace OfflineKernelModel;
enum class Preflight { Accepted, InvalidImage, UnsupportedPreparation, InvalidChecksum,
                       MissingIdentity, WrongEcu, WrongRom };
struct IdentityReview {
    // Explicit fixture metadata, NOT identities extracted/authenticated from a BIN
    // or live ECU. Exact comparison is a candidate policy, not compatibility proof.
    QString expectedEcu, observedEcu, expectedRom, observedRom;
};
inline Preflight reviewImage(const QByteArray &image, const QString &mcu,
                             bool definition, bool checksum, const IdentityReview &ids) {
    if (!SubaruSH7058Safety::validImage(image, SubaruSH7058Safety::memoryMap(mcu)))
        return Preflight::InvalidImage;
    const auto prepared = SubaruSH7058Safety::prepare(image, mcu, definition, checksum);
    if (prepared.state == ChecksumEcuSubaruDensoSH7xxx::State::Unsupported)
        return Preflight::UnsupportedPreparation;
    // Reviewing a tuner-supplied image must not silently accept an unapplied
    // correction. Preparation/correction policy remains a separate decision.
    if (!prepared.usable() || prepared.image != image) return Preflight::InvalidChecksum;
    if (ids.expectedEcu.isEmpty() || ids.observedEcu.isEmpty() ||
        ids.expectedRom.isEmpty() || ids.observedRom.isEmpty()) return Preflight::MissingIdentity;
    if (ids.expectedEcu != ids.observedEcu) return Preflight::WrongEcu;
    if (ids.expectedRom != ids.observedRom) return Preflight::WrongRom;
    return Preflight::Accepted;
}

// A finite mock recording at the API boundary. Request matching is exact.
// Correlation identifiers are independently supplied by fixtures, not invented
// wire bytes or a claim that J2534 can correlate otherwise identical F6 replies.
struct Exchange {
    QByteArray request;
    CommunicationResult submission;
    unsigned long submittedCount = 1;
    CommunicationResult response;
    quint64 replyAttempt = 0, replyToken = 0;
    enum class CancelAt { Never, BeforeSubmission, AfterSubmission, AfterResponse };
    CancelAt cancellation = CancelAt::Never;
};
struct Attempt {
    enum class Result { BlockedPreflight, UploadFailed, ReadyForOfflineReview };
    Result result = Result::UploadFailed;
    Outcome upload = Outcome::Unsupported;
    Phase phase = Phase::Download;
    uint32_t acknowledgedBlocks = 0;
    QVector<QByteArray> submittedRequests; // attempted submissions, including failures
    int reads = 0;
    CommunicationResult transport;
    QString detail;
    // No transition to programming exists, even after synthetic upload success.
    bool programmingAllowed() const { return false; }
};

class Uploader {
public:
    explicit Uploader(quint64 fixtureAttempt) : attempt_(fixtureAttempt) {}
    Attempt run(Preflight preflight, const QByteArray &prepared,
                const SubaruSH7058::KernelLayout &layout, const QVector<Exchange> &script,
                const ReplyProfile *fixtureProfile = nullptr) {
        Attempt report;
        if (used_) { report.upload=Outcome::OutOfOrder; report.detail="Attempt cannot be reused"; return report; }
        used_=true;
        if (preflight != Preflight::Accepted) {
            report.result=Attempt::Result::BlockedPreflight;
            report.detail="Image/identity review failed before any upload request"; return report;
        }
        if (!attempt_) { report.detail="Missing fixture attempt identity"; return report; }
        // Historical BE24 encoding only covers the selected FFxxxxxx window.
        // KernelLayout itself is a general host address/size validator.
        if (!layout.valid() || layout.destination() >> 24 != 0xff || layout.preparedSize() > 0xffffff) {
            report.upload=Outcome::InvalidLayout; report.detail="Unsupported encoded address/length"; return report;
        }
        Transfer transfer(prepared, layout, fixtureProfile);
        int cursor=0;
        bool cancelledAfterResponse=false;
        Request request;
        while (transfer.takeRequest(request)) {
            if (cursor >= script.size()) {
                // No speculative submission if the mock has no matching exchange.
                report.detail="Recording ended before next request";
                transfer.submitted(request.fixtureToken,
                    CommunicationResult::failure(CommunicationResult::State::Timeout, report.detail),0);
                break;
            }
            const auto &exchange=script.at(cursor++);
            if (exchange.request != request.bytes) {
                report.detail="Request differs from complete expected bytes";
                transfer.submitted(request.fixtureToken,
                    CommunicationResult::failure(CommunicationResult::State::Malformed,report.detail),0);
                break;
            }
            if (exchange.cancellation == Exchange::CancelAt::BeforeSubmission) {
                transfer.cancel(); break;
            }
            report.submittedRequests.append(request.bytes);
            transfer.submitted(request.fixtureToken,exchange.submission,exchange.submittedCount);
            if (transfer.outcome() != Outcome::Running) break;
            if (exchange.cancellation == Exchange::CancelAt::AfterSubmission) {
                transfer.cancel(); break;
            }
            ++report.reads;
            if (exchange.replyAttempt != attempt_) {
                report.detail="Reply lacks matching fixture attempt provenance";
                // Do not accept a previous attempt's same-numbered F6 reply.
                // Preserve actual transport error first when one is present.
                transfer.received(0,exchange.response);
                break;
            }
            transfer.received(exchange.replyToken,exchange.response);
            if (exchange.cancellation == Exchange::CancelAt::AfterResponse) {
                transfer.cancel(); cancelledAfterResponse=true; break;
            }
            if (transfer.outcome() != Outcome::Running) break;
        }
        report.upload=cancelledAfterResponse ? Outcome::Cancelled : transfer.outcome(); report.phase=transfer.phase();
        report.acknowledgedBlocks=transfer.acknowledgedBlocks();
        report.transport=transfer.lastTransportResult();
        if (transfer.simulatedReady() && !cancelledAfterResponse) {
            if (cursor != script.size()) {
                report.upload=Outcome::OutOfOrder; report.detail="Unused/duplicate trailing exchange";
            } else report.result=Attempt::Result::ReadyForOfflineReview;
        }
        return report;
    }
private:
    quint64 attempt_;
    bool used_=false;
};
}
#endif
