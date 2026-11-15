#include "mx_uefi.h"
#define ABI __attribute__((ms_abi))
#define MAX_NICS 8
#define MAX_FRAME 2048

typedef struct {uint8_t bytes[32];} MX_MAC;
typedef struct {
 uint32_t state,hw_address_size,media_header_size,max_packet_size;
 uint32_t nvram_size,nvram_access_size,filter_mask,filter_setting;
 uint32_t max_multicast,multicast_count;
 MX_MAC multicast[16],current,broadcast,permanent;
 uint8_t if_type,mac_changeable,multiple_tx,media_supported,media_present;
} MX_SNP_MODE;
typedef struct MX_SNP MX_SNP;
struct MX_SNP {
 uint64_t revision;
 EFI_STATUS(ABI*start)(MX_SNP*);
 EFI_STATUS(ABI*stop)(MX_SNP*);
 EFI_STATUS(ABI*initialize)(MX_SNP*,UINTN,UINTN);
 void*reset;
 EFI_STATUS(ABI*shutdown)(MX_SNP*);
 EFI_STATUS(ABI*receive_filters)(MX_SNP*,uint32_t,uint32_t,uint8_t,UINTN,MX_MAC*);
 void*station_address,*statistics,*multicast_ip_to_mac,*nvram;
 EFI_STATUS(ABI*get_status)(MX_SNP*,uint32_t*,void**);
 EFI_STATUS(ABI*transmit)(MX_SNP*,UINTN,UINTN,void*,MX_MAC*,MX_MAC*,uint16_t*);
 EFI_STATUS(ABI*receive)(MX_SNP*,UINTN*,UINTN*,void*,MX_MAC*,MX_MAC*,uint16_t*);
 void*wait_for_packet;
 MX_SNP_MODE*mode;
};
_Static_assert(__builtin_offsetof(MX_SNP_MODE,current)==552,"UEFI SNP mode ABI mismatch");
_Static_assert(__builtin_offsetof(MX_SNP_MODE,if_type)==648,"UEFI SNP ifType ABI mismatch");
_Static_assert(sizeof(MX_SNP)==128,"UEFI SNP protocol ABI mismatch");

