#ifndef OFFLINE_EXPERIMENTAL_KERNEL_TRANSFER_H
#define OFFLINE_EXPERIMENTAL_KERNEL_TRANSFER_H
#ifndef FASTECU_OFFLINE_KERNEL_TESTS
#error "Experimental kernel model is restricted to the offline test target"
#endif
// TEST ONLY. No serial/J2534 implementation, callback or physical I/O exists
// here. No production target includes this file. This is not an ECU contract.
#include "../../modules/ecu/subaru_sh7058_kernel_layout.h"
#include "../../serial_port/communication_result.h"

namespace OfflineKernelModel {
enum class Phase { Download, Block, Exit, Start, Readiness };
enum class Outcome { Running, SimulatedReady, Unsupported, InvalidLayout,
    TransportError, Disconnected, PartialSubmission, Timeout, Rejected, Malformed,
    Unrelated, Uncorrelated, OutOfOrder, Cancelled };
struct Request {
    Phase phase = Phase::Download;
    // Fixture bookkeeping only. This token is NEVER added to wire bytes and
    // is NOT available as a correlation field in the known ECU/J2534 evidence.
    quint64 fixtureToken = 0;
    QByteArray bytes;
};

class SyntheticProfile {
public:
    // These exact lengths are deliberately synthetic, NOT demonstrated ECU
    // rules. F6 is only a reported positive prefix in the available evidence.
    static QByteArray body(Phase phase) {
        switch (phase) {
        case Phase::Download: return QByteArray::fromHex("7420");
        case Phase::Block: return QByteArray::fromHex("f6");
        case Phase::Exit: return QByteArray::fromHex("77");
        case Phase::Start: return QByteArray::fromHex("71010202");
        case Phase::Readiness: return QByteArray::fromHex("beef00024153");
        }
        return {};
    }
    static QByteArray reply(Phase phase) {
        return QByteArray::fromHex("000007e8") + body(phase);
    }
    Outcome validate(const Request &request, const QByteArray &replyBytes) const {
        if (request.bytes.size() < 5 || body(request.phase).isEmpty() ||
            replyBytes.size() < 5 || replyBytes.left(4) != QByteArray::fromHex("000007e8"))
            return Outcome::Malformed;
        const auto bytes = replyBytes.mid(4);
        if (uint8_t(bytes.at(0)) == 0x7f) {
            if (bytes.size() != 3) return Outcome::Malformed;
            if (bytes.at(1) != request.bytes.at(4)) return Outcome::Unrelated;
            // No pending/retry policy is established; never advance or wait.
            return uint8_t(bytes.at(2)) == 0x78 ? Outcome::Unsupported : Outcome::Rejected;
        }
        const auto expected = body(request.phase);
        if (bytes.size() != expected.size()) return Outcome::Malformed;
        return bytes == expected ? Outcome::Running : Outcome::Unrelated;
    }
};

class Transfer {
public:
    Transfer(const QByteArray &prepared, const SubaruSH7058::KernelLayout &layout,
             const SyntheticProfile *profile = nullptr)
        : prepared_(prepared.constData(), prepared.size()), layout_(layout),
          profile_(profile ? *profile : SyntheticProfile()) {
        if (!profile) stop(Outcome::Unsupported);
        else if (!layout.valid() || prepared.size() != int(layout.preparedSize()))
            stop(Outcome::InvalidLayout);
    }
    Outcome outcome() const { return outcome_; }
    Phase phase() const { return phase_; }
    bool simulatedReady() const { return outcome_ == Outcome::SimulatedReady; }
    uint32_t acknowledgedBlocks() const { return block_; }
    const CommunicationResult &lastTransportResult() const { return lastTransport_; }

