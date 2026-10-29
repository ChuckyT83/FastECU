/*
  Copyright (C) 2022
  Authors: NikolaKozina
			Dale Schultz

  You are free to use this software for any purpose, but please keep
  acknowledge where it came from!

  Before using this library, remove the SD card from the Openport 2.0

  On Linux, USB device requires write permission, add a udev rule entry in /etc/udev/rules.d/
  with the contents such as this to allow write access:
  SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d", GROUP="dialout", MODE="0666"
  User must be a member of the dialout group on the system

  On Windows, the Openport 2.0 system driver has to be changed from the one provided by
  Tactrix to WinUSB.  The easiest way to complete this is to use Zadig (https://zadig.akeo.ie/).

  To enable runtime debug logging to a file, create an environment variable
  with the name LOG_ENABLE and set it to the path and filename to write to.
  To enable libusb messages, create a LIBUSB_DEBUG environment variable and set libusb
  message verbosity:
	NONE = 0, ERROR = 1, WARNING = 2, INFO = 3, DEBUG = 4

  If linked with libusb version 1.0.10 thru 1.0.12, define a preprocessor symbol LIBUSB1010  before
  compilation to enable libusb library version reporting in this library's version info string.
 */

#include "j2534.h"
#include <errno.h>
#include <libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _MSC_VER
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) { return TRUE; }
#endif

// LIBUSBX_API_VERSION is available in libusb version 1.0.13 and later
#if defined(LIBUSB_API_VERSION) || defined(LIBUSBX_API_VERSION) || defined(LIBUSB1010)
#define GET_LIBUSB_VERSION
#endif

#define MAX_LEN	80	// Maximum length of small data message
#define LE_LEN	80	// Maximum length of an error message string
#define LM_LEN 256	// Maximum length of writelog() message

typedef struct _connection
{
	uint8_t device_id;
	int8_t  channel;
	unsigned long protocol_id;
	struct libusb_context *ctx;
	struct libusb_device_handle *dev_handle;
} connection_t;

typedef struct _endpoint
{
	uint8_t intf_num;
	uint8_t addr_in;
	uint8_t addr_out;
} endpoint_t;

typedef struct _fifo_msg
{
	PASSTHRU_MSG *pt_msg;
	struct _fifo_msg *next_in_q;
} fifo_msg_t;

const char *DELIMITERS = " \r\n";
const uint16_t VENDOR_ID = 0x0403;
const uint16_t PRODUCT_ID = 0xcc4d;
const uint8_t ISO9141 = 0x33;
const uint8_t ISO14230 = 0x34;
const uint8_t CAN = 0x35;
const uint8_t ISO15765 = 0x36;
#ifdef _MSC_VER
__declspec(thread) int8_t LAST_ERROR[LE_LEN];
#else
static __thread int8_t LAST_ERROR[LE_LEN];
#endif
int littleEndian = TRUE;
int write_log = FALSE;
int8_t log_msg[LM_LEN];
char fw_version[MAX_LEN];
FILE *logfile;
connection_t con[1];
endpoint_t endpoint[1];
fifo_msg_t *fifo_head = NULL;

enum rx_msg_type {
	NORM_MSG,
	TX_DONE = 0x10,
	TX_LB_MSG = 0x20,
	RX_MSG_END_IND = 0x40,
	EXT_ADDR_MSG_END_IND = 0x44,
	LB_MSG_END_IND = 0x60,
	NORM_MSG_START_IND = 0x80,
	TX_LB_START_IND = 0xA0,
};

static void writelog(const char *str)
{
	fprintf(logfile, "%s", str);
}

static void writelogmsg(const uint8_t *data, const unsigned long start, const unsigned long len)
{
	unsigned long i = start;
	for (; i < len; i++)
		fprintf(logfile, "%02X ", (uint8_t)data[i]);
}

static void writelogpassthrumsg(const PASSTHRU_MSG *msg)
{
	fprintf(logfile,
		"\tMSG: %p\n"
		"\t\tProtocolID:\t%lu\n"
		"\t\tRxStatus:\t%08lX\n"
		"\t\tTxFlags:\t%08lX\n"
		"\t\tTimeStamp:\t0x%08lX (%lu \xC2\xB5sec)\n" // micro seconds
		"\t\tDataSize:\t%lu\n"
		"\t\tExtraData:\t%lu\n"
		"\t\tData:\n\t\t\t",
		msg, msg->ProtocolID, msg->RxStatus, msg->TxFlags, msg->Timestamp,
		msg->Timestamp, msg->DataSize, msg->ExtraDataIndex);
	writelogmsg(msg->Data, 0, msg->DataSize);
	fprintf(logfile, "\n");
}

/*
   Determine the Endian of the CPU
 */
static int isLittleEndian()
{
	int r = -1;
	uint16_t a = 0x1234;
	if (*((uint8_t*)&a) == 0x12)
	{
		r = FALSE;
		if (write_log)
			writelog("CPU Endian: big\n");
	}
	else
	{
		r = TRUE;
		if (write_log)
			writelog("CPU Endian: little\n");
	}
	return r;
}

/*
   Byte swap and return the provided 4-byte value
 */
static uint32_t bswap(const uint32_t bytes)
{
	union {
		uint8_t  B0[4];
		uint32_t B4;
	} tmp_bytes;
	tmp_bytes.B0[0] = (uint8_t)(bytes >> 24);
	tmp_bytes.B0[1] = (uint8_t)(bytes >> 16);
	tmp_bytes.B0[2] = (uint8_t)(bytes >> 8);
	tmp_bytes.B0[3] = (uint8_t)(bytes & 0xff);
	return tmp_bytes.B4;
}

/*
  Parse a timestamp. This parse_ts function parses four bytes from
  the given data array and copies them to a long, then swaps them
  based on the processor endian.
 */
static uint32_t parse_ts(const void *data)
{
	uint32_t timestamp = 0;
	memcpy(&timestamp, data, 4);
	if (littleEndian)
		timestamp = bswap(timestamp);
	return timestamp;
}

/*
  This copy function copies bytes, 2 at a time between s_start to s_end
  from the object src into the object dest beginning at d_pos.
  */

/*
   This open_dev_endpoints function locates the device to open by Vendor and
   Product.  Opens the device and sets the handle to use, then determines the
   addresses for the endpoint transmit and receive queues.
 */
