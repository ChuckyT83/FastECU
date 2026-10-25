#ifndef COMMUNICATION_RESULT_H
#define COMMUNICATION_RESULT_H
#include <QByteArray>
#include <QString>

// Additive contract: submission success is not an ECU acknowledgement.
struct CommunicationResult {
    enum class State { Success, Timeout, TransportError, Disconnected, Malformed, Cancelled, Unsupported, Rejected };
    State state = State::Unsupported;
    QByteArray bytes;
    long status = 0;
    bool hasStatus = false; // actual J2534 return, not a synthesized local error
    QString detail;
    bool ok() const { return state == State::Success; }
    static CommunicationResult failure(State state, const QString &why) {
        CommunicationResult r; r.state=state; r.detail=why; return r;
    }
};
#endif