    bool takeRequest(Request &out) {
        out = Request();
        if (state_ == State::Stopped) return false;
        if (state_ != State::RequestReady) { stop(Outcome::OutOfOrder); return false; }
        current_ = Request(); current_.phase = phase_; current_.fixtureToken = ++token_;
        current_.bytes = QByteArray::fromHex("000007e0");
        switch (phase_) {
        case Phase::Download:
            current_.bytes += QByteArray::fromHex("340433");
            append24(current_.bytes, layout_.destination());
            append24(current_.bytes, layout_.preparedSize());
            break;
        case Phase::Block: {
            SubaruSH7058::KernelLayout::Block block;
            if (!layout_.block(block_, block)) { stop(Outcome::InvalidLayout); return false; }
            current_.bytes += char(0xb6); append24(current_.bytes, block.address);
            current_.bytes += prepared_.mid(int(block.offset), int(block.length));
            break;
        }
        case Phase::Exit: current_.bytes += char(0x37); break;
        case Phase::Start: current_.bytes += QByteArray::fromHex("3101020202"); break;
        case Phase::Readiness: current_.bytes += QByteArray::fromHex("beef000101000000"); break;
        }
        out = current_; state_ = State::Submission;
        return true;
    }
    void submitted(quint64 fixtureToken, const CommunicationResult &result, unsigned long count) {
        if (state_ == State::Stopped) return;
        if (state_ != State::Submission || fixtureToken != current_.fixtureToken) {
            stop(Outcome::OutOfOrder); return;
        }
        snapshot(result);
        if (!result.ok()) { stop(transportFailure(result.state)); return; }
        if (count > 1) { stop(Outcome::Malformed); return; }
        if (count != 1) { stop(Outcome::PartialSubmission); return; }
        state_ = State::Reply;
    }
    void received(quint64 fixtureToken, const CommunicationResult &result) {
        if (state_ == State::Stopped) return;
        if (state_ != State::Reply) { stop(Outcome::OutOfOrder); return; }
        snapshot(result);
        if (!result.ok()) { stop(transportFailure(result.state)); return; }
        // Real uncorrelated F6 bytes do NOT authorize this simulated success.
        if (!fixtureToken) { stop(Outcome::Uncorrelated); return; }
        if (fixtureToken != current_.fixtureToken) { stop(Outcome::OutOfOrder); return; }
        const auto verdict = profile_.validate(current_, result.bytes);
        if (verdict != Outcome::Running) { stop(verdict); return; }
        switch (phase_) {
        case Phase::Download: phase_ = Phase::Block; break;
        case Phase::Block: if (++block_ == layout_.blockCount()) phase_ = Phase::Exit; break;
        case Phase::Exit: phase_ = Phase::Start; break;
        case Phase::Start: phase_ = Phase::Readiness; break;
        case Phase::Readiness: stop(Outcome::SimulatedReady); return;
        }
        state_ = State::RequestReady;
    }
    void cancel() { if (state_ != State::Stopped) stop(Outcome::Cancelled); }
private:
    enum class State { RequestReady, Submission, Reply, Stopped };
    static void append24(QByteArray &bytes, uint32_t value) {
        for (int shift = 16; shift >= 0; shift -= 8) bytes += char(value >> shift);
    }
    static Outcome transportFailure(CommunicationResult::State state) {
        using S = CommunicationResult::State;
        if (state == S::Cancelled) return Outcome::Cancelled;
        if (state == S::Timeout) return Outcome::Timeout;
        if (state == S::Malformed) return Outcome::Malformed;
        if (state == S::Unsupported) return Outcome::Unsupported;
        if (state == S::Disconnected) return Outcome::Disconnected;
        if (state == S::Rejected) return Outcome::Rejected;
        return Outcome::TransportError;
    }
    void stop(Outcome why) { outcome_ = why; state_ = State::Stopped; }
    void snapshot(const CommunicationResult &result) {
        lastTransport_ = result;
        // QByteArray::fromRawData does not own its backing memory.
        lastTransport_.bytes = QByteArray(result.bytes.constData(), result.bytes.size());
    }
    QByteArray prepared_;
    SubaruSH7058::KernelLayout layout_;
    SyntheticProfile profile_; // owns the stateless fixture policy; borrows no pointer
    State state_ = State::RequestReady;
    Outcome outcome_ = Outcome::Running;
    Phase phase_ = Phase::Download;
    uint32_t block_ = 0;
    quint64 token_ = 0;
    Request current_;
    CommunicationResult lastTransport_;
};
}
#endif
