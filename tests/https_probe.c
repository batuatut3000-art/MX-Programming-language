#include "mx_uefi.h"
#define ABI __attribute__((ms_abi))
static int tls_present,ca_present;
static EFI_STATUS ABI locate(EFI_GUID*guid,void*registration,void**interface){
 (void)registration;
 if(guid->a==0x952cb795&&tls_present){*interface=(void*)1;return 0;}
 return 0x800000000000000eULL;
}
static EFI_STATUS ABI get_variable(CHAR16*name,EFI_GUID*guid,uint32_t*attributes,UINTN*size,void*data){
 (void)name;(void)attributes;(void)data;
 if(guid->a==0xfd2340d0&&ca_present){*size=100;return 0x8000000000000005ULL;}
 return 0x800000000000000eULL;
}
void mx_poll(void){}
typedef struct {EFI_TABLE_HEADER hdr;void*fn[6];EFI_STATUS(ABI*get_variable)(CHAR16*,EFI_GUID*,uint32_t*,UINTN*,void*);} RT;
static EFI_BOOT_SERVICES bs;static RT rt;static EFI_SYSTEM_TABLE st;
int main(void){
 bs.locate_protocol=locate;rt.get_variable=get_variable;st.boot_services=&bs;st.runtime_services=&rt;
 if(mx_https_capabilities(&st)!=0)return 1;
 tls_present=1;if(mx_https_capabilities(&st)!=1)return 2;
 ca_present=1;if(mx_https_capabilities(&st)!=3)return 3;
 tls_present=0;if(mx_https_capabilities(&st)!=2)return 4;
 return 0;
}
