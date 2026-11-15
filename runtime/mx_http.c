#include "mx_uefi.h"
#include "mx_browser_text.h"
#define ABI __attribute__((ms_abi))
typedef struct HTTP HTTP;
typedef struct {EFI_STATUS(ABI*create)(void*,EFI_HANDLE*);EFI_STATUS(ABI*destroy)(void*,EFI_HANDLE);} HTTP_BINDING;
typedef struct {uint8_t default_address,ip[4],subnet[4];uint16_t port;} HTTP_V4;
typedef struct {uint32_t version,timeout;uint8_t ipv6;HTTP_V4*access;} HTTP_CONFIG;
typedef struct {uint32_t method;CHAR16*url;} HTTP_REQUEST;
typedef struct {uint32_t status;} HTTP_RESPONSE;
typedef struct {char*name,*value;} HTTP_HEADER;
typedef struct {void*data;UINTN header_count;HTTP_HEADER*headers;UINTN body_length;void*body;} HTTP_MESSAGE;
typedef struct {void*event;EFI_STATUS status;HTTP_MESSAGE*message;} HTTP_TOKEN;
typedef struct {uint16_t scan,unicode;} HTTP_KEY;
typedef struct HTTP_INPUT HTTP_INPUT;
struct HTTP_INPUT {void*reset;EFI_STATUS(ABI*read)(HTTP_INPUT*,HTTP_KEY*);void*wait;};
typedef struct IP4_CONFIG2 IP4_CONFIG2;
struct IP4_CONFIG2 {
 EFI_STATUS(ABI*set_data)(IP4_CONFIG2*,uint32_t,UINTN,void*);
 EFI_STATUS(ABI*get_data)(IP4_CONFIG2*,uint32_t,UINTN*,void*);
 void*register_notify,*unregister_notify;
};
typedef struct {
 CHAR16 name[32];uint8_t if_type;uint32_t hw_address_size;uint8_t hw_address[32];
 uint8_t station_address[4],subnet_mask[4];uint32_t route_table_size;void*route_table;
} IP4_INFO;
_Static_assert(sizeof(HTTP_V4)==12&&sizeof(HTTP_CONFIG)==24,"UEFI HTTP config ABI mismatch");
_Static_assert(sizeof(HTTP_MESSAGE)==40&&sizeof(HTTP_TOKEN)==24,"UEFI HTTP token ABI mismatch");
_Static_assert(__builtin_offsetof(IP4_INFO,station_address)==104&&sizeof(IP4_INFO)==128,"UEFI IPv4 config ABI mismatch");
struct HTTP {void*mode;EFI_STATUS(ABI*configure)(HTTP*,HTTP_CONFIG*);EFI_STATUS(ABI*request)(HTTP*,HTTP_TOKEN*);EFI_STATUS(ABI*cancel)(HTTP*,HTTP_TOKEN*);EFI_STATUS(ABI*response)(HTTP*,HTTP_TOKEN*);EFI_STATUS(ABI*poll)(HTTP*);};
static EFI_STATUS last_efi_status;
static uint32_t last_http_status;
EFI_STATUS mx_http_last_efi_status(void){return last_efi_status;}
uint32_t mx_http_last_http_status(void){return last_http_status;}
int mx_https_capabilities(EFI_SYSTEM_TABLE*table){
 if(!table||!table->boot_services)return 0;
 int capabilities=0;
 EFI_GUID tls_guid={0x952cb795,0xff36,0x48cf,{0xa2,0x49,0x4d,0xf4,0x86,0xd6,0xab,0x8d}};
 EFI_BOOT_SERVICES*bs=(EFI_BOOT_SERVICES*)table->boot_services;void*tls=0;
 if(bs->locate_protocol&&bs->locate_protocol(&tls_guid,0,&tls)==0&&tls)capabilities|=1;
 if(table->runtime_services){
  typedef struct {EFI_TABLE_HEADER hdr;void*fn[6];EFI_STATUS(ABI*get_variable)(CHAR16*,EFI_GUID*,uint32_t*,UINTN*,void*);} VARIABLE_SERVICES;
  VARIABLE_SERVICES*rt=(VARIABLE_SERVICES*)table->runtime_services;
  if(rt->get_variable){
   EFI_GUID ca_guid={0xfd2340d0,0x3dab,0x4349,{0xa6,0xc7,0x3b,0x4f,0x12,0xb4,0x8e,0xae}};
   CHAR16 name[]={'T','l','s','C','a','C','e','r','t','i','f','i','c','a','t','e',0};
   UINTN size=1;uint8_t value=0;
   EFI_STATUS status=rt->get_variable(name,&ca_guid,0,&size,&value);
   if((status==0x8000000000000005ULL||status==0)&&size>0)capabilities|=2;
  }
 }
 return capabilities;
}
UINTN mx_https_ca_bytes(EFI_SYSTEM_TABLE*table){
 if(!table||!table->runtime_services)return 0;
 typedef struct {EFI_TABLE_HEADER hdr;void*fn[6];EFI_STATUS(ABI*get_variable)(CHAR16*,EFI_GUID*,uint32_t*,UINTN*,void*);} VARIABLE_SERVICES;
 VARIABLE_SERVICES*rt=(VARIABLE_SERVICES*)table->runtime_services;
 if(!rt->get_variable)return 0;
 EFI_GUID ca_guid={0xfd2340d0,0x3dab,0x4349,{0xa6,0xc7,0x3b,0x4f,0x12,0xb4,0x8e,0xae}};
 CHAR16 name[]={'T','l','s','C','a','C','e','r','t','i','f','i','c','a','t','e',0};
 UINTN size=0;
 EFI_STATUS status=rt->get_variable(name,&ca_guid,0,&size,0);
 return status==0x8000000000000005ULL?size:0;
}
int mx_https_clock_year(EFI_SYSTEM_TABLE*table){
 if(!table||!table->runtime_services)return 0;
 typedef struct {uint16_t year;uint8_t month,day,hour,minute,second,pad1;uint32_t nanosecond;int16_t timezone;uint8_t daylight,pad2;} EFI_TIME_VALUE;
 typedef struct {EFI_TABLE_HEADER hdr;EFI_STATUS(ABI*get_time)(EFI_TIME_VALUE*,void*);} TIME_SERVICES;
 TIME_SERVICES*rt=(TIME_SERVICES*)table->runtime_services;
 EFI_TIME_VALUE value={0};
 return rt->get_time&&rt->get_time(&value,0)==0?(int)value.year:0;
}
static int same_header(const char*a,const char*b){while(*a&&*b){char x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}return *a==0&&*b==0;}
static int copy_url(char*out,const char*value){int n=0;if(!value)return 0;while(value[n]&&n<255){out[n]=value[n];n++;}out[n]=0;return value[n]==0;}
static int extract_host(const char*url,char*out){
 const char*start=url;
 if(url[4]=='s')start+=8;else start+=7;
 int n=0;
 while(start[n]&&start[n]!='/'&&start[n]!='?'&&start[n]!='#'&&n<254){out[n]=start[n];n++;}
 out[n]=0;
 return n>0&&n<254;
}
static int resolve_location(const char*base,const char*location,char*out){
 if(!location||!location[0])return 0;
 if((location[0]=='h'&&location[1]=='t'&&location[2]=='t'&&location[3]=='p'&&location[4]==':')||(location[0]=='h'&&location[1]=='t'&&location[2]=='t'&&location[3]=='p'&&location[4]=='s'&&location[5]==':'))return copy_url(out,location);
 int scheme=0;while(base[scheme]&&base[scheme]!=':')scheme++;if(!base[scheme])return 0;
 int host=scheme+3;int end=host;while(base[end]&&base[end]!='/')end++;
 int prefix=end;
 if(location[0]!='/'){prefix=end;if(base[end]=='/')prefix=end+1;for(int i=end+1;base[i]&&i<255;i++)if(base[i]=='/')prefix=i+1;}
 if(prefix>255)return 0;
 for(int i=0;i<prefix;i++)out[i]=base[i];
 int j=0;while(location[j]&&prefix+j<255){out[prefix+j]=location[j];j++;}out[prefix+j]=0;return location[j]==0;
}
static void ABI complete(void*event,void*context){(void)event;*(volatile int*)context=1;}
static int ipv4_ready(IP4_CONFIG2*config){
 uint8_t storage[2048];UINTN size=sizeof(storage);
 EFI_STATUS status=config->get_data(config,0,&size,storage);
 if(status!=0||size<sizeof(IP4_INFO))return 0;
 IP4_INFO*info=(IP4_INFO*)storage;
 return info->station_address[0]||info->station_address[1]||info->station_address[2]||info->station_address[3];
}
static int prepare_ipv4(EFI_BOOT_SERVICES*bs,EFI_HANDLE controller,EFI_STATUS(ABI*stall)(UINTN),HTTP_INPUT*input){
 EFI_GUID ip4_guid={0x5b446ed1,0xe30b,0x4faa,{0x87,0x1a,0x36,0x54,0xec,0xa3,0x60,0x80}};
 EFI_STATUS(ABI*handle_protocol)(EFI_HANDLE,EFI_GUID*,void**)=(void*)bs->fn[16];
 IP4_CONFIG2*config=0;
 if(handle_protocol&&controller)handle_protocol(controller,&ip4_guid,(void**)&config);
 if(!config&&bs->locate_protocol)bs->locate_protocol(&ip4_guid,0,(void**)&config);
 if(!config||!config->get_data||!config->set_data){last_efi_status=0x8000000000000011ULL;return -18;}
 if(ipv4_ready(config))return 0;
 uint32_t policy=0;UINTN size=sizeof(policy);
 EFI_STATUS status=config->get_data(config,1,&size,&policy);
 if(status!=0||policy!=1){
  policy=1;
  status=config->set_data(config,1,sizeof(policy),&policy);
  if(status!=0){last_efi_status=status;return -18;}
 }
 for(int ms=0;ms<10000;ms+=10){
  if(ipv4_ready(config))return 0;
  mx_poll();
  HTTP_KEY key={0,0};
  if(input&&input->read&&input->read(input,&key)==0&&(key.scan==0x17||key.unicode==27))return -17;
  if(stall)stall(10000);
 }
 last_efi_status=0x8000000000000011ULL;return -18;
}
static int wait_token(HTTP*http,HTTP_TOKEN*token,volatile int*done,EFI_STATUS(ABI*stall)(UINTN),HTTP_INPUT*input,int timeout_ms){
 for(int ms=0;ms<timeout_ms&&!(*done);ms++){
  http->poll(http);mx_poll();
  HTTP_KEY key={0,0};
  if(input&&input->read&&input->read(input,&key)==0&&(key.scan==0x17||key.unicode==27)){
   http->cancel(http,token);last_efi_status=0;return -17;
  }
  if(stall)stall(1000);
 }
 if(!*done){http->cancel(http,token);last_efi_status=0x8000000000000012ULL;return -1;}
 last_efi_status=token->status;return token->status==0?0:-2;
}
static int http_once(EFI_SYSTEM_TABLE*table,const char*url,char*body,UINTN capacity,UINTN*used,char*redirect){
 if(used)*used=0;if(!body||capacity<2||!table||!table->boot_services||!url)return -1;
 if(!((url[0]=='h'&&url[1]=='t'&&url[2]=='t'&&url[3]=='p'&&url[4]==':'&&url[5]=='/'&&url[6]=='/')||(url[0]=='h'&&url[1]=='t'&&url[2]=='t'&&url[3]=='p'&&url[4]=='s'&&url[5]==':'&&url[6]=='/'&&url[7]=='/')))return -2;
 EFI_BOOT_SERVICES*bs=(EFI_BOOT_SERVICES*)table->boot_services;
 EFI_GUID binding_guid={0xbdc8e6af,0xd9bc,0x4379,{0xa7,0x2a,0xe0,0xc4,0xe7,0x5d,0xae,0x1c}};
 EFI_GUID http_guid={0x7a59b29b,0x910b,0x4171,{0x82,0x42,0xa8,0x5a,0x0d,0xf2,0x5b,0x5b}};
 EFI_STATUS(ABI*create_event)(uint32_t,UINTN,void(ABI*)(void*,void*),void*,void**)=(void*)bs->fn[7];
 EFI_STATUS(ABI*close_event)(void*)=(void*)bs->fn[11];
 EFI_STATUS(ABI*handle_protocol)(EFI_HANDLE,EFI_GUID*,void**)=(void*)bs->fn[16];
 EFI_STATUS(ABI*locate_handles)(uint32_t,EFI_GUID*,void*,UINTN*,EFI_HANDLE**)=(void*)bs->fn[36];
 EFI_STATUS(ABI*free_pool)(void*)=(void*)bs->fn[6];
 EFI_STATUS(ABI*stall)(UINTN)=(void*)bs->fn[28];
 HTTP_BINDING*binding=0;HTTP*http=0;EFI_HANDLE child=0;
 EFI_HANDLE controller=0;
 void*request_event=0,*response_event=0;int rc=-3;
 if(!bs->locate_protocol||!create_event||!close_event||!handle_protocol||!free_pool)return rc;
 if(locate_handles){
  EFI_HANDLE*handles=0;UINTN handle_count=0;
  if(locate_handles(2,&binding_guid,0,&handle_count,&handles)==0&&handles&&handle_count){
   controller=handles[0];handle_protocol(controller,&binding_guid,(void**)&binding);
   free_pool(handles);
  }
 }
 if(!binding){last_efi_status=bs->locate_protocol(&binding_guid,0,(void**)&binding);if(last_efi_status!=0||!binding)return rc;}
 rc=prepare_ipv4(bs,controller,stall,(HTTP_INPUT*)table->con_in);
 if(rc!=0)return rc;
 last_efi_status=binding->create(binding,&child);if(last_efi_status!=0||!child)return -4;
 last_efi_status=handle_protocol(child,&http_guid,(void**)&http);if(last_efi_status!=0||!http){rc=-5;goto finish;}
 HTTP_V4 ipv4={0};ipv4.default_address=1;
 HTTP_CONFIG cfg={0};cfg.version=1;cfg.timeout=url[4]=='s'?15000:6000;cfg.access=&ipv4;
 last_efi_status=http->configure(http,&cfg);if(last_efi_status!=0){rc=-6;goto finish;}
 char host[256];if(!extract_host(url,host)){rc=-7;goto finish;}
 UINTN host_end=(url[4]=='s'?8:7);
 while(url[host_end]&&url[host_end]!='/'&&url[host_end]!='?'&&url[host_end]!='#')host_end++;
 if(url[host_end]=='?'||url[host_end]=='#'){rc=-7;goto finish;}
 CHAR16 wide[256];UINTN i=0;for(;url[i]&&i<254;i++)wide[i]=(uint8_t)url[i];if(url[i]){rc=-7;goto finish;}
 if(url[host_end]==0)wide[i++]='/';
 wide[i]=0;
 HTTP_REQUEST request={0,wide};HTTP_HEADER request_headers[1]={{"Host",host}};
 HTTP_MESSAGE req={0};req.data=&request;req.header_count=1;req.headers=request_headers;
 HTTP_TOKEN req_token={0};req_token.message=&req;volatile int req_done=0;
 last_efi_status=create_event(0x200,8,complete,(void*)&req_done,&request_event);if(last_efi_status!=0){rc=-8;goto finish;}
 req_token.event=request_event;
 last_efi_status=http->request(http,&req_token);if(last_efi_status!=0){rc=-9;goto finish;}
 {int wait=wait_token(http,&req_token,&req_done,stall,(HTTP_INPUT*)table->con_in,cfg.timeout);if(wait!=0){rc=wait==-17?-17:-9;goto finish;}}
 HTTP_RESPONSE response={0};HTTP_MESSAGE resp={0};resp.data=&response;resp.body=body;resp.body_length=capacity-1;
 HTTP_TOKEN resp_token={0};resp_token.message=&resp;volatile int resp_done=0;
 last_efi_status=create_event(0x200,8,complete,(void*)&resp_done,&response_event);if(last_efi_status!=0){rc=-10;goto finish;}
 resp_token.event=response_event;
 last_efi_status=http->response(http,&resp_token);if(last_efi_status!=0){rc=-11;goto finish;}
 {int wait=wait_token(http,&resp_token,&resp_done,stall,(HTTP_INPUT*)table->con_in,cfg.timeout);if(wait!=0){rc=wait==-17?-17:-11;goto finish;}}
 last_http_status=response.status;
 if(resp.body_length==0&&response.status==3){
  if(resp.headers){free_pool(resp.headers);resp.headers=0;}
  resp.header_count=0;resp.data=0;resp.body_length=capacity-1;resp_done=0;
  last_efi_status=http->response(http,&resp_token);if(last_efi_status!=0){rc=-11;goto finish;}
  {int wait=wait_token(http,&resp_token,&resp_done,stall,(HTTP_INPUT*)table->con_in,cfg.timeout);if(wait!=0){rc=wait==-17?-17:-11;goto finish;}}
 }
 body[resp.body_length]=0;
 size_t body_size=(size_t)resp.body_length;
 mx_http_dechunk(body,&body_size);
 if(used)*used=(UINTN)body_size;
 rc=(response.status==3)?0:-12;
 if((response.status==11||response.status==12||response.status==13||response.status==16)&&resp.headers){
  for(UINTN h=0;h<resp.header_count;h++)if(resp.headers[h].name&&same_header(resp.headers[h].name,"Location")){
   if(resolve_location(url,resp.headers[h].value,redirect))rc=-15;
   break;
  }
 }
 if(resp.headers)free_pool(resp.headers);
finish:
 if(response_event)close_event(response_event);
 if(request_event)close_event(request_event);
 if(http&&http->configure)http->configure(http,0);
 binding->destroy(binding,child);
 return rc;
}
int mx_http_get(EFI_SYSTEM_TABLE*table,const char*url,char*body,UINTN capacity,UINTN*used){
 char current[256],next[256];last_efi_status=0;last_http_status=0;
 if(!copy_url(current,url))return -7;
 for(int attempt=0;attempt<5;attempt++){
  next[0]=0;int rc=http_once(table,current,body,capacity,used,next);
  if(rc!=-15)return rc;
  if(!next[0])return -12;
  if(!copy_url(current,next))return -7;
 }
 return -16;
}