static int open_dev_endpoints(libusb_device **devs, const ssize_t cnt,
    const uint16_t vendor_id, const uint16_t product_id, endpoint_t *ep)
{
    for (ssize_t x = 0; x < cnt; ++x) {
        struct libusb_device_descriptor desc;
        int r = libusb_get_device_descriptor(devs[x], &desc);
        if (r != LIBUSB_SUCCESS) return r;
        if (desc.idVendor != vendor_id || desc.idProduct != product_id) continue;
        r = libusb_open(devs[x], &con->dev_handle);
        if (r != LIBUSB_SUCCESS) return r;
        con->device_id = libusb_get_device_address(devs[x]);
        struct libusb_config_descriptor *config = NULL;
        r = libusb_get_config_descriptor(devs[x], 0, &config);
        if (r != LIBUSB_SUCCESS) return r; /* caller owns the opened handle */
        int found = FALSE;
        for (uint8_t i = 0; i < config->bNumInterfaces && !found; ++i) {
            const struct libusb_interface *inter = &config->interface[i];
            for (int j = 0; j < inter->num_altsetting && !found; ++j) {
                const struct libusb_interface_descriptor *alt = &inter->altsetting[j];
                endpoint_t candidate = {0};
                /* No alternate-setting commands are authorized by this lifecycle. */
                if (alt->bAlternateSetting != 0 || alt->bNumEndpoints != 2) continue;
                for (uint8_t k = 0; k < alt->bNumEndpoints; ++k) {
                    const struct libusb_endpoint_descriptor *e = &alt->endpoint[k];
                    if ((e->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                    if (e->bEndpointAddress & LIBUSB_ENDPOINT_IN) candidate.addr_in = e->bEndpointAddress;
                    else candidate.addr_out = e->bEndpointAddress;
                }
                if (candidate.addr_in && candidate.addr_out) {
                    candidate.intf_num = alt->bInterfaceNumber;
                    *ep = candidate;
                    found = TRUE;
                }
            }
        }
        libusb_free_config_descriptor(config);
        return found ? LIBUSB_SUCCESS : LIBUSB_ERROR_NOT_FOUND;
    }
    return LIBUSB_ERROR_NO_DEVICE;
}

/*
  Error map, convert a libusb error code value to a PassThru error code.
*/
static int error_map(const int usb_err)
{
	switch (usb_err) {
	case LIBUSB_SUCCESS:
		return J2534_NOERROR;
	case LIBUSB_ERROR_IO:
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	case LIBUSB_ERROR_INVALID_PARAM:
		return J2534_ERR_FAILED;
	case LIBUSB_ERROR_ACCESS:
		return J2534_ERR_DEVICE_IN_USE;
	case LIBUSB_ERROR_NO_DEVICE:
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	case LIBUSB_ERROR_NOT_FOUND:
		return J2534_ERR_DEVICE_NOT_CONNECTED;
	case LIBUSB_ERROR_BUSY:
		return J2534_ERR_DEVICE_IN_USE;
	case LIBUSB_ERROR_TIMEOUT:
		return J2534_ERR_TIMEOUT;
	case LIBUSB_ERROR_OVERFLOW:
		return J2534_ERR_BUFFER_OVERFLOW;
	case LIBUSB_ERROR_PIPE:
		return J2534_ERR_FAILED;
	case LIBUSB_ERROR_INTERRUPTED:
		return J2534_ERR_FAILED;
	case LIBUSB_ERROR_NO_MEM:
		return J2534_ERR_EXCEEDED_LIMIT;
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return J2534_ERR_NOT_SUPPORTED;
	case LIBUSB_ERROR_OTHER:
		return J2534_ERR_FAILED;
	default:
		return usb_err;
	}
}

/*
	Check if characters converted correctly to a number
*/
static char *next_token(char *text,char **state) {
    char *p=text?text:*state;if(!p)return NULL;
    p+=strspn(p,DELIMITERS);if(!*p){*state=NULL;return NULL;}
    char *end=p+strcspn(p,DELIMITERS);
    if(*end){*end=0;*state=end+1;}else *state=NULL;
    return p;
}
static int parse_decimal(const char *text,unsigned long *value) {
    if(!text || text[0]<'0' || text[0]>'9')return FALSE;
    char *end=NULL;errno=0;unsigned long parsed=strtoul(text,&end,10);
    if(errno || !end || *end){snprintf((char *)LAST_ERROR,LE_LEN,"Invalid numeric adapter response");return FALSE;}
    *value=parsed;return TRUE;
}

/*
  Add a PT message to the receive FIFO queue
*/
static int queue_msg(PASSTHRU_MSG *mBuf)
{
	fifo_msg_t *new_msg = (fifo_msg_t*)malloc(sizeof(fifo_msg_t));
	if (new_msg)
	{
		new_msg->pt_msg = mBuf;
		new_msg->next_in_q = NULL;
		if (fifo_head)
		{
			fifo_msg_t *temp = fifo_head;
			// find last in queue
			while (temp->next_in_q != NULL)
				temp = temp->next_in_q;
			// assign the new msg to last in queue
			temp->next_in_q = new_msg;
		}
		else
		{
			// new msg is now the first in the queue
			fifo_head = new_msg;
		}
		if (write_log)
			writelog("\tNew message queued\n");
		return TRUE;
	}
	return FALSE;
}

/*
  Read a PT message from the receive FIFO queue.
*/
static int read_queue_msg(PASSTHRU_MSG *mBuf)
{
	fifo_msg_t *temp = fifo_head;
	if (temp)
	{
		fifo_head = fifo_head->next_in_q;
		if (write_log)
			writelog("\tMessage dequeued\n");

		if (temp->pt_msg)
		{
			memcpy(mBuf, temp->pt_msg, sizeof(PASSTHRU_MSG));
			free(temp->pt_msg);
			free(temp);
			if (write_log)
				writelogpassthrumsg(mBuf);
			return TRUE;
		}
		else
			if (write_log)
				writelog("\tPT msg is NULL\n");
	}
	return FALSE;
}

/*
  Flush the receive FIFO queue.
*/
static void flush_queue()
{
	fifo_msg_t *current = NULL;
	while (fifo_head)
	{
		current = fifo_head;
		if (current->pt_msg)
			free(current->pt_msg);
		fifo_head = current->next_in_q;
		free(current);
	}
	if (write_log)
		writelog("\tReceive FIFO queue flushed\n");
}

/*
  Serach through data to find a pattern match.
  Return offset in data where first match is found or -1
  if not found.
*/

/*
  Send data and expect to receive a reply, using specified timeout.
  If expect is NULL then command is acknowledged by aro response.
*/
static uint64_t lifecycle_clock_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}
#include "receive_stream.inc"

static int usb_send_expect(uint8_t *data,const size_t length,const int capacity,
                           const uint32_t timeout,const uint8_t *expect)
{
    if(!data || !length || capacity<=0 || length>(size_t)capacity) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Invalid USB command length");return J2534_ERR_INVALID_MSG;
    }
    if(receive_fault)return receive_fault;
    uint64_t deadline=lifecycle_clock_ms()+(timeout?timeout:1);
    unsigned long before=command_serial;int actual=0;
    command_waiting=timeout!=0;command_matched=0;command_error=0;command_expect=expect;
    int r=libusb_bulk_transfer(con->dev_handle,endpoint->addr_out,data,(int)length,&actual,remaining_ms(deadline)?remaining_ms(deadline):1);
    if(write_log){writelog("\tUSB stream Sent:\n\t\t");if(actual>0 && actual<=(int)length)writelogmsg(data,0,(unsigned long)actual);writelog("\n");}
    if(r || actual!=(int)length) {
        snprintf((char *)LAST_ERROR,LE_LEN,"USB command send failed/short: %d",r);command_waiting=0;return r?r:LIBUSB_ERROR_IO;
    }
    if(!timeout){command_waiting=0;return LIBUSB_SUCCESS;}
    while(remaining_ms(deadline)) {
        r=receive_usb(remaining_ms(deadline));
        if(r && r!=LIBUSB_ERROR_TIMEOUT){command_waiting=0;return r;}
        if(command_serial!=before) {
            if(command_error){command_waiting=0;return command_error;}
            if(control_matches(command_reply,expect)) {
                size_t n=strlen(command_reply);
                if(n>=(size_t)capacity){command_waiting=0;return J2534_ERR_BUFFER_OVERFLOW;}
                memcpy(data,command_reply,n+1);command_waiting=0;return LIBUSB_SUCCESS;
            }
            before=command_serial;
        }
        if(r==LIBUSB_ERROR_TIMEOUT)break;
    }
    command_waiting=0;snprintf((char *)LAST_ERROR,LE_LEN,"USB acknowledgment deadline expired");return LIBUSB_ERROR_TIMEOUT;
}

