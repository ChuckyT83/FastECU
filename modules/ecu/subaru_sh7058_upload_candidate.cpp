#include "subaru_sh7058_upload_candidate.h"

namespace SubaruSH7058 {
namespace {
QByteArray request(const QByteArray &application) { return QByteArray::fromHex("000007e0") + application; }
void append24(QByteArray &bytes, uint32_t value) {
    for (int shift=16;shift>=0;shift-=8) bytes += char(value>>shift);
}
}
QByteArray UploadCandidate::kernelIdentityReply() {
    // Exact bundled kernel 5fb9cb89... (Task 006I), FFFF3B0E/FFFF48AC.
    // This ID is not authenticated binary identity or flash qualification.
    return QByteArray::fromHex("000007e8beef002d41") +
        QByteArray("FastECU Subaru SH7058 iso15765 Kernel v1.00",43) + char(0);
}
UploadCandidate::Report UploadCandidate::run(UploadLink &link, const QByteArray &input,
        const KernelLayout &layout, uint16_t timeout, const UploadCancellation &cancel) {
    Report report;
    if (used_.exchange(true)) { report.outcome=Outcome::Reused; return report; }
    auto cancelled=[&]() {
        if (!cancel.requested.load()) return false;
        report.outcome=Outcome::Cancelled;
        report.detail="Cancelled; submitted commands may still execute. No recovery attempted.";
        return true;
    };
    if (cancelled()) return report;
    if (link.context()!=StreamContext::ControlledOfflineLoader) {
        report.detail="Loader entry and live acknowledgement ownership are unreviewed; no request submitted.";
        return report;
    }
    if (!layout.valid() || layout.destination()!=0xffff3000u || layout.preparedSize()!=6528 ||
        layout.blockCount()!=51 || input.size()!=6528 || !timeout) {
        report.outcome=Outcome::InvalidImage;
        report.detail="Requires the pinned 6528-byte/51-block prepared layout and a nonzero configured timeout.";
        return report;
    }
    // Own the bytes even if the caller supplied QByteArray::fromRawData.
    const QByteArray prepared(input.constData(),input.size());
    auto receive=[&](uint16_t wait) {
        try { return link.receive(wait); }
        catch (...) { return CommunicationResult::failure(CommunicationResult::State::TransportError,
            "Receive callback failed; ownership cannot be established."); }
    };
    auto submit=[&](const QByteArray &bytes) {
        try { return link.submit(bytes); }
        catch (...) { return CommunicationResult::failure(CommunicationResult::State::TransportError,
            "Submission callback failed; ECU acceptance is uncertain."); }
    };
    auto ownership=[&]() {
        if (link.context()==StreamContext::ControlledOfflineLoader) return true;
        report.outcome=Outcome::OwnershipUncertain;
        report.detail="Stream context lost; an uncorrelated reply cannot establish ownership.";
        return false;
    };
    auto exchange=[&](const QByteArray &application, const QByteArray &positive) {
        if (cancelled() || !ownership()) return false;
        // Reject already queued messages; never pretend an empty queue proves
        // that an old in-flight F6 cannot arrive later. Live context is blocked.
        auto queued=receive(0);
        report.transport=queued;
        if (cancelled() || !ownership()) return false;
        if (queued.state!=CommunicationResult::State::Timeout) {
            report.outcome=queued.ok()?Outcome::OwnershipUncertain:Outcome::TransportFailure;
            report.detail="Unexpected queued data or queue inspection failure before submission.";
            return false;
        }
        if (cancelled()) return false;
        ++report.submissions;
        if (report.phase==Phase::Start) report.startup=Startup::Submitted;
        report.commandMayHaveExecuted=true;
        report.transport=submit(request(application));
        if (cancelled()) return false;
        if (!report.transport.ok()) {
            report.outcome=Outcome::SubmissionUncertain;
            report.detail="Submission failed/uncertain; no retransmission or following command.";
            return false;
        }
        if (!ownership()) return false;
        report.transport=receive(timeout);
        if (cancelled() || !ownership()) return false;
        if (!report.transport.ok()) {
            using State=CommunicationResult::State;
            switch (report.transport.state) {
            case State::Timeout: report.outcome=Outcome::Timeout; break;
            case State::Malformed: report.outcome=Outcome::Malformed; break;
            case State::Cancelled: report.outcome=Outcome::Cancelled; break;
            case State::Unsupported: report.outcome=Outcome::Unsupported; break;
            default: report.outcome=Outcome::TransportFailure; break;
            }
            report.detail="Required application acknowledgement unavailable; no retransmission attempted.";
            return false;
        }
        const auto &bytes=report.transport.bytes;
        if (bytes.size()<5 || bytes.left(4)!=QByteArray::fromHex("000007e8")) {
            report.outcome=Outcome::Malformed; return false;
        }
        const auto body=bytes.mid(4);
        if (uint8_t(body.at(0))==0x7f) {
            if (body.size()!=3) report.outcome=Outcome::Malformed;
            else if (body.at(1)!=application.at(0)) report.outcome=Outcome::Unrelated;
            else {
                const auto nrc=uint8_t(body.at(2));
                report.outcome=(nrc==0x12 || nrc==0x13 || nrc==0x22) ? Outcome::Rejected : Outcome::Unsupported;
            }
            return false;
        }
        if (body.size()!=positive.size()) { report.outcome=Outcome::Malformed; return false; }
        if (body!=positive) { report.outcome=Outcome::Unrelated; return false; }
        return true;
    };
    // Base.bin 15102939..., 1B3C/1FA0: fixed base and 8-KiB normal window.
    report.phase=Phase::Download;
    QByteArray download=QByteArray::fromHex("340433");
    append24(download,layout.destination()); append24(download,layout.preparedSize());
    if (!exchange(download,QByteArray::fromHex("74200084"))) return report;
    report.phase=Phase::Block;
    for (uint32_t index=0;index<layout.blockCount();++index) {
        KernelLayout::Block block;
        if (!layout.block(index,block) || block.length!=128 || block.offset!=index*128 ||
            block.address!=0xffff3000u+index*128 || block.offset+block.length>uint32_t(prepared.size())) {
            report.outcome=Outcome::InvalidImage; return report;
        }
        QByteArray bytes(1,char(0xb6)); append24(bytes,block.address);
        bytes+=prepared.mid(int(block.offset),128);
        // 1C6E/1D30: F6, no address/sequence suffix or last-block variant.
        if (!exchange(bytes,QByteArray::fromHex("f6"))) return report;
        ++report.acknowledgedBlocks;
    }
    report.phase=Phase::Exit;
    if (!exchange(QByteArray::fromHex("37"),QByteArray::fromHex("77"))) return report;
    report.phase=Phase::Start;
    const bool startAccepted=exchange(QByteArray::fromHex("3101020202"),QByteArray::fromHex("71010202"));
    if (report.startup==Startup::Submitted) report.startupTransport=report.transport;
    if (!startAccepted) {
        if (report.outcome!=Outcome::Timeout) {
            if (report.outcome==Outcome::Rejected) report.startup=Startup::Failed;
            else if (report.startup==Startup::Submitted) report.startup=Startup::Uncertain;
            return report;
        }
        // Only the explicitly controlled offline context reaches this path.
        // No live missing-71 recovery policy is authorized by this candidate.
        report.startup=Startup::Uncertain;
    } else { report.startup=Startup::Acknowledged; report.startupAcknowledged=true; }
    const bool uncertain=!report.startupAcknowledged;
    report.phase=Phase::Readiness;
    if (!exchange(QByteArray::fromHex("beef000101000000"),kernelIdentityReply().mid(4))) {
        if (report.outcome==Outcome::Timeout) report.outcome=Outcome::StartupUncertain;
        return report;
    }
    report.kernelResponding=true; report.startup=Startup::KernelResponding;
    report.outcome=uncertain?Outcome::ReadyAfterUncertainStartReviewRequired:Outcome::ReadyForOfflineReview;
    report.detail=uncertain ? "Kernel ID received after missing startup acknowledgement; review required, programming disabled." :
        "Kernel ID received in controlled offline stream; programming disabled.";
    return report;
}
}
