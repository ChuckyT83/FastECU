#ifndef SH7058_CANDIDATE_LINK_H
#define SH7058_CANDIDATE_LINK_H
#include "../modules/ecu/subaru_sh7058_upload_candidate.h"
class SerialPortActions;
// The real facade binding is deliberately unable to authorize any upload.
// It preserves checked status semantics and the existing JSONL instrumentation.
// There is no live ownership setter, CLI flag, queue-flush shortcut or entry code.
class SH7058CandidateLink final : public SubaruSH7058::UploadLink {
public:
    explicit SH7058CandidateLink(SerialPortActions &serial) : serial_(serial) {}
    SubaruSH7058::StreamContext context() const noexcept override { return SubaruSH7058::StreamContext::Unreviewed; }
    quint64 generation() const noexcept override { return 0; }
    void retireContext() noexcept override {} // already permanently Unreviewed
    CommunicationResult submit(const QByteArray &bytes) override;
    CommunicationResult receive(uint16_t timeout) override;
private:
    SerialPortActions &serial_;
};
#endif