/*
  Establish a connection with a PassThru device.
 */
/* USB-only lifecycle contract, revision 1. Diagnostic markers are emitted only
 * after the associated operation returns. They are evidence of driver/libusb
 * results, not independent observation of Windows kernel handles. */
static int usb_context_acquired = FALSE;
static int usb_interface_acquired = FALSE;
static int usb_device_open = FALSE;

static void lifecycle_event(const char *name, int status)
{
    if (write_log) {
        fprintf(logfile, "USBQUAL/1 %s=%d\n", name, status);
        fflush(logfile);
    }
}
static void finish_lifecycle_log(void)
{
    if (logfile) { fflush(logfile); fclose(logfile); }
    logfile = NULL;
    write_log = FALSE;
}
static int release_usb_resources(void)
{
    int release_status = LIBUSB_SUCCESS;
    if (usb_interface_acquired) {
        release_status = libusb_release_interface(con->dev_handle, endpoint->intf_num);
        lifecycle_event("release_status", release_status);
        usb_interface_acquired = FALSE;
    }
    if (con->dev_handle) {
        libusb_close(con->dev_handle); /* void API: only return can be observed */
        con->dev_handle = NULL;
        lifecycle_event("handle_close_returned", 1);
    }
    if (usb_context_acquired) {
        libusb_exit(con->ctx); /* void API */
        lifecycle_event("context_exit_returned", 1);
        usb_context_acquired = FALSE;
    }
    reset_receive();con->protocol_id=0;con->channel=0;
    con->ctx = NULL;
    con->device_id = 0;
    memset(endpoint, 0, sizeof(*endpoint));
    usb_device_open = FALSE;
    fw_version[0] = '\0';
    return release_status;
}

#define PassThruOpen impl_PassThruOpen
#define PassThruClose impl_PassThruClose
#define PassThruConnect impl_PassThruConnect
#define PassThruDisconnect impl_PassThruDisconnect
#define PassThruReadMsgs impl_PassThruReadMsgs
#define PassThruWriteMsgs impl_PassThruWriteMsgs
#define PassThruStartPeriodicMsg impl_PassThruStartPeriodicMsg
#define PassThruStopPeriodicMsg impl_PassThruStopPeriodicMsg
#define PassThruStartMsgFilter impl_PassThruStartMsgFilter
#define PassThruStopMsgFilter impl_PassThruStopMsgFilter
#define PassThruSetProgrammingVoltage impl_PassThruSetProgrammingVoltage
#define PassThruReadVersion impl_PassThruReadVersion
#define PassThruGetLastError impl_PassThruGetLastError
#define PassThruIoctl impl_PassThruIoctl
int32_t PassThruOpen(const void *pName, unsigned long *pDeviceID)
{
    (void)pName;
    if (!pDeviceID) {
        snprintf(LAST_ERROR, LE_LEN, "PassThruOpen: pDeviceID must not be NULL");
        return J2534_ERR_NULL_PARAMETER;
    }
    if (usb_device_open || usb_context_acquired || con->dev_handle) {
        snprintf(LAST_ERROR, LE_LEN, "PassThruOpen: device already owned; no retry");
        return J2534_ERR_DEVICE_IN_USE;
    }
    const char *le = getenv("LOG_ENABLE");
    logfile = NULL; write_log = FALSE;
    if (le && le[0] && le[0] != '0') {
        logfile = fopen(le, "a");
        if (logfile) { setvbuf(logfile, NULL, _IONBF, 0); write_log = TRUE; }
    }
    reset_receive();con->protocol_id=0;con->channel=0;
    lifecycle_event("begin", 1);
    littleEndian = isLittleEndian();
    fw_version[0] = '\0';
    memset(endpoint, 0, sizeof(*endpoint));
    con->ctx = NULL; con->dev_handle = NULL;
    const char *stage = "libusb_init";
    int r = libusb_init(&con->ctx);
    if (r != LIBUSB_SUCCESS) goto OPEN_FAILED;
    usb_context_acquired = TRUE;
    libusb_device **devs = NULL;
    stage = "device_list";
    ssize_t count = libusb_get_device_list(con->ctx, &devs);
    if (count < 0) { r = (int)count; goto OPEN_FAILED; }
    stage = "USB open/endpoints";
    r = open_dev_endpoints(devs, count, VENDOR_ID, PRODUCT_ID, endpoint);
    libusb_free_device_list(devs, 1);
    if (r != LIBUSB_SUCCESS) goto OPEN_FAILED;
    stage = "claim_interface";
    r = libusb_claim_interface(con->dev_handle, endpoint->intf_num);
    if (r != LIBUSB_SUCCESS) goto OPEN_FAILED;
    usb_interface_acquired = TRUE;
    lifecycle_event("interface_claimed", 1);
    uint8_t data[MAX_LEN] = {0};
    stage = "ati";
    strcpy((char *)data, "\r\n\r\nati\r\n");
    r = usb_send_expect(data, strlen((char *)data), MAX_LEN, 2000, (const uint8_t *)"ari ");
    if (r != LIBUSB_SUCCESS) goto OPEN_FAILED;
    /* Firmware parser expects a colon, matching upstream's version reply. */
    data[MAX_LEN - 1] = '\0';
    memcpy(fw_version, data, sizeof(fw_version));
    lifecycle_event("ati_ack", 1);
    stage = "ata";
    strcpy((char *)data, "ata\r\n");
    r = usb_send_expect(data, strlen((char *)data), MAX_LEN, 2000, NULL);
    if (r != LIBUSB_SUCCESS) goto OPEN_FAILED;
    lifecycle_event("ata_ack", 1);
    usb_device_open = TRUE;
    *pDeviceID = con->device_id;
    LAST_ERROR[0] = '\0';
    lifecycle_event("open_complete", 1);
    return J2534_NOERROR;
OPEN_FAILED:
    {
        char detail[LE_LEN];
        if (stage[0] == 'a') snprintf(detail, sizeof(detail), "%s", LAST_ERROR);
        else snprintf(detail, sizeof(detail), "%s", libusb_error_name(r));
        snprintf(LAST_ERROR, LE_LEN, "PassThruOpen %s failed: %.40s", stage, detail);
        if (write_log) { writelog((const char *)LAST_ERROR); writelog("\n"); }
        int first = error_map(r);
        lifecycle_event("open_status", first);
        /* No atz/ata or other recovery command on an unsuccessful Open. */
        release_usb_resources();
        finish_lifecycle_log();
        return first;
    }
}

