#include "mx_uefi.h"
#define ABI __attribute__((ms_abi))
#define MAX_BIN (1024*1024)

static EFI_SYSTEM_TABLE*table;
void mx_lowlevel_init(EFI_SYSTEM_TABLE*t){table=t;}

uint8_t mx_peek8(uint64_t a){return *(volatile uint8_t*)(uintptr_t)a;}
uint16_t mx_peek16(uint64_t a){return *(volatile uint16_t*)(uintptr_t)a;}
uint32_t mx_peek32(uint64_t a){return *(volatile uint32_t*)(uintptr_t)a;}
void mx_poke8(uint64_t a,uint8_t v){*(volatile uint8_t*)(uintptr_t)a=v;}
void mx_poke16(uint64_t a,uint16_t v){*(volatile uint16_t*)(uintptr_t)a=v;}
void mx_poke32(uint64_t a,uint32_t v){*(volatile uint32_t*)(uintptr_t)a=v;}
uint8_t mx_in8(uint16_t p){uint8_t v;__asm__ __volatile__("inb %1,%0":"=a"(v):"Nd"(p));return v;}
void mx_out8(uint16_t p,uint8_t v){__asm__ __volatile__("outb %0,%1"::"a"(v),"Nd"(p));}
void mx_sleep(uint64_t ms){EFI_BOOT_SERVICES*bs=table?(EFI_BOOT_SERVICES*)table->boot_services:0;if(!bs||!bs->fn[28])return;EFI_STATUS(ABI*stall)(UINTN)=(void*)bs->fn[28];while(ms){UINTN n=ms>1000?1000:(UINTN)ms;stall(n*1000);ms-=n;}}
uint64_t mx_alloc(uint64_t bytes){EFI_BOOT_SERVICES*bs=table?(EFI_BOOT_SERVICES*)table->boot_services:0;if(!bs||!bs->fn[5]||!bytes||bytes>16*1024*1024)return 0;EFI_STATUS(ABI*alloc)(uint32_t,UINTN,void**)=(void*)bs->fn[5];void*p=0;return alloc(2,bytes,&p)==0?(uint64_t)(uintptr_t)p:0;}
void mx_free(uint64_t address){EFI_BOOT_SERVICES*bs=table?(EFI_BOOT_SERVICES*)table->boot_services:0;if(!bs||!bs->fn[6]||!address)return;EFI_STATUS(ABI*release)(void*)=(void*)bs->fn[6];release((void*)(uintptr_t)address);}

typedef struct MX_FILE MX_FILE;
struct MX_FILE {uint64_t revision;EFI_STATUS(ABI*open)(MX_FILE*,MX_FILE**,CHAR16*,uint64_t,uint64_t);EFI_STATUS(ABI*close)(MX_FILE*);void*delete_file;EFI_STATUS(ABI*read)(MX_FILE*,UINTN*,void*);};
typedef struct {uint64_t revision;EFI_STATUS(ABI*open_volume)(void*,MX_FILE**);} MX_FS;
static void number_text(char*out,int64_t n){char b[24];int i=0,j=0;uint64_t v=n<0?(uint64_t)(-(n+1))+1:(uint64_t)n;if(n<0)out[j++]='-';do{b[i++]=(char)('0'+v%10);v/=10;}while(v&&i<23);while(i)out[j++]=b[--i];out[j]=0;}
void mx_print_number(int64_t value){char s[24];number_text(s,value);mx_print(s);}
static void report(const char*prefix,int64_t n){char msg[96];int i=0;while(prefix[i]&&i<60){msg[i]=prefix[i];i++;}number_text(msg+i,n);mx_print(msg);}

int64_t mx_runbin(const char*path){
 EFI_BOOT_SERVICES*bs=table?(EFI_BOOT_SERVICES*)table->boot_services:0;
 if(!bs||!path||!path[0]){mx_print("BIN: invalid path");return -1;}
 EFI_GUID fs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
 EFI_STATUS(ABI*locate)(uint32_t,EFI_GUID*,void*,UINTN*,EFI_HANDLE**)=(void*)bs->fn[36];
 EFI_STATUS(ABI*protocol)(EFI_HANDLE,EFI_GUID*,void**)=(void*)bs->fn[16];
 EFI_STATUS(ABI*alloc)(uint32_t,uint32_t,UINTN,uint64_t*)=(void*)bs->fn[2];
 EFI_STATUS(ABI*free_pages)(uint64_t,UINTN)=(void*)bs->fn[3];
 EFI_STATUS(ABI*free_pool)(void*)=(void*)bs->fn[6];
 if(!locate||!protocol||!alloc||!free_pages||!free_pool){mx_print("BIN: missing UEFI file services");return -2;}
 CHAR16 wide[128];int len=0;while(path[len]&&len<126){char c=path[len];wide[len]=(CHAR16)(c=='/'?'\\':c);len++;}if(path[len]){mx_print("BIN: path too long");return -3;}wide[len]=0;
 EFI_HANDLE*handles=0;UINTN count=0;if(locate(2,&fs_guid,0,&count,&handles)!=0||!handles){mx_print("BIN: no EFI filesystem");return -4;}
 MX_FILE*file=0;MX_FILE*root=0;
 for(UINTN k=0;k<count;k++){MX_FS*fs=0;if(protocol(handles[k],&fs_guid,(void**)&fs)!=0||!fs||!fs->open_volume)continue;if(fs->open_volume(fs,&root)!=0||!root)continue;if(root->open&&root->open(root,&file,wide,1,0)==0&&file){root->close(root);break;}root->close(root);root=0;}
 free_pool(handles);
 if(!file){mx_print("BIN: file not found on EFI volumes");return -5;}
 uint64_t address=0;UINTN pages=(MAX_BIN+4095)/4096;
 if(alloc(0,1,pages,&address)!=0||!address){file->close(file);mx_print("BIN: no executable memory");return -6;}
 UINTN size=MAX_BIN;EFI_STATUS status=file->read(file,&size,(void*)(uintptr_t)address);file->close(file);
 if(status!=0||size==0||size==MAX_BIN){free_pages(address,pages);mx_print("BIN: empty, too large or read failed");return -7;}
 uint64_t(ABI*entry)(void)=(void*)(uintptr_t)address;
 uint64_t result=entry();free_pages(address,pages);report("BIN returned ",(int64_t)result);return (int64_t)result;
}
