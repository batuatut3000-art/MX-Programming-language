#include "mx_uefi.h"
#define ABI __attribute__((ms_abi))
#define WIFI_MAX 16

typedef struct {uint8_t length;uint8_t bytes[32];} WIFI_SSID;
typedef struct {uint8_t oui[3],type;} WIFI_SUITE;
typedef struct {uint16_t count;WIFI_SUITE items[1];} WIFI_SUITES;
typedef struct {uint32_t bss_type;WIFI_SSID ssid;WIFI_SUITES*akm,*cipher;} WIFI_NETWORK;
typedef struct {WIFI_NETWORK network;uint8_t quality;} WIFI_DESCRIPTION;
typedef struct {uint8_t count;WIFI_DESCRIPTION items[1];} WIFI_RESULT;
typedef struct {uint32_t count;WIFI_SSID hidden[1];} WIFI_GET_DATA;
typedef struct {void*event;EFI_STATUS status;WIFI_GET_DATA*data;WIFI_RESULT*result;} WIFI_GET_TOKEN;
typedef struct {WIFI_NETWORK*network;uint32_t timeout;} WIFI_CONNECT_DATA;
typedef struct {void*event;EFI_STATUS status;WIFI_CONNECT_DATA*data;uint32_t result;} WIFI_CONNECT_TOKEN;
_Static_assert(sizeof(WIFI_NETWORK)==56&&sizeof(WIFI_DESCRIPTION)==64,"UEFI Wi-Fi network ABI mismatch");
_Static_assert(__builtin_offsetof(WIFI_RESULT,items)==8,"UEFI Wi-Fi result ABI mismatch");
_Static_assert(sizeof(WIFI_GET_TOKEN)==32&&sizeof(WIFI_CONNECT_TOKEN)==32,"UEFI Wi-Fi token ABI mismatch");
typedef struct WIFI_PROTOCOL WIFI_PROTOCOL;
struct WIFI_PROTOCOL {EFI_STATUS(ABI*get_networks)(WIFI_PROTOCOL*,WIFI_GET_TOKEN*);EFI_STATUS(ABI*connect_network)(WIFI_PROTOCOL*,WIFI_CONNECT_TOKEN*);void*disconnect_network;};
typedef struct {EFI_STATUS(ABI*create)(void*,EFI_HANDLE*);EFI_STATUS(ABI*destroy)(void*,EFI_HANDLE);} WIFI_BINDING;
typedef struct {void*build_packet,*process_packet;EFI_STATUS(ABI*set_data)(void*,uint32_t,void*,UINTN);void*get_data;} WIFI_SUPPLICANT;

static EFI_SYSTEM_TABLE*wifi_table;
static WIFI_PROTOCOL*wifi_protocol;
static EFI_HANDLE wifi_handle;
static WIFI_RESULT*wifi_result;
static char wifi_names[WIFI_MAX][33];
static uint8_t wifi_quality[WIFI_MAX];
static int wifi_count;
static WIFI_GET_DATA scan_data;
static WIFI_GET_TOKEN scan_token;
static volatile int scan_done;
static int scan_pending;
static WIFI_CONNECT_DATA connect_data;
static WIFI_CONNECT_TOKEN connect_token;
static volatile int connect_done;
static int connect_pending;
static WIFI_BINDING*supp_binding;
static EFI_HANDLE supp_child;

static EFI_GUID wifi_guid={0x1b0fb9bf,0x699d,0x4fdd,{0xa7,0xc3,0x25,0x46,0x68,0x1b,0xf6,0x3b}};
static EFI_GUID supp_binding_guid={0x45bcd98e,0x59ad,0x4174,{0x95,0x46,0x34,0x4a,0x07,0x48,0x58,0x98}};
static EFI_GUID supp_guid={0x54fcc43e,0xaa89,0x4333,{0x9a,0x85,0xcd,0xea,0x24,0x05,0x1e,0x9e}};
static void ABI signal_done(void*event,void*context){(void)event;*(volatile int*)context=1;}
static EFI_BOOT_SERVICES*services(void){return wifi_table?(EFI_BOOT_SERVICES*)wifi_table->boot_services:0;}
static UINTN string_len(const char*s){UINTN n=0;if(s)while(s[n])n++;return n;}
static void clear_bytes(void*pointer,UINTN size){volatile uint8_t*p=(volatile uint8_t*)pointer;while(size--)*p++=0;}
static int same_name(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}