int32_t PassThruClose(const unsigned long DeviceID)
{
    if (!usb_device_open || !con->dev_handle || DeviceID != con->device_id) {
        snprintf(LAST_ERROR, LE_LEN, "PassThruClose: invalid or already closed device");
        return J2534_ERR_INVALID_DEVICE_ID;
    }
    uint8_t data[MAX_LEN] = {0};
    strcpy((char *)data, "atz\r\n");
    receive_fault=0;wire_used=0;assembling_active=0;
    int shutdown = usb_send_expect(data, strlen((char *)data), MAX_LEN, 2000, NULL);
    lifecycle_event("shutdown_ack", shutdown == LIBUSB_SUCCESS ? 1 : 0);
    int first = error_map(shutdown);
    char detail[LE_LEN];
    snprintf(detail, sizeof(detail), "%s", LAST_ERROR);
    int released = release_usb_resources();
    if (shutdown != LIBUSB_SUCCESS)
        snprintf(LAST_ERROR, LE_LEN, "PassThruClose atz failed: %.45s", detail);
    else if (released != LIBUSB_SUCCESS) {
        first = error_map(released);
        snprintf(LAST_ERROR, LE_LEN, "PassThruClose release failed: %s", libusb_error_name(released));
    } else LAST_ERROR[0] = '\0';
    if (first && write_log) { writelog((const char *)LAST_ERROR); writelog("\n"); }
    lifecycle_event("close_status", first);
    finish_lifecycle_log();
    return first;
}

/*
  Establish a connection using a protocol channel.
 */
int32_t PassThruConnect(const unsigned long DeviceID,const unsigned long protocolID,
    const unsigned long flags,const unsigned long baud,unsigned long *pChannelID)
{
    if(!pChannelID)return J2534_ERR_NULL_PARAMETER;
    if(!usb_device_open || DeviceID!=con->device_id)return J2534_ERR_INVALID_DEVICE_ID;
    if(con->protocol_id)return J2534_ERR_CHANNEL_IN_USE;
    if(protocolID<3 || protocolID>6)return J2534_ERR_INVALID_PROTOCOL_ID;
    uint8_t data[MAX_LEN];snprintf((char *)data,sizeof(data),"ato%lu %lu %lu 0\r\n",protocolID,flags,baud);
    int r=usb_send_expect(data,strlen((char *)data),sizeof(data),2000,NULL);
    if(r){receive_fault=J2534_ERR_FAILED;return error_map(r);}
    reset_receive();con->channel=(int8_t)(0x30+protocolID);con->protocol_id=protocolID;
    *pChannelID=protocolID;return J2534_NOERROR;
}

/*
  Terminate a connection with a protocol channel.
 */
int32_t PassThruDisconnect(const unsigned long ChannelID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN, "Disconnecting\n\t|\n\tChannelID: %lu\n", ChannelID);
		writelog(log_msg);
	}

	if (!usb_device_open || !con->protocol_id || ChannelID != con->protocol_id)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid ChannelID");
		return J2534_ERR_INVALID_CHANNEL_ID;
	}

	// If any messages in the FIFO queue delete them
	flush_queue();

	uint8_t data[MAX_LEN];
	receive_fault=0;wire_used=0;assembling_active=0;
	snprintf(data, MAX_LEN, "atc%lu\r\n", ChannelID);
	int r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);

	if (write_log && r == LIBUSB_SUCCESS)
		writelog("Disconnected\n");
    if(!r){reset_receive();con->protocol_id=0;con->channel=0;}
	return error_map(r);
}

/*
  Read message(s) from a protocol channel.
 */
