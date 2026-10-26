#ifndef FASTECU_J2534_TRACE_MESSAGE_H
#define FASTECU_J2534_TRACE_MESSAGE_H
#include <QJsonObject>
#include <QByteArray>
#include <cstdint>
// Include after the platform's J2534 definitions. These are driver API messages,
// never a claim to capture raw CAN/ISO-TP frames underneath ISO15765.
inline bool j2534SensitiveIso15765(const QByteArray &data) {
    if(data.size()<5) return false;
    const auto sid=uint8_t(data[4]);
    return sid==0x27 || sid==0x67 ||
        (data.size()>=6 && (sid==0x09 || sid==0x49) && uint8_t(data[5])==2);
}
inline QJsonObject j2534TraceMessage(const PASSTHRU_MSG *msg, const char *direction) {
    QJsonObject out{{"direction",direction}};
    if(!msg) return out;
    out["protocol_id"]=qint64(msg->ProtocolID); out["length"]=qint64(msg->DataSize);
    out["rx_status"]=qint64(msg->RxStatus); out["tx_flags"]=qint64(msg->TxFlags);
    out["driver_timestamp"]=qint64(msg->Timestamp); out["extra_data_index"]=qint64(msg->ExtraDataIndex);
    if(msg->DataSize>PASSTHRU_MSG_DATA_SIZE) { out["capture_error"]="driver_length_exceeds_buffer";return out; }
    const QByteArray data(reinterpret_cast<const char*>(msg->Data),int(msg->DataSize));
    const bool can=msg->ProtocolID==CAN || msg->ProtocolID==ISO15765;
    if(can && data.size()>=4) out["can_id_prefix_hex"]=QString::fromLatin1(data.left(4).toHex());
    // Only ISO15765 is interpreted as a reassembled application payload. Raw CAN
    // framing is deliberately not guessed. This is a privacy filter, not a parser.
    const bool sensitive=msg->ProtocolID==ISO15765 && j2534SensitiveIso15765(data);
    out["representation"]=msg->ProtocolID==ISO15765 ? "j2534_iso15765_message" : "j2534_message";
    out["redacted"]=sensitive;
    if(sensitive) out["redaction_reason"]="security_access_or_vin";
    else {
        out["data_hex"]=QString::fromLatin1(data.toHex());
        if(msg->ProtocolID==ISO15765 && data.size()>=4) {
            const auto extra=msg->ExtraDataIndex;
            if(extra && (extra<4 || extra>msg->DataSize)) out["capture_error"]="invalid_extra_data_index";
            else {
                const int end=extra?int(extra):data.size();
                out["application_hex"]=QString::fromLatin1(data.mid(4,end-4).toHex());
                if(extra)out["extra_data_hex"]=QString::fromLatin1(data.mid(end).toHex());
            }
        }
    }
    return out;
}
#endif
