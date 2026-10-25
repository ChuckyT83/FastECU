#ifndef SUBARU_TEST_SERIAL_H
#define SUBARU_TEST_SERIAL_H
// Link-time fake API. The production module is compiled unchanged against this
// header; no real facade, J2534 loader or serial backend is linked into this test.
#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>
class SerialPortActions {
public:
    QVector<QByteArray> writes;
    int setupCalls = 0;
    QByteArray pending;
    std::function<QByteArray(const QByteArray &)> respond;
    std::function<void(const QByteArray &)> afterRead;
    std::function<bool(const QByteArray &)> failWrite;
    QByteArray last;
    QByteArray write_serial_data_echo_check(QByteArray data) {
        writes.append(data); last = data;
        if (failWrite && failWrite(data)) return QByteArray(1, '\1');
        pending = respond ? respond(data) : QByteArray();
        return {};
    }
    QByteArray read_serial_data(uint16_t) {
        auto result = pending; pending.clear();
        if (!result.isEmpty() && afterRead) afterRead(last);
        return result;
    }
    bool reset_connection() { ++setupCalls; return true; }
    QString open_serial_port() { ++setupCalls; return "fake"; }
    bool is_serial_port_open() { return true; }
    bool get_is_iso15765_connection() { return true; }
    bool set_add_iso14230_header(bool) { ++setupCalls; return true; }
    bool set_is_iso14230_connection(bool) { ++setupCalls; return true; }
    bool set_is_can_connection(bool) { ++setupCalls; return true; }
    bool set_is_iso15765_connection(bool) { ++setupCalls; return true; }
    bool set_is_29_bit_id(bool) { ++setupCalls; return true; }
    bool set_can_speed(QString) { ++setupCalls; return true; }
    bool set_iso15765_source_address(uint32_t) { ++setupCalls; return true; }
    bool set_iso15765_destination_address(uint32_t) { ++setupCalls; return true; }
    bool set_can_source_address(uint32_t) { ++setupCalls; return true; }
    bool set_can_destination_address(uint32_t) { ++setupCalls; return true; }
};
#endif