typedef struct {MX_SNP*snp;char mac[18];uint8_t tx[MAX_FRAME];uint8_t pending,owned;} MX_NIC;
static MX_NIC nics[MAX_NICS];
static int nic_count;
static EFI_SYSTEM_TABLE*nic_table;
static EFI_GUID snp_guid={0xa19832b9,0xac25,0x11d3,{0x9a,0x2d,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
void mx_ethernet_init(EFI_SYSTEM_TABLE*table){nic_table=table;}
int mx_ethernet_count(void){return nic_count;}
static MX_NIC*get_nic(int index){return index>=0&&index<nic_count?&nics[index]:0;}
static void format_mac(char*out,const uint8_t*bytes){const char*hex="0123456789ABCDEF";int pos=0;for(int i=0;i<6;i++){out[pos++]=hex[bytes[i]>>4];out[pos++]=hex[bytes[i]&15];if(i<5)out[pos++]=':';}out[pos]=0;}
const char*mx_ethernet_mac(int index){MX_NIC*nic=get_nic(index);return nic?nic->mac:"";}
int mx_ethernet_scan(void){
 EFI_BOOT_SERVICES*bs=nic_table?(EFI_BOOT_SERVICES*)nic_table->boot_services:0;
 if(!bs)return -1;
 if(nic_count>0)return nic_count;
 EFI_STATUS(ABI*locate)(uint32_t,EFI_GUID*,void*,UINTN*,EFI_HANDLE**)=(void*)bs->fn[36];
 EFI_STATUS(ABI*protocol)(EFI_HANDLE,EFI_GUID*,void**)=(void*)bs->fn[16];
 EFI_STATUS(ABI*release)(void*)=(void*)bs->fn[6];
 if(!locate||!protocol||!release)return -1;
 EFI_HANDLE*handles=0;UINTN count=0;
 if(locate(2,&snp_guid,0,&count,&handles)!=0||!handles){nic_count=0;return 0;}
 nic_count=0;
 for(UINTN i=0;i<count&&nic_count<MAX_NICS;i++){
  MX_SNP*snp=0;if(protocol(handles[i],&snp_guid,(void**)&snp)!=0||!snp||!snp->mode)continue;
  MX_SNP_MODE*mode=snp->mode;
  if(mode->if_type!=1||mode->hw_address_size!=6||mode->media_header_size<14)continue;
  int duplicate=0;for(int j=0;j<nic_count;j++)if(nics[j].snp==snp)duplicate=1;
  if(duplicate)continue;
  MX_NIC*nic=&nics[nic_count++];nic->snp=snp;nic->owned=0;nic->pending=0;
  format_mac(nic->mac,mode->current.bytes);
 }
 release(handles);return nic_count;
}
int mx_ethernet_link(int index){
 MX_NIC*nic=get_nic(index);if(!nic||!nic->snp||!nic->snp->mode)return -1;
 MX_SNP*snp=nic->snp;
 if(snp->mode->state==2&&snp->get_status)snp->get_status(snp,0,0);
 return snp->mode->media_supported?(snp->mode->media_present?1:0):-1;
}
int mx_ethernet_open(int index){
 MX_NIC*nic=get_nic(index);if(!nic||!nic->snp||!nic->snp->mode)return -1;
 MX_SNP*snp=nic->snp;
 if(snp->mode->state==2)return 0;
 if(snp->mode->state==0){if(!snp->start||snp->start(snp)!=0)return -2;nic->owned=2;}
 if(snp->mode->state==1){if(!snp->initialize||snp->initialize(snp,0,0)!=0)return -3;if(!nic->owned)nic->owned=1;}
 if(snp->mode->state!=2)return -3;
 if(nic->owned==2&&snp->receive_filters)snp->receive_filters(snp,0x01|0x04,0,0,0,0);
 return 0;
}
static int recycle(MX_NIC*nic){
 if(!nic->pending)return 0;
 MX_SNP*snp=nic->snp;
 if(!snp||!snp->get_status)return -1;
 for(int t=0;t<1000;t++){
  void*done=0;if(snp->get_status(snp,0,&done)!=0)return -1;
  if(done==nic->tx){nic->pending=0;return 0;}
  mx_step();
 }
 return -2;
}
int mx_ethernet_send(int index,uint64_t address,uint64_t length){
 MX_NIC*nic=get_nic(index);if(!nic||!nic->snp||!nic->snp->transmit||!address||length<14||length>MAX_FRAME)return -1;
 if(mx_ethernet_open(index)!=0)return -2;
 MX_SNP*snp=nic->snp;
 if(length>(uint64_t)snp->mode->max_packet_size+snp->mode->media_header_size)return -3;
 if(recycle(nic)!=0)return -4;
 const uint8_t*src=(const uint8_t*)(uintptr_t)address;
 for(UINTN i=0;i<length;i++)nic->tx[i]=src[i];
 if(snp->transmit(snp,0,(UINTN)length,nic->tx,0,0,0)!=0)return -5;
 nic->pending=1;
 return recycle(nic)==0?0:-6;
}
int mx_ethernet_receive(int index,uint64_t address,uint64_t capacity){
 MX_NIC*nic=get_nic(index);if(!nic||!nic->snp||!nic->snp->receive||!address||capacity<14||capacity>65536)return -1;
 if(mx_ethernet_open(index)!=0)return -2;
 UINTN size=(UINTN)capacity;EFI_STATUS status=nic->snp->receive(nic->snp,0,&size,(void*)(uintptr_t)address,0,0,0);
 if(status==0)return (int)size;
 if((status&0xffff)==6)return 0; /* EFI_NOT_READY: no packet waiting */
 return -3;
}
int mx_ethernet_close(int index){
 MX_NIC*nic=get_nic(index);if(!nic||!nic->snp)return -1;
 if(!nic->owned)return 0;
 if(recycle(nic)!=0)return -2;
 MX_SNP*snp=nic->snp;
 if(snp->shutdown&&snp->mode&&snp->mode->state==2)snp->shutdown(snp);
 if(nic->owned==2&&snp->stop&&snp->mode&&snp->mode->state==1)snp->stop(snp);
 nic->owned=0;return 0;
}
