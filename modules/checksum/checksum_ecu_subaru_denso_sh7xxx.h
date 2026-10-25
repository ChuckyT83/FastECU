#ifndef CHECKSUM_ECU_SUBARU_DENSO_SH7XXX_H
#define CHECKSUM_ECU_SUBARU_DENSO_SH7XXX_H

#include <QDebug>
#include <QMessageBox>

class ChecksumEcuSubaruDensoSH7xxx
{
public:
    ChecksumEcuSubaruDensoSH7xxx();
    ~ChecksumEcuSubaruDensoSH7xxx();

    enum class State { Disabled, Valid, Corrected, Invalid, Unsupported };
    struct Result {
        State state;
        QByteArray image;
        bool usable() const { return state == State::Disabled || state == State::Valid || state == State::Corrected; }
    };
    // No dialogs; invalid configuration never returns a usable image.
    static Result evaluate(QByteArray romData, uint32_t start, uint32_t length, int32_t offset = 0);

    //Note that offset is added to all addresses
    static QByteArray calculate_checksum(QByteArray romData, uint32_t checksum_area_start, uint32_t checksum_area_length, int32_t offset = 0);

private:

};

#endif // CHECKSUM_ECU_SUBARU_DENSO_SH7XXX_H
