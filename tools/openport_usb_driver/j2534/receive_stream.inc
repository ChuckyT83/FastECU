/* Adapter USB records, not CAN/ISO-TP frames. Only complete records/messages
 * become visible to callers. No SSM addresses or conversion rules live here. */
#define RX_QUEUE_LIMIT 64
static unsigned long queued_messages;
static uint8_t wire_record[260];
static size_t wire_used;
static PASSTHRU_MSG assembling;
static int assembling_active, receive_fault, awaiting_response;
static char command_reply[MAX_LEN];
static int control_matches(const char *line,const uint8_t *expected) {
    if(!expected)return !strcmp(line,"aro\r\n");
    size_t n=strlen((const char *)expected);
    return n && !strncmp(line,(const char *)expected,n) &&
        (expected[n-1]==' ' || line[n]==' ' || line[n]=='\r');
}
static unsigned long command_serial;
static int command_error, command_waiting, command_matched;
static const uint8_t *command_expect;

static void reset_receive(void) {
    flush_queue();queued_messages=0;wire_used=0;
    memset(&assembling,0,sizeof(assembling));
    assembling_active=receive_fault=awaiting_response=0;
    command_reply[0]=0;command_error=0;
}
static int stream_failure(int status,const char *detail) {
    snprintf((char *)LAST_ERROR,LE_LEN,"%s; reconnect channel",detail);
    receive_fault=status;
    flush_queue();queued_messages=0;wire_used=0;assembling_active=0;
    memset(&assembling,0,sizeof(assembling));
    return status;
}
static int publish_message(const PASSTHRU_MSG *message) {
    if(queued_messages>=RX_QUEUE_LIMIT)
        return stream_failure(J2534_ERR_BUFFER_OVERFLOW,"Completed receive queue full");
    PASSTHRU_MSG *copy=malloc(sizeof(*copy));
    if(!copy)return stream_failure(J2534_ERR_EXCEEDED_LIMIT,"Receive allocation failed");
    *copy=*message;
    if(!queue_msg(copy)) {free(copy);return stream_failure(J2534_ERR_EXCEEDED_LIMIT,"Receive queue allocation failed");}
    ++queued_messages;return J2534_NOERROR;
}
static int pop_message(PASSTHRU_MSG *message) {
    if(!read_queue_msg(message))return FALSE;
    --queued_messages;return TRUE;
}
static int append_payload(PASSTHRU_MSG *message,const uint8_t *bytes,size_t length) {
    if(message->DataSize>PM_DATA_LEN || length>PM_DATA_LEN-message->DataSize)
        return stream_failure(J2534_ERR_BUFFER_OVERFLOW,"Logical receive message too large");
    memcpy(message->Data+message->DataSize,bytes,length);
    message->DataSize+=(unsigned long)length;
    message->ExtraDataIndex=message->DataSize;
    return J2534_NOERROR;
}
static int accept_record(const uint8_t *record,size_t length) {
    if(length<5 || record[2]!=(uint8_t)con->channel)
        return stream_failure(J2534_ERR_INVALID_MSG,"Unexpected USB channel/record");
    const uint8_t type=record[4];
    const int can=(con->channel==CAN || con->channel==ISO15765);
    const int normal=type==NORM_MSG || type==TX_LB_MSG;
    const int end=type==RX_MSG_END_IND || type==LB_MSG_END_IND;
    const int start=type==NORM_MSG_START_IND || type==TX_LB_START_IND;
    if(type==EXT_ADDR_MSG_END_IND)
        return stream_failure(J2534_ERR_NOT_SUPPORTED,"Extended-address USB record unsupported");
    if(!normal && !end && !start && type!=TX_DONE)
        return stream_failure(J2534_ERR_INVALID_MSG,"Unknown USB message type");
    const size_t prefix=(can || !normal)?9:5;
    if(length<prefix)return stream_failure(J2534_ERR_INVALID_MSG,"USB record missing timestamp");
    const uint8_t *payload=record+prefix;size_t payload_length=length-prefix;
    PASSTHRU_MSG event={0};event.ProtocolID=con->protocol_id;
    if(can || !normal)event.Timestamp=parse_ts(record+5);
    if(start || type==TX_DONE) {
        if(start && assembling_active)return stream_failure(J2534_ERR_INVALID_MSG,"New indication during partial receive");
        event.RxStatus=start?2:8;
        int r=append_payload(&event,payload,payload_length);if(r)return r;
        return publish_message(&event);
    }
    if(!assembling_active) {
        memset(&assembling,0,sizeof(assembling));
        assembling.ProtocolID=con->protocol_id;assembling.RxStatus=(type==TX_LB_MSG || type==LB_MSG_END_IND)?1:0;
        assembling_active=1;
    } else if(normal && assembling.RxStatus!=(type==TX_LB_MSG?1UL:0UL))
        return stream_failure(J2534_ERR_INVALID_MSG,"Interleaved receive/loopback records");
    if(can || !normal)assembling.Timestamp=event.Timestamp;
    int r=append_payload(&assembling,payload,payload_length);if(r)return r;
    if(end || (can && con->channel==CAN)) {
        if(can && assembling.DataSize<4)
            return stream_failure(J2534_ERR_INVALID_MSG,"CAN message missing identifier");
        r=publish_message(&assembling);
        if(!r && assembling.RxStatus==0)awaiting_response=0;
        assembling_active=0;memset(&assembling,0,sizeof(assembling));
        return r;
    }
    return J2534_NOERROR;
}
static int feed_usb(const uint8_t *bytes,size_t length) {
    for(size_t i=0;i<length;++i) {
        if(!wire_used && (bytes[i]=='\r' || bytes[i]=='\n'))continue;
        if(wire_used==sizeof(wire_record))return stream_failure(J2534_ERR_INVALID_MSG,"USB record exceeds capacity");
        wire_record[wire_used++]=bytes[i];
        if(wire_record[0]!='a' || (wire_used>=2 && wire_record[1]!='r'))
            return stream_failure(J2534_ERR_INVALID_MSG,"Invalid USB record header");
        if(wire_used<3)continue;
        if(wire_record[2]>=ISO9141 && wire_record[2]<=ISO15765) {
            if(wire_used<4)continue;
            if(!wire_record[3])return stream_failure(J2534_ERR_INVALID_MSG,"Zero USB record length");
            const size_t needed=4+(size_t)wire_record[3];
            if(wire_used==needed) {
                int r=accept_record(wire_record,wire_used);wire_used=0;if(r)return r;
            }
        } else {
            if(wire_used>=MAX_LEN)return stream_failure(J2534_ERR_INVALID_MSG,"Control response too long");
            if(bytes[i]=='\n') {
                if(wire_used<5 || wire_record[wire_used-2]!='\r')
                    return stream_failure(J2534_ERR_INVALID_MSG,"Malformed control response");
                char line[MAX_LEN];memcpy(line,wire_record,wire_used);line[wire_used]=0;
                int error=0;
                if(!strncmp(line,"are ",4)) {
                    char *end=NULL;errno=0;unsigned long code=strtoul(line+4,&end,10);
                    if(errno || end==line+4 || strcmp(end,"\r\n") || !code || code>INT32_MAX)
                        return stream_failure(J2534_ERR_INVALID_MSG,"Malformed adapter error");
                    error=(int)code;
                    snprintf((char *)LAST_ERROR,LE_LEN,"Adapter returned J2534 error %lu",code);
                    if(!command_waiting)return stream_failure(error,"Unsolicited adapter error");
                }
                if(command_waiting && !command_matched && (error ||
                    control_matches(line,command_expect))) {
                    memcpy(command_reply,line,wire_used+1);command_error=error;
                    command_matched=1;++command_serial;
                }
                wire_used=0;
            }
        }
    }
    return J2534_NOERROR;
}
static unsigned int remaining_ms(uint64_t deadline) {
    uint64_t now=lifecycle_clock_ms();
    if(now>=deadline)return 0;
    uint64_t remaining=deadline-now;
    return remaining>0xffffffffULL?0xffffffffU:(unsigned int)remaining;
}
/* Positive parser/J2534 errors and negative libusb errors are both preserved. */
static int receive_usb(unsigned int timeout) {
    uint8_t bytes[PM_DATA_LEN];int actual=0;
    int r=libusb_bulk_transfer(con->dev_handle,endpoint->addr_in,bytes,sizeof(bytes),&actual,timeout?timeout:1);
    if(actual<0 || actual>(int)sizeof(bytes))return stream_failure(J2534_ERR_INVALID_MSG,"USB returned invalid byte count");
    if(write_log) {writelog("\tUSB stream Rcvd:\n\t\t");writelogmsg(bytes,0,(unsigned long)actual);writelog("\n");}
    if(actual) {int parsed=feed_usb(bytes,(size_t)actual);if(parsed)return parsed;}
    if(r!=LIBUSB_SUCCESS && r!=LIBUSB_ERROR_TIMEOUT)
        return stream_failure(error_map(r),"USB receive failed");
    return r;
}
