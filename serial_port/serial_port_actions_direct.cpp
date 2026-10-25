// This is an open source non-commercial project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com

#include "serial_port_actions_direct.h"
#include <QFileInfo>
#include <QDir>
#include <vector>

SerialPortActionsDirect::SerialPortActionsDirect(QObject *parent)
    : QObject(parent)
    , serial(new QSerialPort(this))
{
    j2534 = new J2534();
#if defined Q_OS_UNIX
    QObject::connect(j2534, &J2534::LOG_E, this, &SerialPortActionsDirect::LOG_E);
    QObject::connect(j2534, &J2534::LOG_W, this, &SerialPortActionsDirect::LOG_W);
    QObject::connect(j2534, &J2534::LOG_I, this, &SerialPortActionsDirect::LOG_I);
    QObject::connect(j2534, &J2534::LOG_D, this, &SerialPortActionsDirect::LOG_D);
#endif
}

SerialPortActionsDirect::~SerialPortActionsDirect()
{
    close_j2534_serial_port();
    delete j2534;
    delete serial;
}

bool SerialPortActionsDirect::is_serial_port_open()
{
    if (!serial->isOpen()){
        if (!J2534_init_ok)
            return false;
        else
            return J2534_open_ok && J2534_connect_ok;
    }

    return serial->isOpen();
}