void mx_wifi_init(EFI_SYSTEM_TABLE*table){wifi_table=table;}
int mx_wifi_count(void){return wifi_count;}
const char*mx_wifi_ssid(int index){return index>=0&&index<wifi_count?wifi_names[index]:"";}
int mx_wifi_quality(int index){return index>=0&&index<wifi_count?wifi_quality[index]:0;}
static void clear_scan(void){EFI_BOOT_SERVICES*bs=services();if(wifi_result&&bs&&bs->fn[6]){EFI_STATUS(ABI*release)(void*)=(void*)bs->fn[6];release(wifi_result);}wifi_result=0;wifi_count=0;}
static void close_event(void*event){EFI_BOOT_SERVICES*bs=services();if(event&&bs&&bs->fn[11]){EFI_STATUS(ABI*close)(void*)=(void*)bs->fn[11];close(event);}}
static int wait_for(volatile int*done,int milliseconds){for(int i=0;i<milliseconds&&!(*done);i++){mx_poll();mx_step();}return *done!=0;}
static void cleanup_supplicant(void){if(supp_binding&&supp_child){supp_binding->destroy(supp_binding,supp_child);supp_child=0;supp_binding=0;}}

int mx_wifi_scan(void){
 EFI_BOOT_SERVICES*bs=services();if(!bs)return -1;
 if(connect_pending&&!connect_done)return -8;
 if(connect_pending){close_event(connect_token.event);connect_pending=0;connect_token.event=0;}
 if(scan_pending&&!scan_done)return -8;
 if(scan_pending){
  if(scan_token.result&&bs->fn[6]){EFI_STATUS(ABI*release)(void*)=(void*)bs->fn[6];release(scan_token.result);scan_token.result=0;}
  close_event(scan_token.event);scan_pending=0;scan_token.event=0;
 }
 clear_scan();
 EFI_STATUS(ABI*locate)(uint32_t,EFI_GUID*,void*,UINTN*,EFI_HANDLE**)=(void*)bs->fn[36];
 EFI_STATUS(ABI*protocol)(EFI_HANDLE,EFI_GUID*,void**)=(void*)bs->fn[16];
 EFI_STATUS(ABI*free_pool)(void*)=(void*)bs->fn[6];
 EFI_STATUS(ABI*create_event)(uint32_t,UINTN,void(ABI*)(void*,void*),void*,void**)=(void*)bs->fn[7];
 if(!locate||!protocol||!free_pool||!create_event)return -1;
 EFI_HANDLE*handles=0;UINTN handle_count=0;
 if(locate(2,&wifi_guid,0,&handle_count,&handles)!=0||!handles)return -1;
 int outcome=-2;
 for(UINTN h=0;h<handle_count;h++){
  WIFI_PROTOCOL*device=0;if(protocol(handles[h],&wifi_guid,(void**)&device)!=0||!device||!device->get_networks)continue;
  wifi_protocol=device;wifi_handle=handles[h];
  scan_data.count=0;scan_token=(WIFI_GET_TOKEN){0};scan_token.data=&scan_data;scan_done=0;
  if(create_event(0x200,8,signal_done,(void*)&scan_done,&scan_token.event)!=0){outcome=-3;continue;}
  EFI_STATUS status=device->get_networks(device,&scan_token);
  if(status!=0){close_event(scan_token.event);scan_token.event=0;outcome=-3;continue;}
  scan_pending=1;
  if(!wait_for(&scan_done,15000)){outcome=-4;break;}
  close_event(scan_token.event);scan_pending=0;scan_token.event=0;
  if(scan_token.status!=0){outcome=-5;if(scan_token.result){free_pool(scan_token.result);scan_token.result=0;}continue;}
  wifi_result=scan_token.result;
  if(!wifi_result){outcome=-5;continue;}
  wifi_count=wifi_result->count<WIFI_MAX?wifi_result->count:WIFI_MAX;
  for(int i=0;i<wifi_count;i++){
   WIFI_DESCRIPTION*d=&wifi_result->items[i];int n=d->network.ssid.length;
   if(n>32)n=32;
   for(int j=0;j<n;j++){uint8_t c=d->network.ssid.bytes[j];wifi_names[i][j]=(c>=32&&c<127)?(char)c:'?';}
   wifi_names[i][n]=0;wifi_quality[i]=d->quality;
  }
  outcome=wifi_count;break;
 }
 free_pool(handles);return outcome;
}