int32_t PassThruReadMsgs(const unsigned long ChannelID,PASSTHRU_MSG *pMsg,
    unsigned long *pNumMsgs,const unsigned long Timeout)
{
    if(!pNumMsgs)return J2534_ERR_NULL_PARAMETER;
    unsigned long requested=*pNumMsgs;*pNumMsgs=0;
    if(!pMsg)return J2534_ERR_NULL_PARAMETER;
    if(!usb_device_open || !con->protocol_id || ChannelID!=con->protocol_id)return J2534_ERR_INVALID_CHANNEL_ID;
    if(receive_fault)return receive_fault;
    if(!requested)return J2534_NOERROR;
    uint64_t deadline=lifecycle_clock_ms()+(Timeout?Timeout:1);
    int r=LIBUSB_SUCCESS;
    for(;;) {
        while(*pNumMsgs<requested && pop_message(pMsg+*pNumMsgs))++*pNumMsgs;
        if(*pNumMsgs==requested)return J2534_NOERROR;
        unsigned int wait=remaining_ms(deadline);if(!wait)break;
        r=receive_usb(wait);
        if(r && r!=LIBUSB_ERROR_TIMEOUT)return error_map(r);
        if(r==LIBUSB_ERROR_TIMEOUT) {
            while(*pNumMsgs<requested && pop_message(pMsg+*pNumMsgs))++*pNumMsgs;
            if(*pNumMsgs==requested)return J2534_NOERROR;
            break;
        }
    }
    if(Timeout && (wire_used || assembling_active || awaiting_response))
        stream_failure(J2534_ERR_FAILED,"Receive deadline left request/record incomplete");
    if(write_log)for(unsigned long i=0;i<*pNumMsgs;++i)writelogpassthrumsg(pMsg+i);
    return Timeout?J2534_ERR_TIMEOUT:J2534_ERR_BUFFER_EMPTY;
}

/*
  Write message(s) to a protocol channel.
 */
int32_t PassThruWriteMsgs(const unsigned long ChannelID,const PASSTHRU_MSG *pMsg,
    unsigned long *pNumMsgs,const unsigned long Timeout)
{
    if(!pNumMsgs)return J2534_ERR_NULL_PARAMETER;
    unsigned long requested=*pNumMsgs;*pNumMsgs=0;
    if(!pMsg)return J2534_ERR_NULL_PARAMETER;
    if(!usb_device_open || !con->protocol_id || ChannelID!=con->protocol_id)return J2534_ERR_INVALID_CHANNEL_ID;
    if(receive_fault)return receive_fault;
    uint64_t deadline=lifecycle_clock_ms()+(Timeout?Timeout:1);
    for(unsigned long i=0;i<requested;++i) {
        if(!pMsg[i].DataSize || pMsg[i].DataSize>PM_DATA_LEN ||
           (pMsg[i].ProtocolID && pMsg[i].ProtocolID!=con->protocol_id))return J2534_ERR_INVALID_MSG;
        if((con->channel==CAN || con->channel==ISO15765) && pMsg[i].DataSize<4)return J2534_ERR_INVALID_MSG;
        uint8_t data[PM_DATA_LEN+MAX_LEN];
        int header=snprintf((char *)data,MAX_LEN,"att%lu %lu %lu\r\n",ChannelID,pMsg[i].DataSize,pMsg[i].TxFlags);
        if(header<0 || header>=MAX_LEN)return J2534_ERR_INVALID_MSG;
        memcpy(data+header,pMsg[i].Data,pMsg[i].DataSize);
        unsigned int wait=remaining_ms(deadline);if(!wait)return J2534_ERR_TIMEOUT;
        int actual=0,total=header+(int)pMsg[i].DataSize;
        int r=libusb_bulk_transfer(con->dev_handle,endpoint->addr_out,data,total,&actual,wait);
        if(write_log){writelogpassthrumsg(pMsg+i);writelog("\tUSB stream Sent:\n\t\t");if(actual>0 && actual<=total)writelogmsg(data,0,(unsigned long)actual);writelog("\n");}
        if(r || actual!=total) {
            snprintf((char *)LAST_ERROR,LE_LEN,"USB message send failed/short: %d",r);
            if(actual>0)stream_failure(J2534_ERR_FAILED,"Partial USB transmit; state uncertain");
            return r?error_map(r):J2534_ERR_FAILED;
        }
        ++*pNumMsgs;awaiting_response=1;
    }
    return J2534_NOERROR;
}

/*
  Start sending a message at a specified time interval on a protocol channel.
 */
int32_t PassThruStartPeriodicMsg(const unsigned long ChannelID, const PASSTHRU_MSG *pMsg,
	const unsigned long *pMsgID, const unsigned long timeInterval)
{
	if (write_log)
		writelog("StartPeriodic, not supported\n");
	return J2534_ERR_NOT_SUPPORTED;
}

/*
  Stop a periodic message.
 */
int32_t PassThruStopPeriodicMsg(const unsigned long ChannelID, const unsigned long msgID)
{
	if (write_log)
		writelog("StopPeriodic, not supported\n");
	return J2534_ERR_NOT_SUPPORTED;
}

/*
  Start filtering incoming messages on a protocol channel.
 */