bool SerialPortActionsDirect::set_kline_timings(uint32_t parameter, int value)
{
    _P1_MAX = value;
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::change_port_speed(QString portSpeed)
{
    serial_port_baudrate = portSpeed;
    baudrate = portSpeed.toInt();

    emit LOG_D("Changing baudrate, checking if port is open...", true, true);
    if (is_serial_port_open())
    {
        emit LOG_D("Port is open, checking adapter type...", true, true);
        if (!use_openport2_adapter)
        {
            emit LOG_D("Adapter type is generic OBD2...", true, true);

            if (serial->setBaudRate(serial_port_baudrate.toDouble()))
            {
                delay(50);
                emit LOG_D("Baudrate set to " + portSpeed + " OK", true, true);
                return STATUS_SUCCESS;
            }
            else
            {
                emit LOG_E("ERROR setting baudrate!", true, true);
                return STATUS_ERROR;
            }
        }
        else
        {
            emit LOG_D("Adapter type is J2534...", true, true);

            SCONFIG_LIST scl;
            SCONFIG scp[1] = {{DATA_RATE,0}};
            scl.NumOfParams = 1;
            scp[0].Value = baudrate;
            scl.ConfigPtr = scp;
            const long status = j2534->PassThruIoctl(chanID,SET_CONFIG,&scl,NULL);
            if (status == STATUS_NOERROR)
            {
                emit LOG_D("Baudrate set to " + portSpeed + " OK", true, true);
                delay(50);
                return STATUS_SUCCESS;
            }
            else
            {
                return transport_failure(status, "SET_CONFIG baudrate");
            }
        }
    }

    return STATUS_ERROR;
}

QByteArray SerialPortActionsDirect::five_baud_init(QByteArray output)
{
    QByteArray response;

    if (use_openport2_adapter)
    {
        if (!J2534_connect_ok)
            return {};
        if (output.size() != 1)
        {
            transport_failure(ERR_INVALID_MSG, "FIVE_BAUD_INIT (requires one address)", false);
            return {};
        }
        unsigned char input[1] = {static_cast<unsigned char>(output.at(0))};
        unsigned char responseBytes[20] = {};
        SBYTE_ARRAY inputMsg = {1, input};
        SBYTE_ARRAY outputMsg = {sizeof(responseBytes), responseBytes};
        const long status = j2534->PassThruIoctl(chanID, FIVE_BAUD_INIT, &inputMsg, &outputMsg);
        if (status != STATUS_NOERROR)
        {
            transport_failure(status, "FIVE_BAUD_INIT");
            return {};
        }
        if (outputMsg.BytePtr != responseBytes || outputMsg.NumOfBytes > sizeof(responseBytes))
        {
            transport_failure(ERR_INVALID_MSG, "FIVE_BAUD_INIT (output bounds)", false);
            return {};
        }
        response = QByteArray(reinterpret_cast<const char *>(responseBytes), int(outputMsg.NumOfBytes));
    }
    else
    {
        // Set timeout to 350ms before init
        accurate_delay(350);
        // Set break to set seril line low
        serial->setBreakEnabled(true);
        // Set timeout to 200ms to generate 200ms low pulse
        accurate_delay(200);
        // Unset break to set seril line high
        serial->setBreakEnabled(false);
        // Set timeout to 400ms to generate 400ms high pulse
        accurate_delay(400);
        serial->setBreakEnabled(true);
        accurate_delay(400);
        serial->setBreakEnabled(false);
        accurate_delay(400);
        serial->setBreakEnabled(true);
        accurate_delay(400);
        serial->setBreakEnabled(false);
        accurate_delay(400);
        // Set timeout to 400ms to generate 400ms high pulse before init data is sent

        // Send init data
        write_serial_data_echo_check(output);
        response = read_serial_obd_data(40);
        //emit LOG_D("Read response", true, true);
        response = read_serial_obd_data(200);
        //emit LOG_D("Five baud init response: " + parse_message_to_hex(response), true, true);
        if (response.size() >= 3 && (uint8_t)response.at(1) == 0x08 && (uint8_t)response.at(2) == 0x08)
        {
            //delay(30);
            output.clear();
            output.append(~((uint8_t)response.at(2)));
            write_serial_data_echo_check(output);
            response.append(read_serial_obd_data(200));
        }
    }

    return response;
}

int SerialPortActionsDirect::fast_init(QByteArray output)
{
    if (use_openport2_adapter && !J2534_connect_ok)
        return STATUS_ERROR;
    QByteArray received;

    if (use_openport2_adapter)
    {
        unsigned long result;
        PASSTHRU_MSG InputMsg;
        PASSTHRU_MSG OutputMsg;

        memset(&InputMsg, 0, sizeof(InputMsg));
        memset(&OutputMsg, 0, sizeof(OutputMsg));

        InputMsg.ProtocolID = ISO14230;
        InputMsg.TxFlags = 0;

        if (add_ssm_header)
            output = append_ssm_header(output);
        else if (add_iso9141_header)
            output = append_iso9141_header(output);
        else if (add_iso14230_header)
            output = append_iso14230_header(output);

        if (output.isEmpty() || output.size() > PASSTHRU_MSG_DATA_SIZE)
            return transport_failure(ERR_INVALID_MSG, "FAST_INIT (length)", false);
        for (int i = 0; i < output.length(); i++)
        {
            InputMsg.Data[i] = (uint8_t)output.at(i);
        }
        InputMsg.DataSize = output.length();

        /* Set timeout to 350ms before init */
        accurate_delay(350);

        result = j2534->PassThruIoctl(chanID, FAST_INIT, &InputMsg, &OutputMsg);
        if (result)
        {
            reportJ2534Error(result, "FAST_INIT");
            return STATUS_ERROR;
        }
    }
    else
    {
        // Set timeout to 350ms before init
        accurate_delay(350);
        // Set break to set seril line low
        serial->setBreakEnabled(true);
        // Set timeout to 25ms to generate 25ms low pulse
        accurate_delay(23.7);
        // Unset break to set seril line high
        serial->setBreakEnabled(false);
        // Set timeout to 25ms to generate 25ms high pulse before init data is sent
        accurate_delay(23.8);
        // Send init data
        write_serial_data_echo_check(output);
        received = read_serial_data(10);
        //emit LOG_D("Fast init response: " + parse_message_to_hex(received), true, true);
        delay(100);
    }

    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::clear_rx_buffer()
{
    if (!J2534_open_ok || !J2534_connect_ok)
        return transport_failure(ERR_INVALID_CHANNEL_ID, "CLEAR_RX_BUFFER (disconnected)", false);
    const long status = j2534->PassThruIoctl(chanID, CLEAR_RX_BUFFER, NULL, NULL);
    if (status != STATUS_NOERROR)
        return transport_failure(status, "CLEAR_RX_BUFFER");
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::clear_tx_buffer()
{
    if (!J2534_open_ok || !J2534_connect_ok)
        return transport_failure(ERR_INVALID_CHANNEL_ID, "CLEAR_TX_BUFFER (disconnected)", false);
    const long status = j2534->PassThruIoctl(chanID, CLEAR_TX_BUFFER, NULL, NULL);
    if (status != STATUS_NOERROR)
        return transport_failure(status, "CLEAR_TX_BUFFER");
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::set_lec_lines(int lec1_state, int lec2_state)
{
    const int first = line_end_check_1_toggled(lec1_state);
    const int second = line_end_check_2_toggled(lec2_state);
    return first == STATUS_SUCCESS && second == STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_ERROR;
}

int SerialPortActionsDirect::pulse_lec_1_line(int timeout)
{
    const int on = line_end_check_1_toggled(requestToSendEnabled);
    accurate_delay(timeout);
    const int off = line_end_check_1_toggled(requestToSendDisabled);
    return on == STATUS_SUCCESS && off == STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_ERROR;
}

int SerialPortActionsDirect::pulse_lec_2_line(int timeout)
{
    const int on = line_end_check_2_toggled(dataTerminalEnabled);
    accurate_delay(timeout);
    const int off = line_end_check_2_toggled(dataTerminalDisabled);
    return on == STATUS_SUCCESS && off == STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_ERROR;
}

int SerialPortActionsDirect::line_end_check_1_toggled(int state)
{
    if (use_openport2_adapter && !J2534_open_ok)
        return STATUS_ERROR;
    if (state == requestToSendEnabled)
    {
        if (use_openport2_adapter)
        {
            const long status = j2534->PassThruSetProgrammingVoltage(devID, J1962_PIN_11, 12000);
#if defined Q_OS_UNIX
            delay(1);
#endif
            if (!checkJ2534Status(status, "PassThruSetProgrammingVoltage"))
                return STATUS_ERROR;
        }
        else
        {
            serial->setRequestToSend(requestToSendEnabled);
            setRequestToSend = false;
        }
    }
    else
    {
        if (use_openport2_adapter)
        {
            if (!checkJ2534Status(j2534->PassThruSetProgrammingVoltage(devID, J1962_PIN_11, -2), "PassThruSetProgrammingVoltage"))
                return STATUS_ERROR;
        }
        else
        {
            serial->setRequestToSend(requestToSendDisabled);
            setRequestToSend = true;
        }
    }

    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::line_end_check_2_toggled(int state)
{
    if (use_openport2_adapter && !J2534_open_ok)
        return STATUS_ERROR;
    if (state == dataTerminalEnabled)
    {
        if (use_openport2_adapter)
        {
            const long status = j2534->PassThruSetProgrammingVoltage(devID, J1962_PIN_9, 12000);
#if defined Q_OS_UNIX
            delay(1);
#endif
            if (!checkJ2534Status(status, "PassThruSetProgrammingVoltage"))
                return STATUS_ERROR;
        }
        else
        {
            serial->setDataTerminalReady(dataTerminalEnabled);
            setDataTerminalReady = false;
        }
    }
    else
    {
        if (use_openport2_adapter)
        {
            if (!checkJ2534Status(j2534->PassThruSetProgrammingVoltage(devID, J1962_PIN_9, -2), "PassThruSetProgrammingVoltage"))
                return STATUS_ERROR;
        }
        else
        {
            serial->setDataTerminalReady(dataTerminalDisabled);
            setDataTerminalReady = true;
        }
    }

    return STATUS_SUCCESS;
}

QStringList SerialPortActionsDirect::check_serial_ports()
{
    const auto serialPortsInfo = QSerialPortInfo::availablePorts();
    QStringList serial_ports;
    //QString j2534DllName = "j2534.dll";

    serialPortAvailable = false;

    for (const QSerialPortInfo &serialPortInfo : serialPortsInfo){
        serial_ports.append(serialPortInfo.portName() + " - " + serialPortInfo.description());
        emit LOG_D("Serial port name: " + serialPortInfo.portName() + " " + serialPortInfo.description(), true, true);
    }
    std::sort(serial_ports.begin(), serial_ports.end(), std::less<QString>());

#if defined(_WIN32) || defined(WIN32) || defined (_WIN64) || defined (WIN64)
    QStringList j2534_interfaces;
    installed_drivers = getAllJ2534DriversNames();
    for (const QString installed_vendor : installed_drivers.keys())
    {
        j2534_interfaces.append(installed_vendor);
    }
    std::sort(j2534_interfaces.begin(), j2534_interfaces.end(), std::less<QString>());
    serial_ports.append(j2534_interfaces);
#endif

    return serial_ports;
}

#if defined(_WIN32) || defined(WIN32) || defined (_WIN64) || defined (WIN64)
QSettings::Format SerialPortActionsDirect::driver_registry_format()
{
#if defined(_WIN64) || defined(WIN64)
    return QSettings::Registry64Format;
#else
    return QSettings::Registry32Format;
#endif
}

//Find first connected device
//TODO find all devices
QStringList SerialPortActionsDirect::check_j2534_devices(QMap<QString, QString> installed_drivers)
{
    close_j2534_serial_port();
    bool j2534DeviceFound = false;
    QStringList j2534_devices;
    int driver_count = 0;
    for (const QString &vendor : installed_drivers.keys())
    {
        driver_count++;
        j2534->disable();
        //close_j2534_serial_port();
        QString j2534DllName = installed_drivers[vendor];
        emit LOG_D("Testing for " + j2534DllName, true, true);
        j2534->setDllName(j2534DllName.toLocal8Bit().data());
        if (j2534->init())
        {
            emit LOG_D(j2534DllName + " init successfull", true, true);
            //0 means no error
            unsigned long probeDeviceID = 0;
            if (!j2534->PassThruOpen(NULL, &probeDeviceID))
            {
                emit LOG_D("Successfully opened " + QString::number(probeDeviceID) + " / " + vendor + " / " + j2534DllName, true, true);
                j2534_devices.append(vendor);
                j2534DeviceFound = true;
                checkJ2534Status(j2534->PassThruClose(probeDeviceID), "PassThruClose probe");
            }
            else
                emit LOG_E(QString::number(probeDeviceID) + " / " + vendor + " device not connected", true, true);
        }
        else
            emit LOG_D(j2534DllName + " not found", true, true);
        j2534->disable();
        if (j2534DeviceFound)
            break;
    }
    emit LOG_D("Tested installed drivers: " + QString::number(driver_count), true, true);

    return j2534_devices;
}

QMap<QString, QString> SerialPortActionsDirect::getAllJ2534DriversNames()
{
    QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\PassThruSupport.04.04", driver_registry_format());
    return read_j2534_registry(registry);
}

QMap<QString, QString> SerialPortActionsDirect::read_j2534_registry(QSettings &registry)
{
    QMap<QString, QString> drivers;
    for (const QString &vendor : registry.childGroups())
    {
        const QVariant value = registry.value(vendor + "/FunctionLibrary");
        QString path = value.type() == QVariant::String ? value.toString().trimmed() : QString();
        if (path.startsWith('"') && path.endsWith('"') && path.size() >= 2)
            path = path.mid(1, path.size() - 2);
        if (!path.isEmpty() && !path.contains(QChar('\0')))
        {
            const std::wstring native = path.toStdWString();
            const DWORD count = ExpandEnvironmentStringsW(native.c_str(), nullptr, 0);
            if (count && count < 32768)
            {
                std::vector<wchar_t> expanded(count);
                if (ExpandEnvironmentStringsW(native.c_str(), expanded.data(), count) == count)
                    path = QString::fromWCharArray(expanded.data());
            }
        }
        if (path.isEmpty() || path.contains(QChar('\0')) || !QDir::isAbsolutePath(path) || !QFileInfo(path).isFile())
        {
            emit LOG_W("Ignoring malformed or missing J2534 FunctionLibrary for " + vendor, true, true);
            continue;
        }
        drivers[vendor] = path;
    }
    if (registry.status() != QSettings::NoError)
        emit LOG_E("J2534 registry read failed (status " + QString::number(registry.status()) + ")", true, true);
    return drivers;
}
#endif

QString SerialPortActionsDirect::open_serial_port()
{
    if (serial_port_list.isEmpty())
        return {};
    emit LOG_D("Serial port = " + serial_port_list.join(", "), true, true);
    //QString serial_port_text = serial_port_list.at(1);
#if defined Q_OS_UNIX
    serial_port = serial_port_prefix_linux + serial_port_list.at(0);
    QString is_j2534 = serial_port.section(" - ", 1, 1);
    serial_port = serial_port.split(" - ").at(0);
#endif
#if defined(_WIN32) || defined(WIN32) || defined (_WIN64) || defined (WIN64)
    serial_port = serial_port_prefix_win + serial_port_list.at(0);
#endif
    emit LOG_D("Interface: " + serial_port, true, true);

#if defined Q_OS_UNIX
    if (!serial_port.isEmpty() && is_j2534 == "OpenPort 2.0")
#else
    if (!serial_port.isEmpty() && installed_drivers.contains(serial_port))
#endif
    {
        reset_connection();
        //close_serial_port();
        //use_openport2_adapter = true;

        J2534_is_denso_dsti = serial_port.contains("DST-i");

        //QMap<QString, QString> user_j2534_drivers; // Local drivers in software folder
#if defined(_WIN32) || defined(WIN32) || defined (_WIN64) || defined (WIN64)
        // The registry path is authoritative. Do not substitute a CWD DLL.
        const QString installedDllName = installed_drivers.value(serial_port);
        j2534->setDllName(installedDllName.toLocal8Bit().constData());
#endif
        long result;

        emit LOG_D("Testing j2534 interface, please wait...", true, true);
        result = init_j2534_connection();

        if (result == STATUS_SUCCESS)
        {
            emit LOG_D("J2534: Interface opened succesfully!", true, true);
            use_openport2_adapter = true;
            J2534_init_ok = true;
            j2534->J2534_init_ok = true;
            openedSerialPort = serial_port;
        }
        else
        {
            emit LOG_D("J2534: Failed to open interface!", true, true);
            use_openport2_adapter = false;
            reset_connection();
            return {};
        }
    }
    if (!use_openport2_adapter && openedSerialPort != serial_port)
    {
        emit LOG_D("Testing serial interface, please wait...", true, true);
        //close_serial_port();

        serial_port = serial_port.split(" - ").at(0);
#if defined Q_OS_UNIX
            //serial_port = serial_port_prefix_linux + serial_port;
#elif defined Q_OS_WIN32
            //serial_port = serial_port_prefix_win + serial_port;
#endif

        if (!(serial->isOpen() && serial->isWritable())){
            serial->setPortName(serial_port);
            serial->setBaudRate(serial_port_baudrate.toDouble());
            serial->setDataBits(QSerialPort::Data8);
            serial->setStopBits(QSerialPort::OneStop);
            //serial->setParity(QSerialPort::EvenParity);
            serial->setParity((QSerialPort::Parity)serial_port_parity);
            serial->setFlowControl(QSerialPort::NoFlowControl);

            if (serial->open(QIODevice::ReadWrite)){
                //serial->setDataTerminalReady(setDataTerminalReady);
                //serial->setRequestToSend(setRequestToSend);
                serial->clearError();
                serial->clear();
                serial->flush();
                openedSerialPort = serial_port;
                //connect(serial, SIGNAL(readyRead()), this, SLOT(ReadSerialDataSlot()), Qt::DirectConnection);
                qRegisterMetaType<QSerialPort::SerialPortError>();
                connect(serial, SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(handle_error(QSerialPort::SerialPortError)));

                emit LOG_D("Serial port '" + serial_port + "' is open at baudrate " + serial_port_baudrate, true, true);
                return openedSerialPort;
            }
            else
            {
                emit LOG_E("Couldn't open serial port '" + serial_port + "'", true, true);
                return NULL;
            }

        }
        else{
            emit LOG_D("Serial port '" + serial_port + "' is already opened", true, true);
            return openedSerialPort;
        }
    }

    return openedSerialPort;
}

void SerialPortActionsDirect::reset_connection()
{
    close_j2534_serial_port();
    close_serial_port();
    delay(250);
}

void SerialPortActionsDirect::close_serial_port()
{
    if (serial->isOpen())
    {
        serial->close();
        openedSerialPort.clear();
    }
}

void SerialPortActionsDirect::close_j2534_serial_port()
{
    // Resource ownership is independent of whether initialization completed.
    // Close each acquired resource once, in reverse order, even after an error.
    if (J2534_connect_ok)
        checkJ2534Status(j2534->PassThruDisconnect(chanID), "PassThruDisconnect");
    if (J2534_open_ok)
        checkJ2534Status(j2534->PassThruClose(devID), "PassThruClose");
#if defined Q_OS_UNIX
    j2534->close_serial_port();
#else
    j2534->disable();
#endif
    use_openport2_adapter = false;
    J2534_open_ok = false;
    J2534_get_version_ok = false;
    J2534_connect_ok = false;
    J2534_timing_ok = false;
    J2534_filters_ok = false;
    J2534_init_ok = false;
    j2534->J2534_init_ok = false;
    devID = 0;
    chanID = 0;
    msgID = 0;
    periodicActive = false;
    transportState = TransportState::Idle;
    transportStatus = STATUS_NOERROR;
    openedSerialPort.clear();
}

QByteArray SerialPortActionsDirect::set_error()
{
    QByteArray received;

    received.append((uint8_t)0x80);
    received.append((uint8_t)0xf0);
    received.append((uint8_t)0x10);
    received.append((uint8_t)0x03);
    received.append((uint8_t)0x7f);
    received.append('\0');
    received.append((uint8_t)0x13);

    return received;
}

QByteArray SerialPortActionsDirect::read_serial_obd_data(uint16_t timeout)
{
    if (use_openport2_adapter && (!J2534_open_ok || !J2534_connect_ok))
    {
        transport_failure(ERR_INVALID_CHANNEL_ID, "J2534 read (disconnected)", false);
        return {};
    }
    QByteArray received;

    if (is_serial_port_open())
    {
        if (use_openport2_adapter)
        {
            received = read_j2534_data(timeout);
            return received;
        }

        //emit LOG_D("Check bytes available", true, true);
        QTime dieTime = QTime::currentTime().addMSecs(timeout);
        while (!serial->bytesAvailable() && QTime::currentTime() < dieTime)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        //emit LOG_D("Byte(s) available or timeout", true, true);
        if (serial->bytesAvailable())
        {
            //emit LOG_D("Byte(s) available", true, true);
            QTime intervalTime = QTime::currentTime().addMSecs(_P1_MAX);
            while (QTime::currentTime() < dieTime)
            {
                if (serial->bytesAvailable())
                {
                    //emit LOG_D("Byte available", true, true);
                    received.append(serial->read(1));
                    intervalTime = QTime::currentTime().addMSecs(_P1_MAX);
                }
                if (intervalTime < QTime::currentTime())
                {
                    //emit LOG_D("Byte timeout", true, true);
                    break;
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            }
            //if (QTime::currentTime() > dieTime)
            //    emit LOG_D("Message timeout", true, true);
        }
    }
    return received;
}

QByteArray SerialPortActionsDirect::read_serial_data(uint16_t timeout)
{
    if (use_openport2_adapter && (!J2534_open_ok || !J2534_connect_ok))
    {
        transport_failure(ERR_INVALID_CHANNEL_ID, "J2534 read (disconnected)", false);
        return {};
    }
    QByteArray received;
    QByteArray req_bytes;
    uint32_t msglen = 0;

    if (is_serial_port_open())
    {
        if (use_openport2_adapter)
        {
            received = read_j2534_data(timeout);
            return received;
        }

        //emit LOG_D("Check if bytes available", true, true);
        received.clear();
        QTime dieTime = QTime::currentTime().addMSecs(timeout);
        while (!serial->bytesAvailable() && QTime::currentTime() < dieTime)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        if (serial->bytesAvailable())
        {
            QByteArray error_bytes;
            while (received.length() < 4 && QTime::currentTime() < dieTime)
            {
                while (serial->bytesAvailable() && received.length() < 4)
                    received.append(serial->read(1));
                if (!is_iso14230_connection)
                {
                    //emit LOG_D("Check for valid header", true, true);
                    error_bytes.clear();
                    while (received.length() > 2 && !received.startsWith("\xbe\xef") && !received.startsWith("\x80\xf0\x10") && !received.startsWith("\x80\xf0\x01"))
                    {
                        error_bytes.append(received.mid(0, 1));
                        received.remove(0, 1);
                    }
                    //emit LOG_D("Error bytes length: " + QString::number(error_bytes.length()) + " : " + parse_message_to_hex(error_bytes), true, true);
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            }
            //emit LOG_D("1. Response (header): " + parse_message_to_hex(received), true, true);
            if (received.size() < 4)
            {
                if (!received.isEmpty())
                    received.prepend(set_error());
                return received;
            }
            if (is_iso14230_connection)
            {
                //emit LOG_D("Read with ISO14230", true, true);

                if (received.at(0) & 0x3f)
                {
                    msglen = (received.at(0) & 0x3f); // Byte in index 3 is payload, no +1 for checksum
                }
                else
                    msglen = received.at(3) + 1; // +1 for checksum
            }
            else if (!is_iso14230_connection)
            {
                if (received.startsWith("\xbe\xef"))
                    msglen = ((uint8_t)received.at(2) << 8) + (uint8_t)received.at(3) + 1; // +1 for checksum
                if (received.startsWith("\x80\xf0"))
                    msglen = (uint8_t)received.at(3) + 1; // +1 for checksum
            }
            while ((uint32_t)req_bytes.length() < msglen && QTime::currentTime() < dieTime)
            {
                while (serial->bytesAvailable() && (uint32_t)req_bytes.length() < msglen)
                    req_bytes.append(serial->read(1));
                QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            }
            //emit LOG_D("2. Response (payload): " + parse_message_to_hex(req_bytes), true, true);
        }
        if (!received.length())
        {
            //emit LOG_D("No message received!", true, true);
        }
        else if (received.length() < 4 || (received.length() && (uint32_t)req_bytes.length() < msglen))
        {
            received.insert(0, set_error());
            received.append(req_bytes);
            //emit LOG_D("Message too short: " + parse_message_to_hex(received), true, true);
            return received;
        }
        received.append(req_bytes);
        //emit LOG_D("3. Response (full): " + parse_message_to_hex(received), true, true);

        return received;
    }
    return received;
}

QByteArray SerialPortActionsDirect::write_serial_data(QByteArray output)
{
    if (use_openport2_adapter && (!J2534_open_ok || !J2534_connect_ok))
    {
        transport_failure(ERR_INVALID_CHANNEL_ID, "J2534 write (disconnected)", false);
        return QByteArray(1, char(STATUS_ERROR));
    }
    QByteArray received;
    QByteArray msg;

    msg.append('\0');

    if (is_serial_port_open())
    {
        if (add_ssm_header)
            output = append_ssm_header(output);
        else if (add_iso9141_header)
            output = append_iso9141_header(output);
        else if (add_iso14230_header)
            output = append_iso14230_header(output);

        if (use_openport2_adapter)
        {
            return write_j2534_data(output) == STATUS_SUCCESS ? QByteArray() : QByteArray(1, char(STATUS_ERROR));
        }

        while (serial->bytesAvailable())
            received.append(serial->readAll());

        for (int i = 0; i < output.length(); i++)
        {
            msg[0] = output.at(i);
            serial->write(msg, 1);
        }
        received.clear();
        return STATUS_SUCCESS;
    }
    transportState = TransportState::Error;
    transportStatus = ERR_DEVICE_NOT_CONNECTED;
    emit LOG_E("Write rejected: no open serial/J2534 transport", true, true);
    return QByteArray(1, char(STATUS_ERROR));
}

QByteArray SerialPortActionsDirect::write_serial_data_echo_check(QByteArray output)
{
    if (use_openport2_adapter && (!J2534_open_ok || !J2534_connect_ok))
    {
        transport_failure(ERR_INVALID_CHANNEL_ID, "J2534 write (disconnected)", false);
        return QByteArray(1, char(STATUS_ERROR));
    }
    QByteArray received;
    QByteArray msg;

    msg.append('\0');

    if (is_serial_port_open())
    {
        if (add_ssm_header)
            output = append_ssm_header(output);
        else if (add_iso9141_header)
            output = append_iso9141_header(output);
        else if (add_iso14230_header)
            output = append_iso14230_header(output);

        if (use_openport2_adapter)
        {
            return write_j2534_data(output) == STATUS_SUCCESS ? QByteArray() : QByteArray(1, char(STATUS_ERROR));
        }

        while (serial->bytesAvailable())
            received.append(serial->readAll());

        received.clear();
        for (int i = 0; i < output.length(); i++)
        {
            msg[0] = output.at(i);
            serial->write(msg, 1);
            // Add serial echo read during transmit to speed up a little
            if (serial->bytesAvailable())
                received.append(serial->read(1));
        }
        QTime dieTime = QTime::currentTime().addMSecs(echo_check_timout);
        while (received.length() < output.length() && (QTime::currentTime() < dieTime))
        {
            while (serial->bytesAvailable() && received.length() < output.length())
            {
                dieTime = QTime::currentTime().addMSecs(echo_check_timout);
                received.append(serial->read(1));
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        if (received.length() < output.length())
            emit LOG_D("Write serial data echo read failed!", true, true);

        received.clear();
        return STATUS_SUCCESS;
    }
    transportState = TransportState::Error;
    transportStatus = ERR_DEVICE_NOT_CONNECTED;
    emit LOG_E("Write rejected: no open serial/J2534 transport", true, true);
    return QByteArray(1, char(STATUS_ERROR));
}

QByteArray SerialPortActionsDirect::append_ssm_header(QByteArray output)
{
    uint8_t chk_sum = 0;
    uint8_t msglength = output.length();

    output.insert(0, kline_startbyte);
    output.insert(1, kline_target_id);
    output.insert(2, kline_tester_id);
    output.insert(3, msglength);

    for (int i = 0; i < output.length(); i++)
        chk_sum = chk_sum + output.at(i);

    output.append(chk_sum);

    //LOG_D("Generated iso9141 message: " + parse_message_to_hex(output), true, true);

    return output;
}

QByteArray SerialPortActionsDirect::append_iso9141_header(QByteArray output)
{
    uint8_t chk_sum = 0;
    uint8_t msglength = output.length();

    output.insert(0, kline_startbyte);
    output.insert(1, kline_target_id);
    output.insert(2, kline_tester_id);

    for (int i = 0; i < output.length(); i++)
        chk_sum = chk_sum + output.at(i);

    output.append(chk_sum);

    //LOG_D("Generated iso9141 message: " + parse_message_to_hex(output), true, true);

    return output;
}

QByteArray SerialPortActionsDirect::append_iso14230_header(QByteArray output)
{
    uint8_t chk_sum = 0;
    uint8_t msglength = output.length();

    //emit LOG_D("Adding iso14230 header to message", true, true);

    output.insert(0, kline_startbyte);
    output.insert(1, kline_target_id);
    output.insert(2, kline_tester_id);
    if (msglength < 0x40)
        output[0] = output[0] | msglength;
    else
        output.insert(3, msglength);

    for (int i = 0; i < output.length(); i++)
        chk_sum = chk_sum + output.at(i);

    output.append(chk_sum);

    //LOG_D("Generated iso14230 message: " + parse_message_to_hex(output), true, true);

    return output;
}

bool SerialPortActionsDirect::valid_message_size(int size) const
{
    const int minimum = protocol == CAN || protocol == ISO15765 ? 4 : 1;
    return size >= minimum && size <= PASSTHRU_MSG_DATA_SIZE;
}

int SerialPortActionsDirect::transport_failure(long status, const char *operation, bool driverError)
{
    transportState = TransportState::Error;
    transportStatus = status;
    if (driverError)
        reportJ2534Error(status, operation);
    else
        emit LOG_E(QString("J2534 %1 rejected locally (status 0x%2)").arg(operation).arg(status, 0, 16), true, true);
    if (status == ERR_INVALID_CHANNEL_ID || status == ERR_INVALID_DEVICE_ID || status == ERR_DEVICE_NOT_CONNECTED)
    {
        J2534_connect_ok = false;
        J2534_init_ok = false;
        j2534->J2534_init_ok = false;
        periodicActive = false;
        chanID = 0;
        if (status != ERR_INVALID_CHANNEL_ID)
            J2534_open_ok = false;
    }
    return STATUS_ERROR;
}

int SerialPortActionsDirect::write_j2534_data(QByteArray output)
{
    if (!J2534_open_ok || !J2534_connect_ok)
        return transport_failure(ERR_INVALID_CHANNEL_ID, "PassThruWriteMsgs (disconnected)", false);
    if (!valid_message_size(output.size()))
        return transport_failure(ERR_INVALID_MSG, "PassThruWriteMsgs (length)", false);
    PASSTHRU_MSG txmsg = {};
    txmsg.ProtocolID = protocol;
    if (protocol == CAN || protocol == ISO15765)
        txmsg.TxFlags = can_id_flag();
    txmsg.TxFlags |= ISO15765_FRAME_PAD; // preserve existing flags
    txmsg.DataSize = static_cast<unsigned long>(output.size());
    memcpy(txmsg.Data, output.constData(), static_cast<size_t>(output.size()));
    unsigned long count = 1;
    const long status = j2534->PassThruWriteMsgs(chanID, &txmsg, &count, 100);
    communicationDriverStatus = status;
    communicationHasDriverStatus = true;
    // One atomic message is submitted; never retry an uncertain write or split
    // a message into new frames. Even count==1 with an error is not success.
    if (count > 1)
        return transport_failure(ERR_INVALID_MSG, "PassThruWriteMsgs (count)", false);
    if (status != STATUS_NOERROR)
        return transport_failure(status, "PassThruWriteMsgs");
    if (count != 1)
        return transport_failure(ERR_FAILED, "PassThruWriteMsgs (zero transmitted)", false);
    transportState = TransportState::Success;
    transportStatus = STATUS_NOERROR;
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::send_periodic_j2534_data(QByteArray output, int timeout)
{
    if (!J2534_open_ok || !J2534_connect_ok)
        return transport_failure(ERR_INVALID_CHANNEL_ID, "PassThruStartPeriodicMsg (disconnected)", false);
    if (!valid_message_size(output.size()) || timeout <= 0 || periodicActive)
        return transport_failure(ERR_INVALID_MSG, "PassThruStartPeriodicMsg (length/interval/already active)", false);
    PASSTHRU_MSG txmsg = {};
    txmsg.ProtocolID = protocol;
    txmsg.DataSize = static_cast<unsigned long>(output.size());
    memcpy(txmsg.Data, output.constData(), static_cast<size_t>(output.size()));
    unsigned long id = 0;
    const long status = j2534->PassThruStartPeriodicMsg(chanID, &txmsg, &id, static_cast<unsigned long>(timeout));
    if (status != STATUS_NOERROR)
        return transport_failure(status, "PassThruStartPeriodicMsg");
    msgID = id; // zero is valid when the API returned success
    periodicActive = true;
    delay(10);
    transportState = TransportState::Success;
    transportStatus = STATUS_NOERROR;
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::stop_periodic_j2534_data()
{
    if (!J2534_open_ok || !J2534_connect_ok)
        return transport_failure(ERR_INVALID_CHANNEL_ID, "PassThruStopPeriodicMsg (disconnected)", false);
    if (!periodicActive)
        return transport_failure(ERR_INVALID_MSG_ID, "PassThruStopPeriodicMsg (no active message)", false);
    const long status = j2534->PassThruStopPeriodicMsg(chanID, msgID);
    if (status != STATUS_NOERROR)
        return transport_failure(status, "PassThruStopPeriodicMsg");
    periodicActive = false;
    msgID = 0;
    delay(10);
    transportState = TransportState::Success;
    transportStatus = STATUS_NOERROR;
    return STATUS_SUCCESS;
}

bool SerialPortActionsDirect::get_is_tx_done()
{
#if defined Q_OS_UNIX
    return j2534->get_is_tx_done();
#else
    return false;
#endif
}

QByteArray SerialPortActionsDirect::read_j2534_data(unsigned long timeout)
{
    if (!J2534_open_ok || !J2534_connect_ok)
    {
        transport_failure(ERR_INVALID_CHANNEL_ID, "PassThruReadMsgs (disconnected)", false);
        return {};
    }
    QElapsedTimer timer;
    timer.start();
    // Preserve the previous maximum of three API reads, but share one deadline.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        PASSTHRU_MSG msg = {};
        unsigned long count = 1;
        const auto elapsed = static_cast<unsigned long>(timer.elapsed());
        const unsigned long remaining = elapsed >= timeout ? 0 : timeout - elapsed;
        const long status = j2534->PassThruReadMsgs(chanID, &msg, &count, remaining);
        communicationDriverStatus = status;
        communicationHasDriverStatus = true;
        const bool noMessage = status == ERR_TIMEOUT || status == ERR_BUFFER_EMPTY;
        if (status != STATUS_NOERROR && !noMessage)
        {
            transport_failure(status, "PassThruReadMsgs");
            return {};
        }
        if (count > 1 || (count == 1 && (msg.DataSize > PASSTHRU_MSG_DATA_SIZE || msg.ExtraDataIndex > msg.DataSize)))
        {
            transport_failure(ERR_INVALID_MSG, "PassThruReadMsgs (count/length)", false);
            return {};
        }
        transportStatus = status;
        if (count == 0)
        {
            transportState = TransportState::NoMessage;
            return {};
        }
        if (msg.RxStatus & (TX_DONE | START_OF_MESSAGE | TX_MSG_TYPE))
        {
            if (timer.elapsed() >= static_cast<qint64>(timeout) && timeout != 0)
                break;
            continue; // API notification, not received payload
        }
        if (!valid_message_size(static_cast<int>(msg.DataSize)))
        {
            transport_failure(ERR_INVALID_MSG, "PassThruReadMsgs (short payload)", false);
            return {};
        }
        transportState = noMessage ? TransportState::Partial : TransportState::Success;
        return QByteArray(reinterpret_cast<const char *>(msg.Data), static_cast<int>(msg.DataSize));
    }
    transportState = TransportState::NoMessage;
    return {};
}

int SerialPortActionsDirect::set_j2534_ioctl(unsigned long parameter, int value)
{
    if (!J2534_open_ok || !J2534_connect_ok)
        return transport_failure(ERR_INVALID_CHANNEL_ID, "SET_CONFIG (disconnected)", false);
    SCONFIG config = {parameter, static_cast<unsigned long>(value)};
    SCONFIG_LIST list = {1, &config};
    const long status = j2534->PassThruIoctl(chanID, SET_CONFIG, &list, NULL);
    return status == STATUS_NOERROR ? STATUS_SUCCESS : transport_failure(status, "SET_CONFIG");
}

unsigned long SerialPortActionsDirect::read_vbatt()
{
    if (use_openport2_adapter)
    {
        if (!J2534_open_ok)
            return STATUS_ERROR;
        if (!checkJ2534Status(j2534->PassThruIoctl(devID,READ_VBATT,NULL,&vBatt), "READ_VBATT"))
        {
            return STATUS_ERROR;
        }
        //emit LOG_D("Batt: " + QString::number(vBatt / 1000.0) + " V", true, true);

        return vBatt;

    }
    else
    {
        //emit LOG_D("Adapter does not support reading voltage", true, true);
        return STATUS_SUCCESS;
    }

    return STATUS_SUCCESS;
}

void SerialPortActionsDirect::dump_msg(PASSTHRU_MSG* msg)
{
    QByteArray datamsg;

    if (msg->RxStatus & START_OF_MESSAGE)
        return; // skip

    datamsg.clear();
    for (unsigned int i = 0; i < msg->DataSize; i++)
    {
        datamsg.append(QString("%1 ").arg(msg->Data[i],2,16,QLatin1Char('0')).toUtf8());
    }
    //emit LOG_D("Timestamp: " + msg->Timestamp << "msg length: " + msg->DataSize << "msg: " + datamsg;
}

bool SerialPortActionsDirect::get_serial_num(char* serial, size_t capacity)
{
    // There is no standard J2534 serial-number query. The former vendor IOCTL
    // was disabled, so it must not report an uninitialized payload as success.
    if (serial && capacity)
        serial[0] = '\0';
    return false;
}

int SerialPortActionsDirect::init_j2534_connection()
{
    close_j2534_serial_port();
    const auto fail = [this]() {
        close_j2534_serial_port();
        return STATUS_ERROR;
    };
#if defined Q_OS_UNIX
    if (j2534->open_serial_port(serial_port) != serial_port)
        return fail();
#endif
    if (!j2534->init())
    {
        emit LOG_E("INIT: Can't load J2534 DLL.", true, true);
#if defined Q_OS_WIN32
        emit LOG_E(QString::fromLocal8Bit(j2534->getLastError()), true, true);
#endif
        return fail();
    }
    if (!checkJ2534Status(j2534->PassThruOpen(NULL, &devID), "PassThruOpen"))
        return fail();
    J2534_open_ok = true;

    // J2534 version outputs have an 80-byte contract. Keep the historical larger
    // buffers, initialize them, and reject missing terminators without strlen.
    char strApiVersion[256] = {};
    char strDllVersion[256] = {};
    char strFirmwareVersion[256] = {};
    char strSerial[256] = {};
    const long versionStatus = j2534->PassThruReadVersion(strApiVersion, strDllVersion, strFirmwareVersion, devID);
    if (versionStatus == STATUS_NOERROR)
    {
        for (const char *value : {strApiVersion, strDllVersion, strFirmwareVersion})
        {
            if (!memchr(value, '\0', 80))
            {
                emit LOG_E("J2534 version response is not terminated within 80 bytes.", true, true);
                return fail();
            }
        }
        J2534_get_version_ok = true;
        emit LOG_D("J2534 API Version: " + QString::fromLocal8Bit(strApiVersion), true, true);
        emit LOG_D("J2534 DLL Version: " + QString::fromLocal8Bit(strDllVersion), true, true);
        emit LOG_D("Device Firmware Version: " + QString::fromLocal8Bit(strFirmwareVersion), true, true);
    }
    else if (versionStatus != ERR_NOT_SUPPORTED)
    {
        reportJ2534Error(versionStatus, "PassThruReadVersion");
        return fail();
    }
    else
        emit LOG_D("J2534 versions: unavailable (operation unsupported)", true, true);
    if (!get_serial_num(strSerial, sizeof(strSerial)))
        emit LOG_D("Device Serial Number: unavailable (no standard J2534 query)", true, true);

    if (is_iso15765_connection || is_can_connection)
    {
        if (set_j2534_can() != STATUS_SUCCESS ||
            set_j2534_can_timings() != STATUS_SUCCESS ||
            set_j2534_can_filters() != STATUS_SUCCESS)
            return fail();
    }
    else
    {
        if (set_j2534_iso9141() != STATUS_SUCCESS ||
            set_j2534_iso9141_timings() != STATUS_SUCCESS ||
            set_j2534_iso9141_filters() != STATUS_SUCCESS)
            return fail();
    }
    J2534_timing_ok = true;
    J2534_filters_ok = true;
    J2534_init_ok = true;
    j2534->J2534_init_ok = true;
    return STATUS_SUCCESS;
}

unsigned long SerialPortActionsDirect::can_id_flag()
{
    /* Returns CAN_29BIT_ID when extended (29-bit) identifiers must be used.
     * Explicit flag wins; otherwise auto-detect: any ID above the 11-bit
     * range (0x7FF) can only be carried by an extended identifier.
     * This keeps 11-bit targets (e.g. FBL 0x7E0/0x7E8) on standard IDs. */
    if (is_29_bit_id)
        return CAN_29BIT_ID;

    if (is_iso15765_connection)
    {
        if (iso15765_source_address > 0x7FF || iso15765_destination_address > 0x7FF)
            return CAN_29BIT_ID;
    }
    else if (is_can_connection)
    {
        if (can_source_address > 0x7FF || can_destination_address > 0x7FF)
            return CAN_29BIT_ID;
    }

    return 0;
}

int SerialPortActionsDirect::set_j2534_can()
{
    if (is_can_connection)
    {
        emit LOG_D("Set CAN flags", true, true);
        protocol = CAN;
        flags = can_id_flag();
    }
    else if (is_iso15765_connection)
    {
        emit LOG_D("Set iso15765 flags", true, true);
        protocol = ISO15765;
        flags = can_id_flag();
    }
    //Denso DST-i hack (must not clear the extended-ID flag: an
    //11-bit connect would make 29-bit targets unreachable)
    if (J2534_is_denso_dsti && protocol == ISO15765 && !(flags & CAN_29BIT_ID))
    {
        flags = 0;
    }

    if (flags & CAN_29BIT_ID)
        emit LOG_D("Using extended 29-bit CAN identifiers", true, true);
    else
        emit LOG_D("Using standard 11-bit CAN identifiers", true, true);
    baudrate = can_speed.toUInt();
    if (!J2534_open_ok)
        return STATUS_ERROR;
    // use ISO9141_NO_CHECKSUM to disable checksumming on both tx and rx messages
    if (!checkJ2534Status(j2534->PassThruConnect(devID, protocol, flags, baudrate, &chanID), "PassThruConnect"))
    {
        return STATUS_ERROR;
    }
    else
    {
#if defined Q_OS_UNIX
        chanID = protocol;
#endif
        //emit LOG_D("Connected: " + devID << protocol << baudrate << chanID;
    }

    J2534_connect_ok = true;
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::unset_j2534_can()
{
    if (!J2534_connect_ok)
        return STATUS_ERROR;
    if (!checkJ2534Status(j2534->PassThruDisconnect(chanID), "PassThruDisconnect"))
    {
        return STATUS_ERROR;
    }

    J2534_connect_ok = false;
    J2534_init_ok = false;
    j2534->J2534_init_ok = false;
    J2534_timing_ok = false;
    J2534_filters_ok = false;
    chanID = 0;
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::set_j2534_can_timings()
{
    // Set timeouts etc.
    if (is_can_connection)
        emit LOG_D("Set CAN timings", true, true);
    else if (is_iso15765_connection)
        emit LOG_D("Set iso15765 timings", true, true);
    SCONFIG_LIST scl;
    SCONFIG scp[] = {{LOOPBACK, 0}};
    scl.NumOfParams = ARRAYSIZE(scp);
    scl.ConfigPtr = scp;
    if (!checkJ2534Status(j2534->PassThruIoctl(chanID,SET_CONFIG,&scl,NULL), "PassThruIoctl"))
    {
        return STATUS_ERROR;
    }
    else
    {
        //emit LOG_D("Set timings OK";
    }

    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::set_j2534_can_filters()
{
    // now setup the filter(s)
    PASSTHRU_MSG txmsg = {};
    PASSTHRU_MSG msgMask, msgPattern, msgFlow;
    unsigned long msgId;

    if (!checkJ2534Status(j2534->PassThruIoctl(chanID, CLEAR_MSG_FILTERS, NULL, NULL), "CLEAR_MSG_FILTERS"))
        return STATUS_ERROR;

    if (is_can_connection)
    {
        emit LOG_D("Set CAN filters", true, true);
        txmsg.ProtocolID = protocol;
        txmsg.RxStatus = 0;
        txmsg.TxFlags = CAN_29BIT_ID;
        txmsg.Timestamp = 0;
        txmsg.DataSize = 4;
        txmsg.ExtraDataIndex = 0;

        msgMask = msgPattern = txmsg;
        memset(msgMask.Data, 0xFF, txmsg.DataSize);
        memset(msgPattern.Data, 0xFF, txmsg.DataSize);

        msgPattern.Data[0] = (can_destination_address >> 24) & 0xFF;
        msgPattern.Data[1] = (can_destination_address >> 16) & 0xFF;
        msgPattern.Data[2] = (can_destination_address >> 8) & 0xFF;
        msgPattern.Data[3] = (can_destination_address & 0xFF);

        if (!checkJ2534Status(j2534->PassThruStartMsgFilter(chanID, PASS_FILTER, &msgMask, &msgPattern, NULL, &msgId), "PassThruStartMsgFilter"))
        {
            return STATUS_ERROR;
        }
    }
    else if (is_iso15765_connection)
    {
        emit LOG_D("Set iso15765 filters", true, true);
        txmsg.ProtocolID = protocol;
        txmsg.RxStatus = 0;
        txmsg.TxFlags = ISO15765_FRAME_PAD | can_id_flag();
        txmsg.Timestamp = 0;
        txmsg.DataSize = 4;
        txmsg.ExtraDataIndex = 0;
        msgMask = msgPattern = msgFlow = txmsg;
        memset(msgMask.Data, 0xFF, txmsg.DataSize);
        memset(msgPattern.Data, 0xFF, txmsg.DataSize);
        memset(msgFlow.Data, 0xFF, txmsg.DataSize);

        msgPattern.Data[0] = (iso15765_destination_address >> 24) & 0xFF;
        msgPattern.Data[1] = (iso15765_destination_address >> 16) & 0xFF;
        msgPattern.Data[2] = (iso15765_destination_address >> 8) & 0xFF;
        msgPattern.Data[3] = iso15765_destination_address & 0xFF;
        msgFlow.Data[0] = (iso15765_source_address >> 24) & 0xFF;
        msgFlow.Data[1] = (iso15765_source_address >> 16) & 0xFF;
        msgFlow.Data[2] = (iso15765_source_address >> 8) & 0xFF;
        msgFlow.Data[3] = (iso15765_source_address & 0xFF);

        if (!checkJ2534Status(j2534->PassThruStartMsgFilter(chanID, FLOW_CONTROL_FILTER, &msgMask, &msgPattern, &msgFlow, &msgId), "PassThruStartMsgFilter"))
        {
            return STATUS_ERROR;
        }
    }
    else
        return STATUS_ERROR;

    if (is_can_connection)
        emit LOG_D("CAN filters OK", true, true);
    else if (is_iso15765_connection)
        emit LOG_D("ISO15765 filters OK", true, true);

    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::set_j2534_iso9141()
{
    baudrate = serial_port_baudrate.toUInt();

    if (is_iso14230_connection)
    {
        protocol = ISO14230;
        flags = ISO9141_NO_CHECKSUM | CAN_ID_BOTH;
    }
    else
    {
        protocol = ISO9141;
        flags = ISO9141_NO_CHECKSUM;
    }

    emit LOG_D("Protocol: " + QString::number(protocol), true, true);

    if (J2534_is_denso_dsti)
    {
        switch (protocol)
        {
        case ISO9141:
            //DST-i does not work well with ISO9141
            //sometimes it reads by 1 byte
            //Denso DST-i specific protocol, in fact ISO9141
            protocol = DSTI_ISO9141;
            flags = ISO9141_NO_CHECKSUM;
            baudrate = 10400;
            break;
        case ISO14230:
            flags = ISO9141_K_LINE_ONLY;
            break;
        }
    }

    if (!J2534_open_ok)
        return STATUS_ERROR;
    // use ISO9141_NO_CHECKSUM to disable checksumming on both tx and rx messages
    if (!checkJ2534Status(j2534->PassThruConnect(devID, protocol, flags, baudrate, &chanID), "PassThruConnect"))
    {
        return STATUS_ERROR;
    }
    else
    {
#if defined Q_OS_WIN32
        emit LOG_D("Connected: DevID " + QString::number(devID) + ", protocol " + QString::number(protocol) + ", baudrate " + QString::number(baudrate) + ", chanID " + QString::number(chanID), true, true);
#elif defined Q_OS_UNIX
        chanID = protocol;
        emit LOG_D("Connected: DevID " + QString::number(devID) + ", protocol " + QString::number(protocol) + ", baudrate " + QString::number(baudrate) + ", chanID " + QString::number(chanID), true, true);
#endif
    }

    J2534_connect_ok = true;
    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::set_j2534_iso9141_timings()
{
    if (J2534_is_denso_dsti)
    {
        SCONFIG_LIST scl;
        SCONFIG scp_dsti_ISO14230[] = {{LOOPBACK,0},{P1_MAX,0xa},{P3_MIN,0x14},{P4_MIN,0},{DATA_RATE,4800}};
        SCONFIG scp_dsti_DSTI_ISO9141[] = {{DATA_RATE, 4800},  {LOOPBACK, 0},  {P1_MIN, 0},
                                           {P1_MAX, 4},        {P2_MIN, 4},    {P2_MAX, 0x14},
                                           {P3_MIN, 0x14},     {P3_MAX, 10000},{P4_MIN, 0},
                                           {P4_MAX, 0x14}};

        if (clear_tx_buffer() != STATUS_SUCCESS || clear_rx_buffer() != STATUS_SUCCESS)
            return STATUS_ERROR;

        switch (protocol)
        {
        case ISO14230:
            scl.ConfigPtr = scp_dsti_ISO14230;
            scl.NumOfParams = ARRAYSIZE(scp_dsti_ISO14230);
            break;
        case DSTI_ISO9141:
            scl.ConfigPtr = scp_dsti_DSTI_ISO9141;
            scl.NumOfParams = ARRAYSIZE(scp_dsti_DSTI_ISO9141);
            break;
        }

        if (!checkJ2534Status(j2534->PassThruIoctl(chanID, SET_CONFIG, &scl, NULL), "PassThruIoctl"))
        {
            return STATUS_ERROR;
        }

    }
    else
    {
        // Set timeouts etc.
        SCONFIG_LIST scl;
        SCONFIG scp[6] = {{LOOPBACK,0},{P1_MAX,0},{P3_MIN,0},{P4_MIN,0},{PARITY,0},{TINIL,0}};
        scl.NumOfParams = 6;
        scp[0].Value = 0;
        scp[1].Value = 1;
        scp[2].Value = 0;
        scp[3].Value = 0;
        scp[4].Value = NO_PARITY;
        if (serial_port_parity == QSerialPort::OddParity)
            scp[4].Value = ODD_PARITY;
        else if (serial_port_parity == QSerialPort::EvenParity)
            scp[4].Value = EVEN_PARITY;
        scp[5].Value = 25;
        scl.ConfigPtr = scp;
        if (!checkJ2534Status(j2534->PassThruIoctl(chanID,SET_CONFIG,&scl,NULL), "PassThruIoctl"))
        {
            return STATUS_ERROR;
        }
        else
        {
            //emit LOG_D("Set timings OK";
        }
    }

    return STATUS_SUCCESS;
}

int SerialPortActionsDirect::set_j2534_iso9141_filters()
{
    // now setup the filter(s)
    PASSTHRU_MSG txmsg = {};
    PASSTHRU_MSG msgMask,msgPattern;
    unsigned long msgId;

    // simply create a "pass all" filter so that we can see
    // everything unfiltered in the raw stream

    txmsg.ProtocolID = protocol;
    txmsg.RxStatus = 0;
    txmsg.TxFlags = 0;
    txmsg.Timestamp = 0;
    txmsg.DataSize = 4;
    txmsg.ExtraDataIndex = 0;
    msgMask = msgPattern = txmsg;
    memset(msgMask.Data, 0, txmsg.DataSize); // mask the first 4 byte to 0
    memset(msgPattern.Data, 0, txmsg.DataSize);// match it with 0 (i.e. pass everything)
    if (!checkJ2534Status(j2534->PassThruStartMsgFilter(chanID, PASS_FILTER, &msgMask, &msgPattern, NULL, &msgId), "PassThruStartMsgFilter"))
    {
        return STATUS_ERROR;
    }
    else
    {
        //emit LOG_D("Set filters OK";
    }
    //j2534->PassThruSetProgrammingVoltage(devID, J1962_PIN_9, 5000);

    return STATUS_SUCCESS;
}

bool SerialPortActionsDirect::checkJ2534Status(long status, const char *operation)
{
    if (status == STATUS_NOERROR)
        return true;
    reportJ2534Error(status, operation);
    return false;
}

void SerialPortActionsDirect::reportJ2534Error(long status, const char *operation)
{
    char err[512] = {};
    const long errorStatus = j2534->PassThruGetLastError(err);
    err[sizeof(err) - 1] = '\0';
    emit LOG_E(QString("J2534 %1 failed (status 0x%2): %3")
               .arg(operation ? operation : "operation")
               .arg(static_cast<qulonglong>(status), 0, 16)
               .arg(errorStatus == STATUS_NOERROR ? QString::fromLocal8Bit(err)
                                                  : QString("driver error text unavailable (status 0x%1)").arg(errorStatus, 0, 16)),
               true, true);
}

void SerialPortActionsDirect::handle_error(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError)
        emit LOG_D("Error: " + QString::number(error), true, true);

    if (error == QSerialPort::NoError)
    {
    }
    else if (error == QSerialPort::DeviceNotFoundError)
    {
        reset_connection();
    }
    else if (error == QSerialPort::PermissionError)
    {
    }
    else if (error == QSerialPort::OpenError)
    {
        reset_connection();
    }
    else if (error == QSerialPort::NotOpenError)
    {
        reset_connection();
    }
/*
    else if (error == QSerialPort::ParityError)
    {
    }
    else if (error == QSerialPort::FramingError)
    {
    }
    else if (error == QSerialPort::BreakConditionError)
    {
    }
*/
    else if (error == QSerialPort::WriteError)
    {
        reset_connection();
    }
    else if (error == QSerialPort::ReadError)
    {
        reset_connection();
    }
    else if (error == QSerialPort::ResourceError)
    {
        reset_connection();
    }
    else if (error == QSerialPort::UnsupportedOperationError)
    {
    }
    else if (error == QSerialPort::TimeoutError)
    {
        reset_connection();
        //emit LOG_E("Timeout error";
        /*
        if (serial->isOpen())
            serial->flush();
        else
            serial->close();
        */
    }
    else if (error == QSerialPort::UnknownError)
    {
        reset_connection();
    }
}

void SerialPortActionsDirect::accurate_delay(double timeout)
{
    double seconds = timeout / 1000.0;
    auto spinStart = std::chrono::high_resolution_clock::now();
    while ((std::chrono::high_resolution_clock::now() - spinStart).count() / 1e9 < seconds);
}

void SerialPortActionsDirect::fast_delay(int timeout)
{
    QTime dieTime = QTime::currentTime().addMSecs(timeout);
    while (QTime::currentTime() < dieTime)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
}

void SerialPortActionsDirect::delay(int timeout)
{
    double seconds = (double)timeout / 1000.0;
    auto spinStart = std::chrono::high_resolution_clock::now();
    while ((std::chrono::high_resolution_clock::now() - spinStart).count() / 1e9 < seconds)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    //QTime dieTime = QTime::currentTime().addMSecs(timeout);
    //while (QTime::currentTime() < dieTime)
    //    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QString SerialPortActionsDirect::parse_message_to_hex(QByteArray received)
{
    QByteArray msg;

    for (int i = 0; i < received.length(); i++)
    {
        msg.append(QString("%1 ").arg((uint8_t)received.at(i),2,16,QLatin1Char('0')).toUtf8());
    }

    return msg;
}


CommunicationResult SerialPortActionsDirect::communication_result(const QByteArray &bytes, bool write)
{
    using State = CommunicationResult::State;
    CommunicationResult r;
    r.bytes = write ? QByteArray() : bytes;
    r.status = communicationDriverStatus;
    r.hasStatus = communicationHasDriverStatus;
    if (transportState == TransportState::Success && (!write || bytes.isEmpty())) r.state = State::Success;
    else if (!write && transportState == TransportState::NoMessage) r.state = State::Timeout;
    else if (!write && transportState == TransportState::Partial) r.state = State::Malformed;
    else if (transportStatus == ERR_INVALID_CHANNEL_ID || transportStatus == ERR_INVALID_DEVICE_ID || transportStatus == ERR_DEVICE_NOT_CONNECTED) r.state = State::Disconnected;
    else if (transportStatus == ERR_INVALID_MSG) r.state = State::Malformed;
    else r.state = State::TransportError;
    r.detail = QString("J2534 outcome %1, local status 0x%2").arg(int(r.state)).arg(transportStatus,0,16);
    return r;
}

CommunicationResult SerialPortActionsDirect::write_j2534_checked(QByteArray output)
{
    if (!use_openport2_adapter)
        return CommunicationResult::failure(CommunicationResult::State::Unsupported, "SH7058 checked communication requires J2534");
    communicationHasDriverStatus = false;
    communicationDriverStatus = 0;
    transportState = TransportState::Idle;
    const QByteArray result = write_serial_data_echo_check(output);
    return communication_result(result, true);
}

CommunicationResult SerialPortActionsDirect::read_j2534_checked(uint16_t timeout)
{
    if (!use_openport2_adapter)
        return CommunicationResult::failure(CommunicationResult::State::Unsupported, "SH7058 checked communication requires J2534");
    communicationHasDriverStatus = false;
    communicationDriverStatus = 0;
    transportState = TransportState::Idle;
    const QByteArray bytes = read_serial_data(timeout);
    return communication_result(bytes, false);
}