int mx_wifi_connect(const char*name,const char*password){
 EFI_BOOT_SERVICES*bs=services();if(!bs||!wifi_result||!wifi_protocol||!name||!name[0])return -1;
 if(scan_pending&&!scan_done)return -8;
 if(connect_pending&&!connect_done)return -8;
 if(connect_pending){close_event(connect_token.event);connect_pending=0;connect_token.event=0;}
 int index=-1;for(int i=0;i<wifi_count;i++)if(same_name(name,wifi_names[i])){index=i;break;}if(index<0)return -2;
 WIFI_NETWORK*network=&wifi_result->items[index].network;
 UINTN pass_len=string_len(password);if(pass_len&&pass_len<8)return -3;
 if(pass_len>63)return -3;
 EFI_STATUS(ABI*create_event)(uint32_t,UINTN,void(ABI*)(void*,void*),void*,void**)=(void*)bs->fn[7];
 EFI_STATUS(ABI*protocol)(EFI_HANDLE,EFI_GUID*,void**)=(void*)bs->fn[16];
 if(!create_event||!protocol)return -4;
 cleanup_supplicant();
 if(pass_len){
  WIFI_BINDING*binding=0;
  if(protocol(wifi_handle,&supp_binding_guid,(void**)&binding)!=0||!binding){
   if(!bs->locate_protocol||bs->locate_protocol(&supp_binding_guid,0,(void**)&binding)!=0||!binding)return -5;
  }
  supp_binding=binding;
  if(binding->create(binding,&supp_child)!=0||!supp_child){supp_binding=0;return -5;}
  WIFI_SUPPLICANT*supp=0;if(protocol(supp_child,&supp_guid,(void**)&supp)!=0||!supp||!supp->set_data){cleanup_supplicant();return -5;}
  char pass_copy[64];for(UINTN i=0;i<pass_len;i++)pass_copy[i]=password[i];pass_copy[pass_len]=0;
  EFI_STATUS status=supp->set_data(supp,3,pass_copy,pass_len+1);
  clear_bytes(pass_copy,sizeof(pass_copy));
  if(status!=0||supp->set_data(supp,4,&network->ssid,sizeof(network->ssid))!=0){cleanup_supplicant();return -6;}
  if(network->akm&&network->akm->count)supp->set_data(supp,0,&network->akm->items[0],sizeof(WIFI_SUITE));
  if(network->cipher&&network->cipher->count){supp->set_data(supp,1,&network->cipher->items[0],sizeof(WIFI_SUITE));supp->set_data(supp,2,&network->cipher->items[0],sizeof(WIFI_SUITE));}
 }
 connect_data.network=network;connect_data.timeout=30;
 connect_token=(WIFI_CONNECT_TOKEN){0};connect_token.data=&connect_data;connect_done=0;
 if(create_event(0x200,8,signal_done,(void*)&connect_done,&connect_token.event)!=0){cleanup_supplicant();return -7;}
 EFI_STATUS status=wifi_protocol->connect_network(wifi_protocol,&connect_token);
 if(status!=0){close_event(connect_token.event);connect_token.event=0;cleanup_supplicant();return -7;}
 connect_pending=1;
 if(!wait_for(&connect_done,30000))return -8;
 close_event(connect_token.event);connect_pending=0;connect_token.event=0;
 if(connect_token.status!=0||connect_token.result!=0){cleanup_supplicant();return -9;}
 return 0;
}
