/* Actual driver code with the existing fake-USB harness. No libusb is linked. */
#define LOGGING_HARNESS
#include "mock_connection.c"
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#endif
static int gate_ready,gate_release,block_receive,owner_ok;
static void pause_ms(void) {
#ifdef _WIN32
 Sleep(1);
#else
 usleep(1000);
#endif
}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t incoming[50000],transmitted[PM_DATA_LEN+MAX_LEN];
static size_t in_size,in_pos,split_at;static int bytewise,timeout_with_data,tx_count,tx_failure,tx_short,read_calls,reentrant;
static unsigned int largest_timeout;
static void inject(const void *p,size_t n){CHECK(in_size+n<=sizeof(incoming));memcpy(incoming+in_size,p,n);in_size+=n;}
static void record(uint8_t type,const uint8_t *p,size_t n) {
    CHECK(n<=250);uint8_t h[]={0x61,0x72,0x36,(uint8_t)(5+n),type,0,0,0,1};inject(h,sizeof(h));if(n)inject(p,n);
}
static void reply(const uint8_t *p,size_t n) {record(0,p,n);record(0x40,NULL,0);}
static void reset_input(void) {in_size=in_pos=split_at=0;bytewise=timeout_with_data=0;read_calls=0;}
static int transfer(libusb_device_handle *h,unsigned char ep,unsigned char *p,int cap,int *n,unsigned int timeout) {
    (void)h;CHECK(timeout>0);if(timeout>largest_timeout)largest_timeout=timeout;*n=0;
    if(ep==1) {
        if(cap>=3 && !memcmp(p,"att",3)) {
            ++tx_count;CHECK(cap<=(int)sizeof(transmitted));memcpy(transmitted,p,cap);
            if(tx_count==tx_failure)return LIBUSB_ERROR_TIMEOUT;
            *n=cap-(tx_short?1:0);return 0;
        }
        *n=cap;
        if(!memcmp(p,"atf",3))inject("arf 1\r\naro\r\n",12);
        else if(!memcmp(p,"atg",3))inject("arg 1 0\r\n",9);
        else inject("aro\r\n",5);
        return 0;
    }
    CHECK(ep==0x81);++read_calls;
    if(reentrant) {
        reentrant=0;PASSTHRU_MSG m={0};unsigned long count=1;
        CHECK(PassThruWriteMsgs(6,&m,&count,10)==J2534_ERR_DEVICE_IN_USE);
    }
    if(block_receive) {
        block_receive=0;__atomic_store_n(&gate_ready,1,__ATOMIC_RELEASE);
        uint64_t deadline=lifecycle_clock_ms()+2000;
        while(!__atomic_load_n(&gate_release,__ATOMIC_ACQUIRE) && lifecycle_clock_ms()<deadline)pause_ms();
        CHECK(__atomic_load_n(&gate_release,__ATOMIC_ACQUIRE));
    }
    if(in_pos==in_size)return LIBUSB_ERROR_TIMEOUT;
    size_t amount=in_size-in_pos;if(amount>(size_t)cap)amount=cap;
    if(bytewise)amount=1;
    else if(split_at && in_pos<split_at && amount>split_at-in_pos)amount=split_at-in_pos;
    memcpy(p,incoming+in_pos,amount);in_pos+=amount;*n=(int)amount;
    return timeout_with_data?LIBUSB_ERROR_TIMEOUT:0;
}
static void setup(void) {
    unsigned long id=0,ch=0;CHECK(PassThruOpen(NULL,&id)==0);logging_transfer=transfer;
    CHECK(PassThruConnect(id,6,0,500000,&ch)==0 && ch==6);reset_input();largest_timeout=0;
}
static void fresh(void) {reset_receive();reset_input();tx_count=tx_failure=tx_short=0;largest_timeout=0;}
static PASSTHRU_MSG message(void) {
    PASSTHRU_MSG m={0};m.ProtocolID=6;m.DataSize=6;m.Data[2]=7;m.Data[3]=0xe0;m.Data[4]=0xa8;return m;
}
static const uint8_t avcs[]={0,0,7,0xe8,0xe8,0x52,0x64,0x18,0x18};
static void check_reply(void) {
    struct {PASSTHRU_MSG m;uint8_t canary[16];} out;memset(&out,0xa5,sizeof(out));unsigned long n=1;
    CHECK(PassThruReadMsgs(6,&out.m,&n,100)==0 && n==1);
    CHECK(out.m.DataSize==sizeof(avcs) && !memcmp(out.m.Data,avcs,sizeof(avcs)));
    CHECK(out.m.ProtocolID==6 && out.m.RxStatus==0 && out.m.ExtraDataIndex==sizeof(avcs));
    for(size_t i=0;i<sizeof(out.canary);++i)CHECK(out.canary[i]==0xa5);
}
#ifdef _WIN32
static DWORD WINAPI concurrent_reader(LPVOID unused)
#else
static void *concurrent_reader(void *unused)
#endif
{
    (void)unused;PASSTHRU_MSG m={0};unsigned long n=1;char error[80]={0};
    int r=PassThruReadMsgs(6,&m,&n,1000);PassThruGetLastError(error);
    owner_ok=r==0 && n==1 && m.DataSize==sizeof(avcs) && !memcmp(m.Data,avcs,sizeof(avcs)) && !strstr(error,"Concurrent");
    return 0;
}
int main(int argc,char **argv) {
    CHECK(argc==2);setup();const char *test=argv[1];
    if(!strcmp(test,"split")) {
        for(size_t i=1;i<27;++i){fresh();reply(avcs,sizeof(avcs));split_at=i;check_reply();}
        fresh();reply(avcs,sizeof(avcs));bytewise=1;check_reply();
    } else if(!strcmp(test,"coalesced")) {
        record(0x80,avcs,4);reply(avcs,sizeof(avcs));record(0x10,avcs,4);reply(avcs,sizeof(avcs));
        PASSTHRU_MSG m={0};unsigned long n=1;CHECK(PassThruReadMsgs(6,&m,&n,50)==0 && n==1 && m.RxStatus==2);
        check_reply();n=1;CHECK(PassThruReadMsgs(6,&m,&n,50)==0 && m.RxStatus==8);check_reply();
    } else if(!strcmp(test,"partial-timeout")) {
        record(0,avcs,sizeof(avcs));PASSTHRU_MSG m;memset(&m,0xcc,sizeof(m));unsigned long n=1;
        CHECK(PassThruReadMsgs(6,&m,&n,50)==J2534_ERR_TIMEOUT && n==0 && m.Data[0]==0xcc);
        record(0x40,NULL,0);n=1;CHECK(PassThruReadMsgs(6,&m,&n,50)==J2534_ERR_FAILED && n==0);
        m=message();n=1;CHECK(PassThruWriteMsgs(6,&m,&n,50)==J2534_ERR_FAILED && n==0 && tx_count==0);
        CHECK(PassThruIoctl(6,J2534_CLEAR_RX_BUFFER,NULL,NULL)==J2534_ERR_FAILED);
    } else if(!strcmp(test,"poll-carry")) {
        const uint8_t h[]={0x61,0x72,0x36};inject(h,3);PASSTHRU_MSG m={0};unsigned long n=1;
        CHECK(PassThruReadMsgs(6,&m,&n,0)==J2534_ERR_BUFFER_EMPTY && n==0);
        reset_input();uint8_t rest[]={14,0,0,0,0,1};inject(rest,sizeof(rest));inject(avcs,sizeof(avcs));record(0x40,NULL,0);check_reply();
    } else if(!strcmp(test,"timeout-bytes")) {
        reply(avcs,sizeof(avcs));timeout_with_data=1;check_reply();
    } else if(!strcmp(test,"malformed")) {
        const uint8_t bad[][9]={{'a','r',0x36,1,0}, {'a','r',0x36,5,0xff,0,0,0,1}, {'a','r',0x35,5,0x40,0,0,0,1}, {'x','r',0x36,5,0,0,0,0,1}};
        for(size_t i=0;i<4;++i) {fresh();inject(bad[i],i==0?5:9);PASSTHRU_MSG m={0};unsigned long n=1;CHECK(PassThruReadMsgs(6,&m,&n,20)==J2534_ERR_INVALID_MSG && n==0);}
    } else if(!strcmp(test,"capacity")) {
        uint8_t payload[PM_DATA_LEN];for(size_t i=0;i<sizeof(payload);++i)payload[i]=(uint8_t)i;
        for(int overflow=0;overflow<=1;++overflow) {
            fresh();for(size_t i=0;i<sizeof(payload);) {size_t n=sizeof(payload)-i;if(n>250)n=250;record(0,payload+i,n);i+=n;}
            if(overflow)record(0,payload,1);record(0x40,NULL,0);
            PASSTHRU_MSG m={0};unsigned long n=1;int r=PassThruReadMsgs(6,&m,&n,100);
            if(overflow)CHECK(r==J2534_ERR_BUFFER_OVERFLOW && n==0);
            else CHECK(r==0 && n==1 && m.DataSize==PM_DATA_LEN && !memcmp(m.Data,payload,sizeof(payload)));
        }
    } else if(!strcmp(test,"queue-limit")) {
        for(int i=0;i<65;++i)record(0x10,avcs,4);
        PASSTHRU_MSG m={0};unsigned long n=1;CHECK(PassThruReadMsgs(6,&m,&n,100)==J2534_ERR_BUFFER_OVERFLOW && n==0);
    } else if(!strcmp(test,"write-counts")) {
        PASSTHRU_MSG m[3]={message(),message(),message()};unsigned long n=3;tx_failure=2;
        CHECK(PassThruWriteMsgs(6,m,&n,100)==J2534_ERR_TIMEOUT && n==1 && tx_count==2);
        fresh();n=3;tx_failure=1;CHECK(PassThruWriteMsgs(6,m,&n,100)==J2534_ERR_TIMEOUT && n==0 && tx_count==1);
        fresh();n=1;tx_short=1;CHECK(PassThruWriteMsgs(6,m,&n,100)==J2534_ERR_FAILED && n==0);
    } else if(!strcmp(test,"write-capacity")) {
        PASSTHRU_MSG m=message();m.DataSize=PM_DATA_LEN;memset(m.Data,0x5a,sizeof(m.Data));unsigned long n=1;
        CHECK(PassThruWriteMsgs(6,&m,&n,30)==0 && n==1 && largest_timeout<=30);
        const char *header="att6 4128 0\r\n";CHECK(!memcmp(transmitted,header,strlen(header)) && !memcmp(transmitted+strlen(header),m.Data,PM_DATA_LEN));
    } else if(!strcmp(test,"filter-bounds")) {
        PASSTHRU_MSG mask=message(),pattern=message(),flow=message();mask.DataSize=pattern.DataSize=4;flow.DataSize=13;unsigned long id=0;
        CHECK(PassThruStartMsgFilter(6,3,&mask,&pattern,&flow,&id)==J2534_ERR_INVALID_MSG && tx_count==0);
        flow.DataSize=4;CHECK(PassThruStartMsgFilter(6,3,&mask,&pattern,&flow,&id)==0 && id==1);
    } else if(!strcmp(test,"control-routing")) {
        reply(avcs,sizeof(avcs));SCONFIG cfg={1,0};SCONFIG_LIST list={1,&cfg};
        CHECK(PassThruIoctl(6,J2534_SET_CONFIG,&list,NULL)==0);check_reply();
    } else if(!strcmp(test,"clear")) {
        reply(avcs,sizeof(avcs));CHECK(PassThruIoctl(6,J2534_CLEAR_RX_BUFFER,NULL,NULL)==0);
        PASSTHRU_MSG m={0};unsigned long n=1;CHECK(PassThruReadMsgs(6,&m,&n,0)==J2534_ERR_BUFFER_EMPTY && n==0);
        CHECK(PassThruIoctl(6,J2534_CLEAR_TX_BUFFER,NULL,NULL)==J2534_ERR_NOT_SUPPORTED);
    } else if(!strcmp(test,"ownership")) {
        reply(avcs,sizeof(avcs));reentrant=1;check_reply();
    } else if(!strcmp(test,"repeated")) {
        for(int i=0;i<10000;++i) {reset_input();uint8_t b[9];memcpy(b,avcs,9);b[5]=(uint8_t)i;reply(b,9);PASSTHRU_MSG m={0};unsigned long n=1;
            CHECK(PassThruReadMsgs(6,&m,&n,50)==0 && n==1 && m.DataSize==9 && !memcmp(m.Data,b,9));}
    } else if(!strcmp(test,"threads")) {
        reply(avcs,sizeof(avcs));block_receive=1;
#ifdef _WIN32
        HANDLE thread=CreateThread(NULL,0,concurrent_reader,NULL,0,NULL);CHECK(thread);
#else
        pthread_t thread;CHECK(!pthread_create(&thread,NULL,concurrent_reader,NULL));
#endif
        uint64_t deadline=lifecycle_clock_ms()+2000;
        while(!__atomic_load_n(&gate_ready,__ATOMIC_ACQUIRE) && lifecycle_clock_ms()<deadline)pause_ms();
        CHECK(__atomic_load_n(&gate_ready,__ATOMIC_ACQUIRE));
        PASSTHRU_MSG m=message();unsigned long n=1;char error[80]={0};
        CHECK(PassThruWriteMsgs(6,&m,&n,100)==J2534_ERR_DEVICE_IN_USE && n==0);
        CHECK(PassThruGetLastError(error)==0 && strstr(error,"Concurrent"));
        __atomic_store_n(&gate_release,1,__ATOMIC_RELEASE);
#ifdef _WIN32
        CHECK(WaitForSingleObject(thread,3000)==WAIT_OBJECT_0);CloseHandle(thread);
#else
        CHECK(!pthread_join(thread,NULL));
#endif
        CHECK(owner_ok);
    } else if(!strcmp(test,"fuzz")) {
        uint32_t seed=0x12345678;
        for(int i=0;i<10000;++i) {
            fresh();uint8_t bytes[300];
            for(size_t j=0;j<sizeof(bytes);++j){seed=seed*1664525u+1013904223u;bytes[j]=(uint8_t)(seed>>24);}
            bytes[0]='a';bytes[1]='r';bytes[2]=0x36;bytes[4]=(i%3==0)?0:(i%3==1)?0x40:0x80;
            size_t length=(seed>>16)%sizeof(bytes);
            for(size_t j=0;j<length;++j)if(feed_usb(bytes+j,1))break;
        }
        fresh();
    } else if(!strcmp(test,"parameters")) {
        PASSTHRU_MSG m={0};unsigned long n=1;
        CHECK(PassThruReadMsgs(6,NULL,&n,1)==J2534_ERR_NULL_PARAMETER && n==0);
        CHECK(PassThruReadMsgs(262,&m,&n,1)==J2534_ERR_INVALID_CHANNEL_ID);
        CHECK(PassThruWriteMsgs(6,&m,NULL,1)==J2534_ERR_NULL_PARAMETER);
        unsigned long ch=999;CHECK(PassThruConnect(42+256,6,0,500000,&ch)==J2534_ERR_INVALID_DEVICE_ID && ch==999);
    } else {fprintf(stderr,"Unknown test\n");return 2;}
    reset_input();PassThruClose(42);CHECK(!con->dev_handle && !con->ctx && !fifo_head);
    printf("PASS %s\n",test);return 0;
}
