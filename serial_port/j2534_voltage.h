#ifndef FASTECU_J2534_VOLTAGE_H
#define FASTECU_J2534_VOLTAGE_H
#include <QElapsedTimer>
#include <QUuid>
#include <QString>
#include <QtGlobal>

// Host observation, not a calibrated measurement or programming authorization.
// Timestamps are monotonic, process-local and must never be persisted/reused as
// freshness proof after a restart. Driver sensor-cache age/accuracy are unknown.
struct J2534VoltageSample {
    enum class State { Unavailable, Unsupported, Disconnected, DriverError, InvalidValue, Observed };
    enum class Assessment { NoValue, WrongContext, Stale, OutsideReviewedRange, CurrentUnqualified };
    State state=State::Unavailable;
    quint64 millivolts=0;
    qint64 observedAtMs=-1,returnedAtMs=-1;
    QUuid context;
    unsigned long deviceId=0;
    bool hasStatus=false;
    long status=0;
    QString detail;
    static qint64 monotonicMs() {QElapsedTimer t;t.start();return t.msecsSinceReference();}
    Assessment assess(qint64 now,qint64 maxAge,const QUuid &currentContext,
                      quint64 reviewedMinimum,quint64 reviewedMaximum) const {
        if(state!=State::Observed) return Assessment::NoValue;
        if(context.isNull() || context!=currentContext) return Assessment::WrongContext;
        if(observedAtMs<0 || returnedAtMs<observedAtMs || now<returnedAtMs || maxAge<=0 ||
            now-observedAtMs>maxAge) return Assessment::Stale;
        if(reviewedMinimum>reviewedMaximum || millivolts<reviewedMinimum || millivolts>reviewedMaximum)
            return Assessment::OutsideReviewedRange;
        return Assessment::CurrentUnqualified;
    }
    // No default electrical limits, freshness policy, calibration or permission.
    bool programmingAllowed() const {return false;}
};
#endif