int32_t PassThruStartMsgFilter(const unsigned long ChannelID, const unsigned long FilterType,
	const PASSTHRU_MSG *pMaskMsg, const PASSTHRU_MSG *pPatternMsg,
	const PASSTHRU_MSG *pFlowControlMsg, unsigned long *pMsgID)
{
    char *token_state=NULL;
    if(!pMaskMsg || !pPatternMsg || !pMsgID)return J2534_ERR_NULL_PARAMETER;
    if(!usb_device_open || !con->protocol_id || ChannelID!=con->protocol_id)return J2534_ERR_INVALID_CHANNEL_ID;
    if(FilterType<1 || FilterType>3)return J2534_ERR_INVALID_FLAGS;
    if(!pMaskMsg->DataSize || pMaskMsg->DataSize>12 || pPatternMsg->DataSize!=pMaskMsg->DataSize)return J2534_ERR_INVALID_MSG;
    if(FilterType==3 && (!pFlowControlMsg || pFlowControlMsg->DataSize!=pMaskMsg->DataSize ||
        pFlowControlMsg->DataSize>12 || pFlowControlMsg->TxFlags!=pMaskMsg->TxFlags))return J2534_ERR_INVALID_MSG;

	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"StartMsgFilter\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tFilterType:\t%lu\n"
			"\tpMaskMsg:\n",
			ChannelID, FilterType);
		writelog(log_msg);
		writelogpassthrumsg(pMaskMsg);
		writelog("\n\tpPatternMsg:\n");
		writelogpassthrumsg(pPatternMsg);
		writelog("\n\tpFlowControlMsg:\n");
		if (pFlowControlMsg == NULL)
			writelog("\tNULL");
		else
			writelogpassthrumsg(pFlowControlMsg);
		writelog("\n");
	}

	if (pMaskMsg == NULL || pPatternMsg == NULL)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: PASSTHRU_MSG* must not be NULL");
		return J2534_ERR_NULL_PARAMETER;
	}
	if (pMaskMsg->DataSize > 12 || pPatternMsg->DataSize > 12)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: PASSTHRU_MSG invalid data length");
		return J2534_ERR_INVALID_MSG;
	}
	if (pMaskMsg->DataSize != pPatternMsg->DataSize)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Mask and Pattern have differnt data lenghts");
		return J2534_ERR_INVALID_MSG;
	}
	if (pMaskMsg->TxFlags != pPatternMsg->TxFlags)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Mask and Pattern have differnt TX flags");
		return J2534_ERR_INVALID_MSG;
	}
	if ((FilterType == J2534_PASS_FILTER || FilterType == J2534_BLOCK_FILTER)
		&& pFlowControlMsg)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: FilterType, FlowControlMsg mismatch");
		return J2534_ERR_INVALID_MSG;
	}

	uint8_t data[MAX_LEN];
	snprintf(data, MAX_LEN, "atf%lu %lu %lu %lu\r\n", ChannelID,
		FilterType, pMaskMsg->TxFlags, pMaskMsg->DataSize);

    if(strlen((char *)data)+pMaskMsg->DataSize+pPatternMsg->DataSize+(pFlowControlMsg?pFlowControlMsg->DataSize:0)>=sizeof(data))return J2534_ERR_INVALID_MSG;
	// append the mask, pattern and flow control bytes keeping track of the final byte count
	size_t j = 0;
	size_t datalen = strlen(data);
	size_t i = datalen;
	datalen = datalen + pMaskMsg->DataSize;
	while (i < datalen)
		data[i++] = pMaskMsg->Data[j++];

	datalen = i + pPatternMsg->DataSize;
	j = 0;
	while (i < datalen)
		data[i++] = pPatternMsg->Data[j++];

	if (pFlowControlMsg)
	{
		datalen = i + pFlowControlMsg->DataSize;
		j = 0;
		while (i < datalen)
			data[i++] = pFlowControlMsg->Data[j++];
	}

	int r = usb_send_expect(data, i, MAX_LEN, 2000, "arf");

	if(r)return error_map(r);
	int failed = FALSE;
	int8_t *word = next_token((char *)data,&token_state);
	if (word)
	{
		word = next_token(NULL,&token_state);
		if (word)
		{
			unsigned long lval=0;
            if (parse_decimal(word,&lval))
				*pMsgID = lval;
			else
				failed = TRUE;
		}
		else
			failed = TRUE;
	}
	else
		failed = TRUE;

	if (failed)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: failed to parse reply");
		r = J2534_ERR_FAILED;
	}

	if (write_log)
		writelog("EndStartMsgFilter\n");
	return error_map(r);
}

/*
  Stops filtering incoming messages on a protocol channel.
 */
int32_t PassThruStopMsgFilter(const unsigned long ChannelID, const unsigned long msgID)
{
	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"StopMsgFilter\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tmsgID:\t\t%lu\n",
			ChannelID, msgID);
		writelog(log_msg);
	}

	int r = J2534_NOERROR;
	if (!usb_device_open || !con->protocol_id || ChannelID != con->protocol_id)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Invalid ChannelID");
		r = J2534_ERR_INVALID_CHANNEL_ID;
	}
	else
	{
		uint8_t data[MAX_LEN];
		snprintf(data, MAX_LEN, "atk%lu %lu\r\n", ChannelID, msgID);
		r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);
	}
	if (write_log)
		writelog("EndStopMsgFilter\n");
	return error_map(r);
}

/*
  Set a programming voltage on a specific pin.
 */
int32_t PassThruSetProgrammingVoltage(const unsigned long DeviceID,
	const unsigned long pinNumber, const unsigned long voltage)
{
	if (write_log)
		writelog("SetProgrammingVoltage, not support\n");
	return J2534_ERR_NOT_SUPPORTED;
}

/*
  Reads the version information of the firmware, this DLL and API implmentation.
 */
int32_t PassThruReadVersion(const unsigned long DeviceID, char *pFirmwareVersion,
	char *pDllVersion, char *pApiVersion)
{
    if(!usb_device_open || DeviceID!=con->device_id)return J2534_ERR_INVALID_DEVICE_ID;
    char *token_state=NULL;
	if (pFirmwareVersion == NULL || pDllVersion == NULL
		|| pApiVersion == NULL)
	{
		snprintf(LAST_ERROR, LE_LEN, "Error: Version* must not be NULL");
		return J2534_ERR_NULL_PARAMETER;
	}

	char dll_ver[MAX_LEN];

#ifdef GET_LIBUSB_VERSION
	// version info only available for libusb 1.0.10 and later
	const struct libusb_version *ver = libusb_get_version();
	snprintf(dll_ver, MAX_LEN, "%s (libusb-%u.%u.%u.%u%s)", DLL_VERSION,
		ver->major, ver->minor, ver->micro, ver->nano, ver->rc);
#else
	snprintf(dll_ver, MAX_LEN, "%s", DLL_VERSION);
#endif
	int failed = FALSE;
	if (fw_version[0] != 0)
	{
		char firmware_copy[MAX_LEN];memcpy(firmware_copy,fw_version,sizeof(firmware_copy));
        char *pos = strrchr(firmware_copy, ':');
		if (pos)
		{
			char *word = next_token(pos + 1,&token_state);
			if (word)
				strcpy(pFirmwareVersion, word);
			else
				failed = TRUE;
		}
		else
			failed = TRUE;
	}
	if (failed)
		strcpy(pFirmwareVersion, "unavailable");

	strcpy(pDllVersion, dll_ver);
	strcpy(pApiVersion, API_VERSION);

	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"ReadVersion\n\t|\n"
			"\tfwVer : %s\n"
			"\tlibVer: %s\n"
			"\tapiVer: %s\n"
			"EndReadVersion\n",
			pFirmwareVersion, pDllVersion, pApiVersion);
		writelog(log_msg);
	}
	return J2534_NOERROR;
}

/*
  Gets the text description of the last error.
 */
int32_t PassThruGetLastError(char *pErrorDescription)
{
	if (write_log)
		writelog("GetLastError\n\t|\n\tErrorDescription:\t");
	if (pErrorDescription == NULL)
	{
		if (write_log)
			writelog("NULL");
		return J2534_ERR_NULL_PARAMETER;
	}
	snprintf(pErrorDescription, LE_LEN, "%s", LAST_ERROR);
	if (write_log)
	{
		writelog(pErrorDescription);
		writelog("\nEndGetLastError\n");
	}
	return J2534_NOERROR;
}

/*
  General I/O control functions for reading and writing
  protocol configuration parameters (e.g. initialization,
  baud rates, programming voltages, etc.).
 */
