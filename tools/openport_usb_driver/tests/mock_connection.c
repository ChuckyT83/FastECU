/* Hardware-independent regression harness. Every USB function is a local fake.
 * Never link this harness to libusb or execute an attached Windows DLL. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#define setenv(name,value,overwrite) _putenv_s(name,value)
#endif
#ifndef DRIVER_SOURCE
#define DRIVER_SOURCE "../j2534/j2534.c"
#endif
#include DRIVER_SOURCE
static int failure, writes, reads, released, closed, exited, release_failure, freed_configs;
static libusb_device *devices[] = {(libusb_device *)(uintptr_t)1, NULL};
static const struct libusb_endpoint_descriptor eps[] = {
 {.bEndpointAddress=0x81,.bmAttributes=LIBUSB_TRANSFER_TYPE_BULK},
 {.bEndpointAddress=0x01,.bmAttributes=LIBUSB_TRANSFER_TYPE_BULK}};
static const struct libusb_interface_descriptor alt = {
 .bInterfaceNumber=0,.bNumEndpoints=2,.endpoint=eps};
static const struct libusb_interface intf = {.altsetting=&alt,.num_altsetting=1};
static struct libusb_config_descriptor cfg = {.bNumInterfaces=1,.interface=&intf};
int libusb_init(libusb_context **c) { *c=(libusb_context *)(uintptr_t)1; return failure==10 ? LIBUSB_ERROR_OTHER : 0; }
void libusb_exit(libusb_context *c) { (void)c; exited++; }
ssize_t libusb_get_device_list(libusb_context *c,libusb_device ***d) { (void)c;*d=devices;return failure==9 ? LIBUSB_ERROR_IO : 1; }
void libusb_free_device_list(libusb_device **d,int unref) { (void)d;(void)unref; }
int libusb_get_device_descriptor(libusb_device *d,struct libusb_device_descriptor *desc) {
 (void)d;memset(desc,0,sizeof(*desc));desc->idVendor=0x0403;desc->idProduct=0xcc4d;return 0;
}
int libusb_open(libusb_device *d,libusb_device_handle **h) {
 (void)d;if(failure==5)return LIBUSB_ERROR_ACCESS;*h=(libusb_device_handle *)(uintptr_t)1;return 0;
}
uint8_t libusb_get_device_address(libusb_device *d) { (void)d;return 42; }
int libusb_get_config_descriptor(libusb_device *d,uint8_t i,struct libusb_config_descriptor **c) {
 (void)d;(void)i;if(failure==7)return LIBUSB_ERROR_IO;*c=&cfg;if(failure==8){static struct libusb_config_descriptor empty={0};*c=&empty;}return 0;
}
void libusb_free_config_descriptor(struct libusb_config_descriptor *c) { (void)c;freed_configs++; }
int libusb_kernel_driver_active(libusb_device_handle *h,int i) { (void)h;(void)i;return 0; }
int libusb_detach_kernel_driver(libusb_device_handle *h,int i) { (void)h;(void)i;assert(0);return 0; }
int libusb_claim_interface(libusb_device_handle *h,int i) { (void)h;(void)i;return failure==6?LIBUSB_ERROR_BUSY:0; }
int libusb_release_interface(libusb_device_handle *h,int i) { (void)h;(void)i;released++;return release_failure ? LIBUSB_ERROR_BUSY : 0; }
void libusb_close(libusb_device_handle *h) { (void)h;closed++; }
const char *libusb_error_name(int e) {
 switch(e){case 0:return "LIBUSB_SUCCESS";case -7:return "LIBUSB_ERROR_TIMEOUT";
 case -3:return "LIBUSB_ERROR_ACCESS";case -6:return "LIBUSB_ERROR_BUSY";default:return "LIBUSB_ERROR_IO";}
}
int libusb_bulk_transfer(libusb_device_handle *h,unsigned char ep,unsigned char *data,int length,int *n,unsigned int timeout) {
 (void)h;assert(timeout>0 && timeout<=2000);*n=0;
 if(ep==1){
  assert((length==9 && !memcmp(data,"\r\n\r\nati\r\n",9)) ||
         (length==5 && (!memcmp(data,"ata\r\n",5) || !memcmp(data,"atz\r\n",5))));
  writes++;if(failure==1&&writes==1)return LIBUSB_ERROR_TIMEOUT;
  *n=(failure==13)?length-1:length;return 0;
 }
 assert(ep==0x81);reads++;
 if((failure==2&&writes==1)||(failure==3&&writes>=2))return LIBUSB_ERROR_TIMEOUT;
 const char *reply=(failure==4&&writes==1)?"are 7\r\n":(writes==1?"ari fw: 1.17.4877\r\n":"aro\r\n");
 *n=(int)strlen(reply);assert(*n<length);memcpy(data,reply,*n);return 0;
}
const struct libusb_version *libusb_get_version(void) { static const struct libusb_version v={1,0,29,0,"",""};return &v; }
int main(int argc,char **argv) {
 assert(argc>=2);char error[82];memset(error,0x5a,sizeof(error));
 if(strcmp(argv[1],"null-error")==0){assert(PassThruGetLastError(NULL)==J2534_ERR_NULL_PARAMETER);}
 else if(strcmp(argv[1],"copy-error")==0){
  snprintf((char *)LAST_ERROR,LE_LEN,"Cannot find device: LIBUSB_ERROR_ACCESS");
  assert(PassThruGetLastError(error)==0);assert(strcmp(error,(char *)LAST_ERROR)==0);assert(error[80]==0x5a);
 }
 else if(strcmp(argv[1],"max-error")==0){
  memset(LAST_ERROR,'x',LE_LEN-1);LAST_ERROR[LE_LEN-1]=0;
  assert(PassThruGetLastError(error)==0);assert(strlen(error)==79);assert(error[80]==0x5a);
 }
 else if(strncmp(argv[1],"close-",6)==0 || strcmp(argv[1],"ownership")==0) {
  unsigned long id=0xdeadbeefUL;assert(PassThruOpen(NULL,&id)==0);assert(id==42);
  char fw[80]={0},dll[80]={0},api[80]={0};
  assert(PassThruReadVersion(id,fw,dll,api)==0);assert(!strcmp(fw,"1.17.4877"));assert(!strcmp(api,"04.04"));
  if(!strcmp(argv[1],"ownership")) {
   unsigned long other=99;assert(PassThruOpen(NULL,&other)==J2534_ERR_DEVICE_IN_USE);assert(other==99);assert(writes==2);
   assert(PassThruClose(id+256)==J2534_ERR_INVALID_DEVICE_ID);assert(writes==2);
  }
  if(!strcmp(argv[1],"close-timeout") || !strcmp(argv[1],"close-both"))failure=3;
  if(!strcmp(argv[1],"close-release") || !strcmp(argv[1],"close-both"))release_failure=1;
  int status=PassThruClose(id);
  if(failure==3){assert(status==J2534_ERR_TIMEOUT);assert(strstr((char *)LAST_ERROR,"atz failed"));}
  else if(release_failure){assert(status==J2534_ERR_DEVICE_IN_USE);assert(strstr((char *)LAST_ERROR,"release failed"));}
  else assert(status==0);
  assert(released==1&&closed==1&&exited==1);assert(!con->dev_handle&&!con->ctx);
  assert(PassThruClose(id)==J2534_ERR_INVALID_DEVICE_ID);assert(writes==3);
 }
 else {
  failure=atoi(argv[1]);unsigned long id=0xdeadbeefUL;
  if(argc==3)assert(setenv("LOG_ENABLE",argv[2],1)==0);
  if(failure==11){failure=2;release_failure=1;}
  int status=PassThruOpen(NULL,&id);
  if(failure==0){assert(status==0);assert(id==42);assert(writes==2);assert(LAST_ERROR[0]==0);assert(PassThruClose(id)==0);}
  else {
   int expected=failure<=3?J2534_ERR_TIMEOUT:failure==4?J2534_ERR_FAILED:
                failure==5||failure==6?J2534_ERR_DEVICE_IN_USE:
                failure==7||failure==9||failure==13?J2534_ERR_DEVICE_NOT_CONNECTED:
                failure==8?J2534_ERR_DEVICE_NOT_CONNECTED:J2534_ERR_FAILED;
   assert(status==expected);assert(id==0xdeadbeefUL);assert(!con->dev_handle&&!con->ctx);
   assert(closed==(failure==5||failure==9||failure==10?0:1));
   assert(exited==(failure==10?0:1));
   assert(released==(failure<=4||failure==13?1:0));
   assert(PassThruGetLastError(error)==0);assert(error[0]);
   if(release_failure)assert(strstr(error,"ati failed"));
   if(failure==7||failure==8||failure==9||failure==10)assert(writes==0);
  }
  if(argc==3){FILE *f=fopen(argv[2],"rb");assert(f);char log[8192]={0};size_t size=fread(log,1,sizeof(log)-1,f);fclose(f);
   assert(size>0);assert(strstr(log,"USBQUAL/1 begin=1"));assert(strstr(log,"context_exit_returned=1"));}
 }
 printf("PASS %s\n",argv[1]);return 0;
}