int32_t PassThruIoctl(const unsigned long ChannelID, const unsigned long ioctlID,
	const void *pInput, void *pOutput)
{
    char *token_state=NULL;
    if(!usb_device_open || !con->protocol_id || ChannelID!=con->protocol_id)return J2534_ERR_INVALID_CHANNEL_ID;
    if((ioctlID==J2534_GET_CONFIG || ioctlID==J2534_SET_CONFIG) &&
        (!pInput || !((const SCONFIG_LIST *)pInput)->ConfigPtr))return J2534_ERR_NULL_PARAMETER;
    if(ioctlID==J2534_READ_VBATT && !pOutput)return J2534_ERR_NULL_PARAMETER;
    if(ioctlID==J2534_GET_CONFIG || ioctlID==J2534_SET_CONFIG) {
        unsigned long n=((const SCONFIG_LIST *)pInput)->NumOfParams;
        if(!n)return J2534_NOERROR;
        if(n>256)return J2534_ERR_EXCEEDED_LIMIT;
    }

	if (write_log)
	{
		snprintf(log_msg, LM_LEN,
			"Ioctl\n\t|\n"
			"\tChannelID:\t%lu\n"
			"\tioctlID:\t%lu ",
			ChannelID, ioctlID);
		writelog(log_msg);
	}
	uint8_t data[MAX_LEN];
	size_t strln = 0;
	uint32_t i = 0, par_cnt = 0, bytes_read = 0;
	int r = LIBUSB_ERROR_NOT_SUPPORTED;
	if (ioctlID == J2534_GET_CONFIG)
	{
		const SCONFIG_LIST *inputlist = pInput;
		if (write_log)
		{
			snprintf(log_msg, LM_LEN,
				"[Config GET]\n\tNumOfParams: %lu\n",
				inputlist->NumOfParams);
			writelog(log_msg);
		}
		SCONFIG *cfgitem;
		par_cnt = inputlist->NumOfParams;
		for (i = 0; i < par_cnt; ++i)
		{
			cfgitem = &inputlist->ConfigPtr[i];
			snprintf(data, MAX_LEN, "atg%lu %lu\r\n", ChannelID, cfgitem->Parameter);
			r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, "arg");
            if(r)goto EXIT_IOCTL;

            char *token=next_token((char *)data,&token_state);
            unsigned long parameter=0,value=0;
            if(!token || strcmp(token,"arg") || !parse_decimal(next_token(NULL,&token_state),&parameter) ||
               !parse_decimal(next_token(NULL,&token_state),&value) || next_token(NULL,&token_state) || parameter!=cfgitem->Parameter) {
                snprintf((char *)LAST_ERROR,LE_LEN,"GET_CONFIG echo/format mismatch");r=J2534_ERR_INVALID_MSG;goto EXIT_IOCTL;
            }
            cfgitem->Value=value;

		}
	}
	if (ioctlID == J2534_SET_CONFIG)
	{
		const SCONFIG_LIST *inputlist = pInput;
		if (write_log)
		{
			snprintf(log_msg, LM_LEN,
				"[Config SET]\n\tNumOfParams: %lu\n",
				inputlist->NumOfParams);
			writelog(log_msg);
		}
		SCONFIG *cfgitem;
		par_cnt = inputlist->NumOfParams;
		for (i = 0; i < par_cnt; ++i)
		{
			cfgitem = &inputlist->ConfigPtr[i];
			snprintf(data, MAX_LEN, "ats%lu %lu %lu\r\n", ChannelID, cfgitem->Parameter, cfgitem->Value);
			if (write_log)
			{
				snprintf(log_msg, LM_LEN,
					"\t\tConfigItem(p,v): %02lX, %02lX\n",
					cfgitem->Parameter, cfgitem->Value);
				writelog(log_msg);
			}
			r = usb_send_expect(data, strlen(data), MAX_LEN, 2000, NULL);
            if(r)goto EXIT_IOCTL;
		}
	}
    if(ioctlID==J2534_READ_VBATT) {
        snprintf((char *)data,MAX_LEN,"atr 16\r\n");
        r=usb_send_expect(data,strlen((char *)data),MAX_LEN,2000,(const uint8_t *)"arr ");
        if(r)goto EXIT_IOCTL;
        char *token=next_token((char *)data,&token_state);unsigned long pin=0,value=0;
        if(!token || strcmp(token,"arr") || !parse_decimal(next_token(NULL,&token_state),&pin) || pin!=16 ||
           !parse_decimal(next_token(NULL,&token_state),&value) || next_token(NULL,&token_state) || value>UINT32_MAX) {
            snprintf((char *)LAST_ERROR,LE_LEN,"READ_VBATT echo/format mismatch");r=J2534_ERR_INVALID_MSG;goto EXIT_IOCTL;
        }
        *(uint32_t *)pOutput=(uint32_t)value;
    }

	if (ioctlID == J2534_FAST_INIT)
	{
		if (pInput == NULL || pOutput == NULL)
		{
			snprintf(LAST_ERROR, LE_LEN, "Error: pInput/Output must not be NULL");
			r = J2534_ERR_NULL_PARAMETER;
			goto EXIT_IOCTL;
		}

		const PASSTHRU_MSG *pMsg = pInput;
		unsigned long len = pMsg->DataSize;
		if (len > 0 && len <= MAX_LEN)
		{
			if (write_log)
			{
				writelog("[FAST INIT]\n");
				writelogpassthrumsg(pMsg);
			}
			snprintf(data, MAX_LEN, "aty%lu %lu 0\r\n", ChannelID, pMsg->DataSize);
			strln = strlen(data);
			for (i = 0; i < len; ++i)
				data[strln++] = pMsg->Data[i];

			r = usb_send_expect(data, strln, MAX_LEN, 2000, "ary");
			if (r != LIBUSB_SUCCESS)
				goto EXIT_IOCTL;

			len = strtoul(data + 5, NULL, 10);
			if (len == 0)
			{
				snprintf(LAST_ERROR, LE_LEN, "Error: failed to convert to long");
				r = J2534_ERR_FAILED;
				goto EXIT_IOCTL;
			}

			r = libusb_bulk_transfer(con->dev_handle, endpoint->addr_in,
				data, MAX_LEN, &bytes_read, 500);
			if (r != LIBUSB_SUCCESS)
			{
				snprintf(LAST_ERROR, LE_LEN, "Error: failed to read timing: %s",
					libusb_error_name(r));
				goto EXIT_IOCTL;
			}

			PASSTHRU_MSG *pOutMsg = pOutput;
			pOutMsg->DataSize = 0;
			if(len>bytes_read || len>MAX_LEN || len>PM_DATA_LEN){r=J2534_ERR_INVALID_MSG;goto EXIT_IOCTL;}
            memcpy(pOutMsg->Data,data,len);
			pOutMsg->DataSize = len;
			pOutMsg->ExtraDataIndex = len;
			pOutMsg->RxStatus = 0;
			pOutMsg->ProtocolID = con->protocol_id;
			if (write_log)
				writelogpassthrumsg(pOutMsg);
		}
		else
		{
			if (write_log)
			{
				snprintf(log_msg, LM_LEN,
					"\tInvalid message size: %lu\n", len);
				writelog(log_msg);
			}
			snprintf(LAST_ERROR, LE_LEN, "Invalid message size: %lu", len);
			return J2534_ERR_INVALID_MSG;
		}
	}
	if (ioctlID == J2534_CLEAR_TX_BUFFER)
	{
		if (write_log)
			writelog("[CLEAR_TX_BUFFER]\n");
		snprintf((char *)LAST_ERROR,LE_LEN,"Adapter TX purge is not implemented");
        r = LIBUSB_ERROR_NOT_SUPPORTED;
	}
	if (ioctlID == J2534_CLEAR_RX_BUFFER)
	{
		if (write_log)
			writelog("[CLEAR_RX_BUFFER]\n");

        // A local reset is not an adapter purge. Drain bytes without publishing
        // them, bound waiting, and retain faults that require reconnection.
        if(receive_fault){r=receive_fault;goto EXIT_IOCTL;}
        if(awaiting_response || assembling_active || wire_used) {
            r=stream_failure(J2534_ERR_FAILED,"Cannot purge an unfinished request");goto EXIT_IOCTL;
        }
        reset_receive();
        uint64_t deadline=lifecycle_clock_ms()+100;
        uint8_t discard[PM_DATA_LEN];int actual=0;
        do {
            r=libusb_bulk_transfer(con->dev_handle,endpoint->addr_in,discard,sizeof(discard),&actual,remaining_ms(deadline)?remaining_ms(deadline):1);
            if(r==LIBUSB_ERROR_TIMEOUT && actual==0){r=LIBUSB_SUCCESS;break;}
            if(r && r!=LIBUSB_ERROR_TIMEOUT)break;
            r=LIBUSB_ERROR_TIMEOUT;
        }while(remaining_ms(deadline));
        if(r)stream_failure(J2534_ERR_FAILED,"Receive purge did not reach a quiet boundary");

	}

	EXIT_IOCTL:
	if (write_log)
		writelog("EndIoctl\n");
	return error_map(r);
}

#undef PassThruOpen
#undef PassThruClose
#undef PassThruConnect
#undef PassThruDisconnect
#undef PassThruReadMsgs
#undef PassThruWriteMsgs
#undef PassThruStartPeriodicMsg
#undef PassThruStopPeriodicMsg
#undef PassThruStartMsgFilter
#undef PassThruStopMsgFilter
#undef PassThruSetProgrammingVoltage
#undef PassThruReadVersion
#undef PassThruGetLastError
#undef PassThruIoctl
static unsigned char api_busy;
int32_t PassThruOpen(const void *pName, unsigned long *pDeviceID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruOpen(pName,pDeviceID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruClose(const unsigned long DeviceID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruClose(DeviceID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruConnect(const unsigned long DeviceID,const unsigned long protocolID,
    const unsigned long flags,const unsigned long baud,unsigned long *pChannelID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruConnect(DeviceID,protocolID,flags,baud,pChannelID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruDisconnect(const unsigned long ChannelID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruDisconnect(ChannelID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruReadMsgs(const unsigned long ChannelID,PASSTHRU_MSG *pMsg,
    unsigned long *pNumMsgs,const unsigned long Timeout) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        if(pNumMsgs)*pNumMsgs=0;
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruReadMsgs(ChannelID,pMsg,pNumMsgs,Timeout);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruWriteMsgs(const unsigned long ChannelID,const PASSTHRU_MSG *pMsg,
    unsigned long *pNumMsgs,const unsigned long Timeout) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        if(pNumMsgs)*pNumMsgs=0;
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruWriteMsgs(ChannelID,pMsg,pNumMsgs,Timeout);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruStartPeriodicMsg(const unsigned long ChannelID, const PASSTHRU_MSG *pMsg,
	const unsigned long *pMsgID, const unsigned long timeInterval) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruStartPeriodicMsg(ChannelID,pMsg,pMsgID,timeInterval);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruStopPeriodicMsg(const unsigned long ChannelID, const unsigned long msgID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruStopPeriodicMsg(ChannelID,msgID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruStartMsgFilter(const unsigned long ChannelID, const unsigned long FilterType,
	const PASSTHRU_MSG *pMaskMsg, const PASSTHRU_MSG *pPatternMsg,
	const PASSTHRU_MSG *pFlowControlMsg, unsigned long *pMsgID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruStartMsgFilter(ChannelID,FilterType,pMaskMsg,pPatternMsg,pFlowControlMsg,pMsgID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruStopMsgFilter(const unsigned long ChannelID, const unsigned long msgID) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruStopMsgFilter(ChannelID,msgID);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruSetProgrammingVoltage(const unsigned long DeviceID,
	const unsigned long pinNumber, const unsigned long voltage) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruSetProgrammingVoltage(DeviceID,pinNumber,voltage);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruReadVersion(const unsigned long DeviceID, char *pFirmwareVersion,
	char *pDllVersion, char *pApiVersion) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruReadVersion(DeviceID,pFirmwareVersion,pDllVersion,pApiVersion);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
int32_t PassThruGetLastError(char *pErrorDescription) {
    if(!pErrorDescription)return J2534_ERR_NULL_PARAMETER;
    snprintf(pErrorDescription,LE_LEN,"%s",LAST_ERROR);return J2534_NOERROR;
}
int32_t PassThruIoctl(const unsigned long ChannelID, const unsigned long ioctlID,
	const void *pInput, void *pOutput) {
    if(__atomic_test_and_set(&api_busy,__ATOMIC_ACQUIRE)) {
        snprintf((char *)LAST_ERROR,LE_LEN,"Concurrent J2534 operation refused");return J2534_ERR_DEVICE_IN_USE;
    }
    int32_t result=impl_PassThruIoctl(ChannelID,ioctlID,pInput,pOutput);
    __atomic_clear(&api_busy,__ATOMIC_RELEASE);return result;
}
