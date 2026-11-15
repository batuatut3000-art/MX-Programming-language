#include "mx_uefi.h"
#include "font8x16.h"
#include "mx_browser_text.h"
#include "mx_browser_layout.h"
#include "mx_browser_js.h"
#define ABI __attribute__((ms_abi))
#define CURSOR_W 21
#define CURSOR_H 28
static EFI_SYSTEM_TABLE *st;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
typedef EFI_STATUS (ABI *EFI_STALL)(UINTN);
static EFI_STALL stall_us;
static uint32_t *backbuffer;
static uint64_t backbuffer_pixels;
typedef struct {char name[32];int x,y,w,h,r,visible;uint32_t color;} OBJ;
static OBJ objs[48];static int count=0;
typedef struct {char name[32];int x,y,visible;uint32_t color;char value[96];} TEXT_OBJ;
static TEXT_OBJ texts[48];static int text_count=0;
typedef struct {OBJ*owner;int dx,dy;char value[96];} LABEL;
static LABEL labels[48];static int label_count=0;
static uint32_t bg=0x0c1423;
static char message[96],prompt_text[96],input_text[96];
static char browser_url[256],browser_raw[32768],browser_plain[32768];
static MX_WEB_PAGE browser_page;
static char browser_js_title[112];
static char browser_previous[256];
static int browser_active=0,browser_scroll=0;
static int wifi_active=0,wifi_password_mode=0;
static char wifi_status[128],secret_text[96];
static int ethernet_active=0,ethernet_display_count=0;
static char ethernet_status[128],ethernet_lines[8][96];
static int input_length=0,input_overlay=0;
static int last_key=0;
static int mouse_enabled=0,mouse_x=512,mouse_y=384,mouse_down=0,mouse_click=0;
static int mouse_dpi=1600;
static int mouse_source=0,mouse_events=0,mouse_idle=0,ps2_tried=0;
static int mouse_pointer_device=-1;
static int64_t mouse_remainder_x=0,mouse_remainder_y=0;
static int64_t mouse_relative_remainder_x=0,mouse_relative_remainder_y=0;
static int absolute_scan_start=0,pointer_scan_start=0,usb_scan_start=0;
static unsigned pointer_class_turn;
static char mouse_status[48];
typedef struct MX_POINTER MX_POINTER;
typedef struct {uint64_t resolution_x,resolution_y,resolution_z;uint8_t left_button,right_button;} MX_POINTER_MODE;
typedef struct {int32_t x,y,z;uint8_t left_button,right_button;} MX_POINTER_STATE;
struct MX_POINTER {EFI_STATUS (ABI *reset)(MX_POINTER*,uint8_t);EFI_STATUS (ABI *get_state)(MX_POINTER*,MX_POINTER_STATE*);void*wait;MX_POINTER_MODE*mode;};
#define MAX_POINTER_DEVICES 16
static MX_POINTER*pointers[MAX_POINTER_DEVICES];
static int pointer_count;
typedef struct MX_ABSOLUTE_POINTER MX_ABSOLUTE_POINTER;
typedef struct {uint64_t min_x,max_x,min_y,max_y,min_z,max_z;uint32_t attributes;} MX_ABSOLUTE_MODE;
typedef struct {uint64_t x,y,z;uint32_t buttons;} MX_ABSOLUTE_STATE;
struct MX_ABSOLUTE_POINTER {EFI_STATUS (ABI *reset)(MX_ABSOLUTE_POINTER*,uint8_t);EFI_STATUS (ABI *get_state)(MX_ABSOLUTE_POINTER*,MX_ABSOLUTE_STATE*);void*wait;MX_ABSOLUTE_MODE*mode;};
static MX_ABSOLUTE_POINTER*absolute_pointers[MAX_POINTER_DEVICES];
static int absolute_count;
static MX_ABSOLUTE_STATE absolute_last[MAX_POINTER_DEVICES];
static uint8_t absolute_seen[MAX_POINTER_DEVICES];
typedef struct MX_USB_IO MX_USB_IO;
typedef struct {uint8_t request_type,request;uint16_t value,index,length;} MX_USB_REQUEST;
typedef struct {uint8_t length,type,number,alternate,endpoints,device_class,subclass,protocol,string;} MX_USB_INTERFACE;
typedef struct {uint8_t length,type,address,attributes;uint16_t max_packet;uint8_t interval;} MX_USB_ENDPOINT;
typedef struct {uint8_t length,type;uint16_t usb_version;uint8_t device_class,subclass,protocol,max_packet_zero;uint16_t vendor,product,device_version;uint8_t manufacturer,product_string,serial,configurations;} MX_USB_DEVICE;
typedef EFI_STATUS (ABI *MX_USB_CALLBACK)(void*,UINTN,void*,uint32_t);
struct MX_USB_IO {
 EFI_STATUS (ABI *control)(MX_USB_IO*,MX_USB_REQUEST*,uint32_t,uint32_t,void*,UINTN,uint32_t*);
 void*bulk;
 EFI_STATUS (ABI *async_interrupt)(MX_USB_IO*,uint8_t,uint8_t,UINTN,UINTN,MX_USB_CALLBACK,void*);
 void*sync_interrupt,*isochronous,*async_isochronous;
 EFI_STATUS (ABI *get_device)(MX_USB_IO*,MX_USB_DEVICE*);
 void*get_config;
 EFI_STATUS (ABI *get_interface)(MX_USB_IO*,MX_USB_INTERFACE*);
 EFI_STATUS (ABI *get_endpoint)(MX_USB_IO*,uint8_t,MX_USB_ENDPOINT*);
};
#define MAX_USB_MICE 8
typedef struct {MX_USB_IO*io;uint8_t endpoint,interval;uint16_t packet_size;volatile int dx,dy,buttons,has_report,error_pending;int retry_countdown,retry_attempts;} MX_USB_MOUSE;
static MX_USB_MOUSE usb_mice[MAX_USB_MICE];
static int usb_count;
static MX_USB_IO*qemu_tablet;
static uint8_t qemu_tablet_interface;
static unsigned qemu_tablet_tick;
static int qemu_tablet_seen,qemu_tablet_x,qemu_tablet_y,qemu_tablet_buttons;
static int usb_io_count,usb_boot_count,usb_start_fail,usb_displayed_errors;
static volatile int usb_report_errors;
static char usb_status[80];
static EFI_STATUS ABI usb_mouse_report(void*,UINTN,void*,uint32_t);
static void usb_mouse_scan(void);
static int ps2_enabled=0,ps2_packet_pos=0;
static uint8_t ps2_packet[3];
static uint8_t port_read(uint16_t port){uint8_t value;__asm__ __volatile__("inb %1,%0":"=a"(value):"Nd"(port));return value;}
static void port_write(uint16_t port,uint8_t value){__asm__ __volatile__("outb %0,%1"::"a"(value),"Nd"(port));}
static int ps2_wait_write(void){for(int i=0;i<100000;i++)if(!(port_read(0x64)&2))return 1;return 0;}
static int ps2_command(uint8_t command){
 if(!ps2_wait_write())return 0;port_write(0x64,0xd4);
 if(!ps2_wait_write())return 0;port_write(0x60,command);
 for(int i=0;i<100000;i++){uint8_t status=port_read(0x64);if((status&0x21)==0x21)return port_read(0x60)==0xfa;}
 return 0;
}
static int ps2_start(void){
 if(!ps2_wait_write())return 0;port_write(0x64,0xa8);
 if(!ps2_command(0xea))return 0;
 return ps2_command(0xf4);
}
static int clip_on=0,clip_x,clip_y,clip_right,clip_bottom;
static int eq(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
int mx_streq(const char*a,const char*b){return eq(a,b);}
static void copy(char*d,const char*s,int cap){int i=0;while(s[i]&&i<cap-1){d[i]=s[i];i++;}d[i]=0;}
static int length(const char*s){int n=0;while(s[n])n++;return n;}
static void mouse_status_update(void){const char*mode=mouse_source==1?"simple":mouse_source==2?"absolute":mouse_source==3?"ps2":mouse_source==4?"usb":mouse_source==5?"tablet":"none";copy(mouse_status,"mouse: ",48);int n=length(mouse_status);while(*mode&&n<35)mouse_status[n++]=*mode++;mouse_status[n++]=' ';int v=mouse_events%10000;mouse_status[n++]=(char)('0'+v/1000);mouse_status[n++]=(char)('0'+v/100%10);mouse_status[n++]=(char)('0'+v/10%10);mouse_status[n++]=(char)('0'+v%10);mouse_status[n]=0;}
static void status_word(char*dst,int*n,const char*word){while(*word&&*n<78)dst[(*n)++]=*word++;}
static void status_number(char*dst,int*n,int value){
 char digits[12];int count=0;
 if(value<0)value=0;
 do{digits[count++]=(char)('0'+value%10);value/=10;}while(value&&count<11);
 while(count&&*n<78)dst[(*n)++]=digits[--count];
}
static void usb_status_update(void){
 int n=0;
 status_word(usb_status,&n,"USB io:");status_number(usb_status,&n,usb_io_count);
 status_word(usb_status,&n," hid:");status_number(usb_status,&n,usb_boot_count);
 status_word(usb_status,&n," on:");status_number(usb_status,&n,usb_count);
 status_word(usb_status,&n," tab:");status_number(usb_status,&n,qemu_tablet?1:0);
 status_word(usb_status,&n," fail:");status_number(usb_status,&n,usb_start_fail);
 status_word(usb_status,&n," err:");status_number(usb_status,&n,usb_report_errors);
 usb_status[n]=0;
}
static OBJ*find(const char*n){for(int i=0;i<count;i++)if(eq(n,objs[i].name))return &objs[i];return 0;}
static uint32_t encode(uint32_t c){if(gop->mode->info->pixel_format==0)return ((c&255)<<16)|(c&0xff00)|((c>>16)&255);return c;}
static void pixel(int x,int y,uint32_t c){if(!gop||!gop->mode||!gop->mode->info)return;if(clip_on&&(x<clip_x||y<clip_y||x>=clip_right||y>=clip_bottom))return;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*i=gop->mode->info;if(x<0||y<0||(uint32_t)x>=i->horizontal_resolution||(uint32_t)y>=i->vertical_resolution||i->pixel_format>1)return;uint64_t idx=(uint64_t)y*i->pixels_per_scan_line+x;if(idx>=gop->mode->framebuffer_size/4)return;uint32_t color=encode(c);if(backbuffer&&idx<backbuffer_pixels)backbuffer[idx]=color;else{volatile uint32_t*fb=(volatile uint32_t*)(uintptr_t)gop->mode->framebuffer_base;fb[idx]=color;}}
static void rect(int x,int y,int w,int h,uint32_t c,int r){
 if(w<=0||h<=0)return;
 if(r>w/2)r=w/2;if(r>h/2)r=h/2;
 int left=0,top=0,right=w,bottom=h;
 if(clip_on){
  if(left<clip_x-x)left=clip_x-x;
  if(top<clip_y-y)top=clip_y-y;
  if(right>clip_right-x)right=clip_right-x;
  if(bottom>clip_bottom-y)bottom=clip_bottom-y;
 }
 for(int j=top;j<bottom;j++)for(int i=left;i<right;i++){
  int dx=i<r?r-i-1:(i>=w-r?i-(w-r):0);
  int dy=j<r?r-j-1:(j>=h-r?j-(h-r):0);
  if(dx*dx+dy*dy<=r*r)pixel(x+i,y+j,c);
 }
}
static uint8_t cursor_mask[CURSOR_H][CURSOR_W];
static int cursor_ready;
static int cursor_inside(int x,int y,const int8_t points[][2],int count){
 int inside=0,px=2*x+1,py=2*y+1;
 for(int a=0,b=count-1;a<count;b=a++){
  int ax=2*points[a][0],ay=2*points[a][1];
  int bx=2*points[b][0],by=2*points[b][1];
  if((ay>py)!=(by>py)){
   int64_t lhs=(int64_t)(px-ax)*(by-ay),rhs=(int64_t)(bx-ax)*(py-ay);
   if((by-ay)>0?lhs<rhs:lhs>rhs)inside=!inside;
  }
 }
 return inside;
}
static void draw_cursor(void){
 if(!cursor_ready){
  static const int8_t outer[][2]={{1,0},{3,0},{20,15},{20,17},{14,17},{18,25},{18,27},{13,28},{11,26},{7,18},{3,22},{1,22},{0,21},{0,2}};
  static const int8_t inner[][2]={{2,2},{2,20},{7,16},{14,26},{16,25},{12,16},{18,16}};
  for(int y=0;y<CURSOR_H;y++)for(int x=0;x<CURSOR_W;x++){
   if(cursor_inside(x,y,outer,sizeof(outer)/sizeof(outer[0])))
    cursor_mask[y][x]=cursor_inside(x,y,inner,sizeof(inner)/sizeof(inner[0]))?2:1;
  }
  cursor_ready=1;
 }
 for(int y=0;y<CURSOR_H;y++)for(int x=0;x<CURSOR_W;x++){
  uint8_t ink=cursor_mask[y][x];
  if(ink)pixel(mouse_x+x,mouse_y+y,ink==2?0x000000:0xffffff);
 }
}
static void text(int x,int y,const char*s,uint32_t color){while(*s){unsigned char ch=(unsigned char)*s++;if(ch<32||ch>126)ch='?';const unsigned char*glyph=mx_font[ch-32];for(int j=0;j<16;j++)for(int i=0;i<8;i++)if(glyph[j]&(0x80>>i))pixel(x+i,y+j,color);x+=8;}}
static void text_zoom(int x,int y,const char*s,uint32_t color,int scale){
 if(scale<=1){text(x,y,s,color);return;}
 while(*s){
  unsigned char ch=(unsigned char)*s++;if(ch<32||ch>126)ch='?';
  const unsigned char*glyph=mx_font[ch-32];
  for(int j=0;j<16;j++)for(int i=0;i<8;i++)if(glyph[j]&(0x80>>i))
   for(int sy=0;sy<scale;sy++)for(int sx=0;sx<scale;sx++)pixel(x+i*scale+sx,y+j*scale+sy,color);
  x+=8*scale;
 }
}
void mx_add(const char*n,int x,int y,int w,int h,uint32_t c,int r){if(count>=48)return;OBJ*o=&objs[count++];copy(o->name,n,32);o->x=x;o->y=y;o->w=w;o->h=h;o->color=c;o->r=r;o->visible=1;}
void mx_text(const char*n,int x,int y,const char*s,uint32_t c){if(text_count>=48)return;TEXT_OBJ*t=&texts[text_count++];copy(t->name,n,32);copy(t->value,s,96);t->x=x;t->y=y;t->color=c;t->visible=1;}
void mx_visible(const char*n,int v){OBJ*o=find(n);if(o){if(o->visible!=v){o->visible=v;mx_present();}return;}for(int k=0;k<text_count;k++)if(eq(texts[k].name,n)){if(texts[k].visible!=v){texts[k].visible=v;mx_present();}return;}}
void mx_move(const char*n,int dx,int dy){OBJ*o=find(n);if(o){o->x+=dx;o->y+=dy;mx_present();return;}for(int k=0;k<text_count;k++)if(eq(texts[k].name,n)){texts[k].x+=dx;texts[k].y+=dy;mx_present();return;}}
void mx_mouse(int dpi){
 if(dpi<100)dpi=100;if(dpi>12800)dpi=12800;
 mouse_dpi=dpi;
 mouse_remainder_x=mouse_remainder_y=0;
 mouse_relative_remainder_x=mouse_relative_remainder_y=0;
 usb_mouse_scan();
 usb_status_update();
 if(!pointer_count&&!absolute_count&&!usb_count&&!qemu_tablet){ps2_tried=1;ps2_enabled=ps2_start();}
 mouse_enabled=pointer_count||absolute_count||usb_count||qemu_tablet||ps2_enabled;
 mouse_source=absolute_count?2:pointer_count?1:usb_count?4:qemu_tablet?5:ps2_enabled?3:0;
 mouse_status_update();
 mx_present();
 if(ps2_enabled)mx_print("PS/2 mouse active");
 else if(!mouse_enabled)mx_print("Mouse unavailable: firmware has no pointer driver");
}
int mx_clicked(const char*n){if(!mouse_click)return 0;OBJ*o=find(n);if(o)return o->visible&&mouse_x>=o->x&&mouse_y>=o->y&&mouse_x<o->x+o->w&&mouse_y<o->y+o->h;for(int k=0;k<text_count;k++){TEXT_OBJ*t=&texts[k];if(eq(t->name,n))return t->visible&&mouse_x>=t->x&&mouse_y>=t->y&&mouse_x<t->x+8*length(t->value)&&mouse_y<t->y+16;}return 0;}
void mx_label(const char*n,int x,int y,const char*s){OBJ*o=find(n);if(!o)return;for(int k=0;k<label_count;k++)if(labels[k].owner==o){labels[k].dx=x-o->x;labels[k].dy=y-o->y;copy(labels[k].value,s,96);mx_present();return;}if(label_count>=48)return;LABEL*l=&labels[label_count++];l->owner=o;l->dx=x-o->x;l->dy=y-o->y;copy(l->value,s,96);mx_present();}
void mx_background(uint32_t c){bg=c;}
int mx_percent_x(int percent){return gop&&gop->mode&&gop->mode->info?(int)((int64_t)percent*gop->mode->info->horizontal_resolution/100):0;}
int mx_percent_y(int percent){return gop&&gop->mode&&gop->mode->info?(int)((int64_t)percent*gop->mode->info->vertical_resolution/100):0;}
int mx_percent_radius(int percent,int width,int height){int shorter=width<height?width:height;return (int)((int64_t)percent*shorter/100);}
static void draw_area(int x,int y,int w,int h){
 if(!gop||!gop->mode||!gop->mode->info||w<=0||h<=0)return;
 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*i=gop->mode->info;
 int right=x+w,bottom=y+h;
 if(x<0)x=0;if(y<0)y=0;
 if(right>(int)i->horizontal_resolution)right=i->horizontal_resolution;
 if(bottom>(int)i->vertical_resolution)bottom=i->vertical_resolution;
 if(right<=x||bottom<=y)return;
 clip_x=x;clip_y=y;clip_right=right;clip_bottom=bottom;clip_on=1;
 rect(x,y,right-x,bottom-y,bg,0);
 for(int j=0;j<count;j++){OBJ*o=&objs[j];if(o->visible&&o->x<right&&o->y<bottom&&o->x+o->w>x&&o->y+o->h>y)rect(o->x,o->y,o->w,o->h,o->color,o->r);}
 for(int k=0;k<text_count;k++){TEXT_OBJ*t=&texts[k];if(t->visible)text(t->x,t->y,t->value,t->color);}
 for(int k=0;k<label_count;k++){LABEL*l=&labels[k];if(l->owner->visible)text(l->owner->x+l->dx,l->owner->y+l->dy,l->value,0xffffff);}
 OBJ*shell=find("window");
 if(!shell||shell->visible){
  text(230,250,message,0xffffff);text(230,222,prompt_text,0xffffff);
  if(wifi_password_mode){char mask[96];int n=input_length;if(n>95)n=95;for(int j=0;j<n;j++)mask[j]='*';mask[n]=0;text(230+8*length(prompt_text),222,mask,0xffffff);}
  else text(230+8*length(prompt_text),222,input_text,0xffffff);
 }
 text(20,(int)i->vertical_resolution-24,mouse_status,0x91dcb4);
 text(300,(int)i->vertical_resolution-24,usb_status,0x91dcb4);
 if(browser_active){
  int sw=(int)i->horizontal_resolution,sh=(int)i->vertical_resolution;
  int view_x=26,view_y=132,view_w=sw-52,view_h=sh-158;
  rect(18,54,sw-36,sh-72,0xd9dfe8,8);
  rect(18,54,sw-36,70,0x29446c,8);
  rect(30,54,200,27,0x405d82,6);
  char tab_title[24];copy(tab_title,browser_page.title[0]?browser_page.title:"MX browser",24);
  if(y<75&&bottom>58)text(42,59,tab_title,0xffffff);
  rect(30,83,42,32,0x405d82,7);
  if(y<108&&bottom>90)text(45,91,"<",0xffffff);
  rect(84,83,sw-154,32,0xffffff,7);
  char visible_url[112];copy(visible_url,browser_url,sw>1000?100:78);
  if(y<108&&bottom>90)text(97,91,visible_url,0x25364e);
  rect(sw-62,83,34,32,0x9c4052,7);
  if(y<108&&bottom>90)text(sw-49,91,"X",0xffffff);
  rect(18,124,sw-36,sh-142,browser_page.background,0);
  int old_x=clip_x,old_y=clip_y,old_right=clip_right,old_bottom=clip_bottom;
  if(clip_x<view_x)clip_x=view_x;if(clip_y<view_y)clip_y=view_y;
  if(clip_right>view_x+view_w)clip_right=view_x+view_w;
  if(clip_bottom>view_y+view_h)clip_bottom=view_y+view_h;
  if(clip_x<clip_right&&clip_y<clip_bottom)for(int n=0;n<browser_page.line_count;n++){
   MX_WEB_LINE*line=&browser_page.lines[n];int top=view_y+line->y-browser_scroll;
   if(top+line->h<clip_y||top>=clip_bottom)continue;
   text_zoom(view_x+line->x,top,line->text,line->color,line->scale);
   if(line->link>=0)rect(view_x+line->x,top+16*line->scale+2,line->w,2,line->color,0);
  }
  clip_x=old_x;clip_y=old_y;clip_right=old_right;clip_bottom=old_bottom;
  if(browser_page.content_height>view_h){
   int thumb=view_h*view_h/browser_page.content_height;if(thumb<26)thumb=26;
   int thumb_y=view_y+browser_scroll*(view_h-thumb)/(browser_page.content_height-view_h);
   rect(sw-26,view_y,5,view_h,0xd3d8df,2);
   rect(sw-26,thumb_y,5,thumb,0x6b829d,2);
  }
 }
 if(wifi_active){
  rect(18,54,(int)i->horizontal_resolution-36,(int)i->vertical_resolution-72,0x101d32,8);
  rect(18,54,(int)i->horizontal_resolution-36,45,0x29446c,8);
  if(y<84&&bottom>68)text(34,68,"kakaOS Wi-Fi  |  Esc: close  R: scan",0xffffff);
  if(y<129&&bottom>112)text(34,112,wifi_status,0xaacfff);
  int limit=mx_wifi_count();if(limit>16)limit=16;
  for(int k=0;k<limit;k++){
   int top=158+k*25;
   if(y<top+20&&bottom>top){
    char label[96];int n=0;label[n++]=(char)('1'+k/10);if(k>=9){label[0]='1';label[n++]=(char)('0'+(k-9));}label[n++]=')';label[n++]=' ';
    const char*name=mx_wifi_ssid(k);while(*name&&n<75)label[n++]=*name++;
    label[n++]=' ';label[n++]='(';int quality=mx_wifi_quality(k);if(quality>=100)label[n++]='1';if(quality>=10)label[n++]=(char)('0'+quality/10%10);label[n++]=(char)('0'+quality%10);label[n++]='%';label[n++]=')';label[n]=0;
    text(46,top,label,0xe5edff);
   }
  }
  if(y<600&&bottom>584)text(34,584,"Click a network; enter password. Open network: press Enter.",0xaacfff);
  if(wifi_password_mode&&y<630&&bottom>614){
   text(34,614,prompt_text,0xffffff);
   char mask[96];int n=input_length;if(n>95)n=95;for(int j=0;j<n;j++)mask[j]='*';mask[n]=0;
   text(34+8*length(prompt_text),614,mask,0xffffff);
  }
 }
 if(ethernet_active){
  rect(18,54,(int)i->horizontal_resolution-36,(int)i->vertical_resolution-72,0x101d32,8);
  rect(18,54,(int)i->horizontal_resolution-36,45,0x29446c,8);
  if(y<84&&bottom>68)text(34,68,"kakaOS Ethernet  |  Esc: close  R: refresh",0xffffff);
  if(y<129&&bottom>112)text(34,112,ethernet_status,0xaacfff);
  for(int k=0;k<ethernet_display_count;k++){
   int top=158+k*28;
   if(y<top+20&&bottom>top)text(46,top,ethernet_lines[k],0xe5edff);
  }
  if(y<590&&bottom>574)text(34,574,"Raw frames: ethernet_open, ethernet_send, ethernet_receive.",0xaacfff);
  if(y<618&&bottom>602)text(34,602,"Web pages use the firmware HTTP/TCP stack.",0xaacfff);
 }
 if(input_overlay){
  int left=((int)i->horizontal_resolution-720)/2;
  if(left<24)left=24;
  rect(left,170,720,140,0x142137,12);
  rect(left,170,720,42,0x29446c,12);
  if(y<202&&bottom>182)text(left+20,182,"Open web page  |  Enter: open  Esc: cancel",0xffffff);
  rect(left+18,231,684,44,0x0b1528,6);
  if(y<259&&bottom>244){
   text(left+28,244,prompt_text,0xaacfff);
   text(left+28+8*length(prompt_text),244,input_text,0xffffff);
  }
 }
 if(mouse_enabled)draw_cursor();
 clip_on=0;
 if(backbuffer){volatile uint32_t*fb=(volatile uint32_t*)(uintptr_t)gop->mode->framebuffer_base;uint32_t stride=i->pixels_per_scan_line;for(int row=y;row<bottom;row++){uint64_t start=(uint64_t)row*stride+x;for(int col=0;col<right-x;col++)fb[start+col]=backbuffer[start+col];}}
}
void mx_animate(const char*n,int dx,int duration){
 OBJ*o=find(n);TEXT_OBJ*t=0;
 if(!o)for(int k=0;k<text_count;k++)if(eq(texts[k].name,n)){t=&texts[k];break;}
 if(!o&&!t)return;
 int x=o?o->x:t->x;
 int steps=duration/16;if(steps<1)steps=1;if(steps>120)steps=120;
 for(int i=1;i<=steps;i++){
  int old=o?o->x:t->x;
  if(o)o->x=x+dx*i/steps;else t->x=x+dx*i/steps;
  int now=o?o->x:t->x;
  int left=old<now?old:now;
  int right=(old>now?old:now)+(o?o->w:8*length(t->value));
  int top=o?o->y:t->y,bottom=top+(o?o->h:16);
  if(o)for(int k=0;k<label_count;k++){
   LABEL*l=&labels[k];if(l->owner!=o)continue;
   int start=(old<now?old:now)+l->dx,end=(old>now?old:now)+l->dx+8*length(l->value);
   if(start<left)left=start;if(end>right)right=end;
   if(o->y+l->dy<top)top=o->y+l->dy;
   if(o->y+l->dy+16>bottom)bottom=o->y+l->dy+16;
  }
  draw_area(left,top,right-left,bottom-top);
  if(stall_us&&duration>0)stall_us((UINTN)duration*1000/steps);
 }
}
void mx_present(void){if(!gop||!gop->mode||!gop->mode->info)return;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*i=gop->mode->info;draw_area(0,0,i->horizontal_resolution,i->vertical_resolution);}
void mx_print(const char*s){copy(message,s,96);mx_present();}
static void browser_layout(const char*source,size_t size){
 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*i=gop&&gop->mode?gop->mode->info:0;
 if(!i)return;
 int w=(int)i->horizontal_resolution-52,h=(int)i->vertical_resolution-158;
 browser_scroll=0;
 mx_web_layout(&browser_page,source,size,w,h);
 if(browser_js_title[0])copy(browser_page.title,browser_js_title,sizeof(browser_page.title));
 mx_present();
}
static int pointer_delta(int32_t value,uint64_t resolution,int extent,int64_t*remainder){
 if(!value)return 0;
 if(!resolution||resolution>1000000000ULL){
  int64_t scaled=(int64_t)value*mouse_dpi+*remainder;
  int64_t pixels=scaled/800;*remainder=scaled%800;
  return pixels>extent?extent:pixels< -extent?-extent:(int)pixels;
 }
 /* UEFI recommends 2% of the screen per millimeter of device movement. */
 int64_t denominator=(int64_t)resolution*50*800;
 int64_t scaled=(int64_t)value*extent*mouse_dpi+*remainder;
 int64_t pixels=scaled/denominator;
 *remainder=scaled%denominator;
 if(pixels>extent)return extent;
 if(pixels< -extent)return -extent;
 return (int)pixels;
}
static int mouse_scale_delta(int value,int64_t*remainder){
 int64_t scaled=(int64_t)value*mouse_dpi+*remainder;
 int64_t pixels=scaled/800;
 *remainder=scaled%800;
 return pixels>100000?100000:pixels< -100000?-100000:(int)pixels;
}
void mx_poll(void){
 mouse_click=0;
 if(!mouse_enabled||!gop||!gop->mode||!gop->mode->info)return;
 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*i=gop->mode->info;
 int oldx=mouse_x,oldy=mouse_y,down=mouse_down,ready=0,source=0;
 if(qemu_tablet&&++qemu_tablet_tick>=4){
  qemu_tablet_tick=0;
  MX_USB_REQUEST request={0xa1,0x01,0x0100,qemu_tablet_interface,6};
  uint8_t report[6]={0};uint32_t transfer_result=0;
  if(qemu_tablet->control(qemu_tablet,&request,0,50,report,6,&transfer_result)==0&&!transfer_result){
   int x=report[1]|((int)report[2]<<8),y=report[3]|((int)report[4]<<8),buttons=report[0];
   int changed=!qemu_tablet_seen||x!=qemu_tablet_x||y!=qemu_tablet_y||buttons!=qemu_tablet_buttons;
   if(!qemu_tablet_seen&&!x&&!y&&!buttons)changed=0;
   int previous_x=qemu_tablet_x,previous_y=qemu_tablet_y,previous_seen=qemu_tablet_seen;
   if(x>32767)x=32767;if(y>32767)y=32767;
   if(changed){
    int target_x=(int)((int64_t)x*(i->horizontal_resolution-1)/32767);
    int target_y=(int)((int64_t)y*(i->vertical_resolution-1)/32767);
    if(previous_seen&&mouse_source==5){
     int prior_x=(int)((int64_t)previous_x*(i->horizontal_resolution-1)/32767);
     int prior_y=(int)((int64_t)previous_y*(i->vertical_resolution-1)/32767);
     mouse_x+=mouse_scale_delta(target_x-prior_x,&mouse_relative_remainder_x);
     mouse_y+=mouse_scale_delta(target_y-prior_y,&mouse_relative_remainder_y);
    }else{mouse_relative_remainder_x=mouse_relative_remainder_y=0;mouse_x=target_x;mouse_y=target_y;}
    down=(buttons&1)!=0;ready=1;source=5;
   }
   if(changed){qemu_tablet_seen=1;qemu_tablet_x=x;qemu_tablet_y=y;qemu_tablet_buttons=buttons;}
  }else if(usb_report_errors<9999)usb_report_errors++;
 }
 for(int device=0;device<usb_count;device++){
  MX_USB_MOUSE*m=&usb_mice[device];
  if(!m->error_pending||m->retry_attempts>=3)continue;
  if(m->retry_countdown>0){m->retry_countdown--;continue;}
  m->io->async_interrupt(m->io,m->endpoint,0,0,0,0,0);
  m->error_pending=0;
  m->retry_attempts++;
  if(m->io->async_interrupt(m->io,m->endpoint,1,m->interval,m->packet_size,usb_mouse_report,m)!=0){
   m->error_pending=1;m->retry_countdown=5000;usb_start_fail++;usb_status_update();
  }
 }
 for(int offset=0;offset<usb_count&&!ready;offset++){
  int device=(usb_scan_start+offset)%usb_count;
  MX_USB_MOUSE*m=&usb_mice[device];
  int dx,dy,buttons,reported;
  EFI_BOOT_SERVICES*bs=(EFI_BOOT_SERVICES*)st->boot_services;
  typedef UINTN (ABI *EFI_RAISE_TPL)(UINTN);
  typedef void (ABI *EFI_RESTORE_TPL)(UINTN);
  EFI_RAISE_TPL raise=(EFI_RAISE_TPL)bs->fn[0];
  EFI_RESTORE_TPL restore=(EFI_RESTORE_TPL)bs->fn[1];
  UINTN old_tpl=raise(16);
  dx=m->dx;dy=m->dy;buttons=m->buttons;reported=m->has_report;
  m->dx=0;m->dy=0;m->has_report=0;
  restore(old_tpl);
  if(!reported||(!dx&&!dy&&(buttons&1)==mouse_down))continue;
  if(mouse_source!=4)mouse_relative_remainder_x=mouse_relative_remainder_y=0;
  mouse_x+=mouse_scale_delta(dx,&mouse_relative_remainder_x);
  mouse_y+=mouse_scale_delta(dy,&mouse_relative_remainder_y);
  down=(buttons&1)!=0;ready=1;source=4;
  usb_scan_start=(device+1)%usb_count;
 }
 for(int class_pass=0;class_pass<2&&!ready;class_pass++){
 if(((pointer_class_turn+(unsigned)class_pass)&1)==0){
 for(int offset=0;offset<pointer_count&&!ready;offset++){
  int device=(pointer_scan_start+offset)%pointer_count;
  MX_POINTER*pointer=pointers[device];
  if(!pointer||!pointer->get_state)continue;
  MX_POINTER_STATE state;
  if(pointer->get_state(pointer,&state)==0){
   if(!state.x&&!state.y&&(state.left_button!=0)==mouse_down)continue;
   if(mouse_source!=1||mouse_pointer_device!=device){mouse_remainder_x=0;mouse_remainder_y=0;}
   mouse_pointer_device=device;
   mouse_x+=pointer_delta(state.x,pointer->mode?pointer->mode->resolution_x:0,(int)i->horizontal_resolution,&mouse_remainder_x);
   mouse_y+=pointer_delta(state.y,pointer->mode?pointer->mode->resolution_y:0,(int)i->vertical_resolution,&mouse_remainder_y);
   down=state.left_button!=0;ready=1;source=1;
   pointer_scan_start=(device+1)%pointer_count;
  }
 }
 }else{
 for(int offset=0;offset<absolute_count&&!ready;offset++){
  int device=(absolute_scan_start+offset)%absolute_count;
  MX_ABSOLUTE_POINTER*absolute_pointer=absolute_pointers[device];
  if(!absolute_pointer||!absolute_pointer->get_state||!absolute_pointer->mode)continue;
  MX_ABSOLUTE_STATE state;MX_ABSOLUTE_MODE*mode=absolute_pointer->mode;
  if(absolute_pointer->get_state(absolute_pointer,&state)==0){
   MX_ABSOLUTE_STATE*last=&absolute_last[device];
   if(!absolute_seen[device]&&!state.x&&!state.y&&!state.buttons)continue;
   if(absolute_seen[device]&&state.x==last->x&&state.y==last->y&&state.buttons==last->buttons)continue;
   MX_ABSOLUTE_STATE previous=*last;
   int previously_seen=absolute_seen[device];
   *last=state;absolute_seen[device]=1;
   if(mode->max_x>mode->min_x){
    uint64_t x=state.x<mode->min_x?mode->min_x:state.x>mode->max_x?mode->max_x:state.x;
    int target=(int)((x-mode->min_x)*(i->horizontal_resolution-1)/(mode->max_x-mode->min_x));
    if(previously_seen&&mouse_source==2){
     uint64_t old=previous.x<mode->min_x?mode->min_x:previous.x>mode->max_x?mode->max_x:previous.x;
     int prior=(int)((old-mode->min_x)*(i->horizontal_resolution-1)/(mode->max_x-mode->min_x));
     mouse_x+=mouse_scale_delta(target-prior,&mouse_relative_remainder_x);
    }else{mouse_relative_remainder_x=0;mouse_x=target;}
   }
   if(mode->max_y>mode->min_y){
    uint64_t y=state.y<mode->min_y?mode->min_y:state.y>mode->max_y?mode->max_y:state.y;
    int target=(int)((y-mode->min_y)*(i->vertical_resolution-1)/(mode->max_y-mode->min_y));
    if(previously_seen&&mouse_source==2){
     uint64_t old=previous.y<mode->min_y?mode->min_y:previous.y>mode->max_y?mode->max_y:previous.y;
     int prior=(int)((old-mode->min_y)*(i->vertical_resolution-1)/(mode->max_y-mode->min_y));
     mouse_y+=mouse_scale_delta(target-prior,&mouse_relative_remainder_y);
    }else{mouse_relative_remainder_y=0;mouse_y=target;}
   }
   down=(state.buttons&1)!=0;ready=1;source=2;
   absolute_scan_start=(device+1)%absolute_count;
  }
 }
 }
 }
 pointer_class_turn++;
 if(!ready&&ps2_enabled){
  for(int k=0;k<16;k++){
   uint8_t status=port_read(0x64);
   if((status&0x21)!=0x21)break;
   uint8_t byte=port_read(0x60);
   if(ps2_packet_pos==0&&!(byte&8))continue;
   ps2_packet[ps2_packet_pos++]=byte;
   if(ps2_packet_pos==3){
    ps2_packet_pos=0;
    if(!(ps2_packet[0]&0xc0)){
     if(mouse_source!=3)mouse_relative_remainder_x=mouse_relative_remainder_y=0;
     mouse_x+=mouse_scale_delta((int8_t)ps2_packet[1],&mouse_relative_remainder_x);
     mouse_y-=mouse_scale_delta((int8_t)ps2_packet[2],&mouse_relative_remainder_y);
    }
    down=(ps2_packet[0]&1)!=0;ready=1;source=3;
   }
  }
 }
 if(ready&&mouse_x==oldx&&mouse_y==oldy&&down==mouse_down)ready=0;
 if(!ready){
  if(!ps2_tried&&++mouse_idle>5000){ps2_tried=1;ps2_enabled=ps2_start();if(ps2_enabled){mouse_source=3;mouse_status_update();draw_area(18,(int)i->vertical_resolution-26,260,24);}}
  if(usb_report_errors!=usb_displayed_errors){usb_displayed_errors=usb_report_errors;usb_status_update();draw_area(298,(int)i->vertical_resolution-26,500,24);}
  return;
 }
 mouse_idle=0;mouse_source=source;mouse_events++;mouse_status_update();
 if(usb_report_errors!=usb_displayed_errors){usb_displayed_errors=usb_report_errors;usb_status_update();}
 if(mouse_x<0)mouse_x=0;if(mouse_y<0)mouse_y=0;
 if(mouse_x>=(int)i->horizontal_resolution)mouse_x=i->horizontal_resolution-1;
 if(mouse_y>=(int)i->vertical_resolution)mouse_y=i->vertical_resolution-1;
 mouse_click=down&&!mouse_down;mouse_down=down;
 if(mouse_x!=oldx||mouse_y!=oldy){draw_area(oldx,oldy,CURSOR_W,CURSOR_H);draw_area(mouse_x,mouse_y,CURSOR_W,CURSOR_H);}
 draw_area(18,(int)i->vertical_resolution-26,780,24);
}
typedef struct {uint16_t scan,unicode;} MX_KEY;
typedef struct MX_IN MX_IN;
struct MX_IN {void*reset;EFI_STATUS (ABI *read)(MX_IN*,MX_KEY*);void*wait;};
static void browser_scroll_by(int delta){
 int h=gop&&gop->mode&&gop->mode->info?(int)gop->mode->info->vertical_resolution-158:0;
 int max=browser_page.content_height-h;if(max<0)max=0;
 browser_scroll+=delta;if(browser_scroll<0)browser_scroll=0;if(browser_scroll>max)browser_scroll=max;
 mx_present();
}
static int browser_resolve(const char*base,const char*href,char*out){
 if(!href||!href[0]||href[0]=='#'||href[0]=='?'||href[0]=='m'&&href[1]=='a'&&href[2]=='i'&&href[3]=='l'&&href[4]=='t'&&href[5]=='o'&&href[6]==':')return 0;
 if((href[0]=='h'&&href[1]=='t'&&href[2]=='t'&&href[3]=='p'&&href[4]==':' )||(href[0]=='h'&&href[1]=='t'&&href[2]=='t'&&href[3]=='p'&&href[4]=='s'&&href[5]==':')){copy(out,href,256);return 1;}
 int scheme=0;while(base[scheme]&&base[scheme]!=':'&&scheme<16)scheme++;
 if(base[scheme]!=':'||base[scheme+1]!='/'||base[scheme+2]!='/')return 0;
 int origin=scheme+3;while(base[origin]&&base[origin]!='/')origin++;
 int prefix=origin;
 if(href[0]!='/'){
  prefix=length(base);while(prefix>origin&&base[prefix-1]!='/')prefix--;
  if(prefix==origin){prefix=origin;}
 }
 int n=0;while(n<prefix&&n<255){out[n]=base[n];n++;}
 if(href[0]!='/'&&n==origin&&n<255)out[n++]='/';
 for(int j=0;href[j]&&n<255;j++)out[n++]=href[j];out[n]=0;
 return n>0;
}
void mx_browser(const char*url){
 copy(browser_url,url?url:"",256);browser_previous[0]=0;browser_active=1;
 MX_IN*in=st?(MX_IN*)st->con_in:0;int running=1;
 while(running){
  int navigate=0;
  browser_js_title[0]=0;browser_layout("Loading page...",15);
  UINTN used=0;int rc=mx_http_get(st,browser_url,browser_raw,sizeof(browser_raw),&used);
  if(rc==-17)break;
  if(rc==0){size_t rendered=mx_js_render(browser_raw,(size_t)used,browser_plain,sizeof(browser_plain),browser_js_title,sizeof(browser_js_title));browser_layout(browser_plain,rendered);}
  else {
   if(rc==-3)copy(browser_plain,"No UEFI HTTP driver. Enable network in firmware/OVMF.",32768);
   else if(rc==-18)copy(browser_plain,"IPv4/DHCP unavailable. Check QEMU NIC and OVMF network drivers.",32768);
   else if(rc==-9&&mx_http_last_efi_status()==0x8000000000000011ULL)copy(browser_plain,"HTTP needs an IPv4 address. DHCP is not ready on this adapter.",32768);
   else if(rc==-2)copy(browser_plain,"URL must start with http:// or https://",32768);
   else if(rc==-16)copy(browser_plain,"Too many HTTP redirects.",32768);
   else {
    const char*phase=rc==-6?"Configure":rc==-9?"Request":rc==-11?"Response":rc==-12?"HTTP status":rc==-4?"CreateChild":"HTTP";
    copy(browser_plain,phase,32768);int n=length(browser_plain);
    const char*s=" failed. UEFI status 0x";while(*s&&n<32700)browser_plain[n++]=*s++;
    uint64_t value=mx_http_last_efi_status();const char*hex="0123456789ABCDEF";
    for(int shift=60;shift>=0;shift-=4)browser_plain[n++]=hex[(value>>shift)&15];
    browser_plain[n++]='\n';
    if(browser_url[4]=='s'){
     int caps=mx_https_capabilities(st);
     s=!(caps&1)?"HTTPS: firmware has no UEFI TLS driver. Use a TLS-enabled OVMF build.":
       !(caps&2)?"HTTPS: no CA database in firmware. Run bash run-qemu.sh main --https.":
       value==0x8000000000000015ULL?"HTTPS: TLS session aborted by firmware. Check CA trust, guest clock and TLS compatibility.":
       "HTTPS: firmware request failed.";
    }else s="QEMU: add -nic user,model=e1000; OVMF needs HTTP drivers.";
    while(*s&&n<32700)browser_plain[n++]=*s++;
    if(browser_url[4]=='s'){
     char diagnostics[80];int d=0;
     status_word(diagnostics,&d,"\nCA bytes: ");
     UINTN ca_bytes=mx_https_ca_bytes(st);
     status_number(diagnostics,&d,ca_bytes>2147483647ULL?2147483647:(int)ca_bytes);
     status_word(diagnostics,&d,"  UEFI clock year: ");
     status_number(diagnostics,&d,mx_https_clock_year(st));
     diagnostics[d]=0;
     s=diagnostics;while(*s&&n<32700)browser_plain[n++]=*s++;
    }
    browser_plain[n]=0;
   }
   browser_layout(browser_plain,(size_t)length(browser_plain));
  }
  if(!in||!in->read)break;
  while(running&&!navigate){
   mx_poll();
   int sw=gop&&gop->mode&&gop->mode->info?(int)gop->mode->info->horizontal_resolution:1024;
   int sh=gop&&gop->mode&&gop->mode->info?(int)gop->mode->info->vertical_resolution:768;
   if(mouse_click){
    if(mouse_y>=83&&mouse_y<115&&mouse_x>=sw-62&&mouse_x<sw-28){running=0;break;}
    if(mouse_y>=83&&mouse_y<115&&mouse_x>=30&&mouse_x<72&&browser_previous[0]){
     char target[256];copy(target,browser_previous,256);copy(browser_previous,browser_url,256);copy(browser_url,target,256);navigate=1;break;
    }
    if(mouse_y>=83&&mouse_y<115&&mouse_x>=84&&mouse_x<sw-70){
     const char*typed=mx_input("URL> ");
     if(typed[0]){copy(browser_previous,browser_url,256);copy(browser_url,typed,256);navigate=1;break;}
    }
    if(mouse_x>=26&&mouse_x<sw-26&&mouse_y>=132&&mouse_y<sh-26){
     int link=mx_web_link_at(&browser_page,mouse_x-26,mouse_y-132+browser_scroll);
     if(link>=0&&link<browser_page.link_count){
      char target[256];if(browser_resolve(browser_url,browser_page.links[link],target)){
       copy(browser_previous,browser_url,256);copy(browser_url,target,256);navigate=1;break;
      }
     }
    }
   }
   MX_KEY key={0,0};
   if(in->read(in,&key)==0){
    if(key.scan==0x17||key.unicode==27){running=0;break;}
    if(key.scan==1||key.scan==9)browser_scroll_by(-64);
    if(key.scan==2||key.scan==10)browser_scroll_by(64);
    if(key.unicode=='r'||key.unicode=='R'){navigate=1;break;}
   }
   mx_step();
  }
 }
 browser_active=0;mx_present();
}
int mx_key(const char*n){if(eq(n,"Escape"))return last_key==27;if(eq(n,"Enter"))return last_key==13;return 0;}
const char*mx_input(const char*prompt){
 copy(prompt_text,prompt,96);input_length=0;input_text[0]=0;last_key=0;
 OBJ*shell=find("window");input_overlay=(browser_active||!shell||!shell->visible)&&!wifi_password_mode;
 mx_present();
 MX_IN*in=st?(MX_IN*)st->con_in:0;
 if(!in||!in->read){input_overlay=0;prompt_text[0]=0;mx_present();return input_text;}
 for(;;){
  mx_poll();MX_KEY key;
  if(in->read(in,&key)!=0){mx_step();continue;}
  int c=key.unicode;
  if(key.scan==0x17||c==27){last_key=27;input_text[0]=0;input_overlay=0;prompt_text[0]=0;mx_present();return input_text;}
  if(c==13){last_key=13;input_overlay=0;prompt_text[0]=0;mx_present();return input_text;}
  if(c==8){if(input_length)input_text[--input_length]=0;}
  else if(c>=32&&c<127&&input_length<94){input_text[input_length++]=(char)c;input_text[input_length]=0;}
  else continue;
  mx_present();
 }
}
const char*mx_password_input(const char*prompt){
 wifi_password_mode=1;const char*value=mx_input(prompt);copy(secret_text,value,96);
 for(int i=0;i<96;i++)input_text[i]=0;input_length=0;wifi_password_mode=0;mx_present();return secret_text;
}
void mx_forget_password(void){for(int i=0;i<96;i++)secret_text[i]=0;}
static void wifi_scan_message(int count){
 if(count>=0)copy(wifi_status,count?"Networks found. Click one to connect.":"No Wi-Fi networks found.",128);
 else if(count==-1)copy(wifi_status,"No UEFI Wi-Fi driver. QEMU virtual NIC is Ethernet.",128);
 else if(count==-4)copy(wifi_status,"Wi-Fi scan timed out.",128);
 else if(count==-8)copy(wifi_status,"Wi-Fi operation is still pending.",128);
 else copy(wifi_status,"Wi-Fi scan failed in firmware.",128);
}
void mx_wifi_manager(void){
 wifi_active=1;copy(wifi_status,"Scanning Wi-Fi networks...",128);mx_present();
 wifi_scan_message(mx_wifi_scan());mx_present();
 MX_IN*in=st?(MX_IN*)st->con_in:0;
 for(;;){
  mx_poll();int selected=-1;
  if(mouse_click&&mouse_x>=35&&mouse_x<900&&mouse_y>=158){selected=(mouse_y-158)/25;if(selected>=mx_wifi_count()||selected>=16)selected=-1;}
  MX_KEY key={0,0};
  if(in&&in->read&&in->read(in,&key)==0){
   if(key.scan==0x17||key.unicode==27)break;
   if(key.unicode=='r'||key.unicode=='R'){copy(wifi_status,"Scanning Wi-Fi networks...",128);mx_present();wifi_scan_message(mx_wifi_scan());mx_present();}
   if(key.unicode>='1'&&key.unicode<='9')selected=key.unicode-'1';
  }
  if(selected>=0&&selected<mx_wifi_count()){
   const char*name=mx_wifi_ssid(selected);
   const char*password=mx_password_input("Wi-Fi password> ");
   if(last_key==27){mx_forget_password();continue;}
   copy(wifi_status,"Connecting...",128);mx_present();
   int result=mx_wifi_connect(name,password);mx_forget_password();
   if(result==0)copy(wifi_status,"Wi-Fi connected. Open the browser with web.",128);
   else if(result==-5||result==-6)copy(wifi_status,"Firmware has no usable WPA supplicant.",128);
   else if(result==-3)copy(wifi_status,"WPA password must be 8-63 characters.",128);
   else if(result==-8)copy(wifi_status,"Wi-Fi connection timed out; operation pending.",128);
   else copy(wifi_status,"Wi-Fi connection failed. Check password/network.",128);
   mx_present();
  }
  mx_step();
 }
 wifi_active=0;mx_forget_password();mx_present();
}
static void ethernet_refresh(void){
 int count=mx_ethernet_scan();
 if(count<0){ethernet_display_count=0;copy(ethernet_status,"Ethernet scan failed in UEFI.",128);return;}
 if(count==0){ethernet_display_count=0;copy(ethernet_status,"No UEFI Ethernet driver (Simple Network Protocol).",128);return;}
 copy(ethernet_status,"Detected Ethernet adapters:",128);
 ethernet_display_count=count>8?8:count;
 for(int k=0;k<ethernet_display_count;k++){
  char*line=ethernet_lines[k];int n=0;line[n++]='#';line[n++]=(char)('1'+k);line[n++]=' ';
  const char*mac=mx_ethernet_mac(k);while(*mac&&n<60)line[n++]=*mac++;
  int link=mx_ethernet_link(k);
  const char*state=link==1?"  link up":link==0?"  link down":"  link unknown";
  while(*state&&n<94)line[n++]=*state++;
  line[n]=0;
 }
}
void mx_ethernet_manager(void){
 ethernet_active=1;ethernet_refresh();mx_present();
 MX_IN*in=st?(MX_IN*)st->con_in:0;
 for(;;){
  mx_poll();MX_KEY key={0,0};
  if(in&&in->read&&in->read(in,&key)==0){
   if(key.scan==0x17||key.unicode==27)break;
   if(key.unicode=='r'||key.unicode=='R'){ethernet_refresh();mx_present();}
  }
  mx_step();
 }
 ethernet_active=0;mx_present();
}
const char*mx_poll_input(const char*prompt){static char completed[96];last_key=0;if(!eq(prompt_text,prompt)){copy(prompt_text,prompt,96);mx_present();}MX_IN*in=(MX_IN*)st->con_in;if(!in||!in->read)return "";MX_KEY key;if(in->read(in,&key)!=0)return "";int c=key.unicode;if(key.scan==0x17||c==27){last_key=27;input_length=0;input_text[0]=0;mx_present();return "";}if(c==13){last_key=13;copy(completed,input_text,96);input_length=0;input_text[0]=0;mx_present();return completed;}if(c==8){if(input_length)input_text[--input_length]=0;}else if(c>=32&&c<127&&input_length<94){input_text[input_length++]=(char)c;input_text[input_length]=0;}else return "";mx_present();return "";}
void mx_step(void){if(stall_us)stall_us(1000);else __asm__ __volatile__("pause");}
typedef void (ABI *EFI_RESET_SYSTEM)(uint32_t,EFI_STATUS,UINTN,void*);
void mx_shutdown(void){if(!st||!st->runtime_services)return;uint8_t*rt=(uint8_t*)st->runtime_services;EFI_RESET_SYSTEM reset=*(EFI_RESET_SYSTEM*)(rt+24+10*8);if(reset)reset(2,0,0,0);}
typedef EFI_STATUS (ABI *EFI_ALLOCATE_POOL)(uint32_t,UINTN,void**);
typedef EFI_STATUS (ABI *EFI_LOCATE_HANDLE_BUFFER)(uint32_t,EFI_GUID*,void*,UINTN*,EFI_HANDLE**);
typedef EFI_STATUS (ABI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE,EFI_GUID*,void**);
typedef EFI_STATUS (ABI *EFI_FREE_POOL)(void*);
static int collect_pointer_devices(EFI_BOOT_SERVICES*bs,EFI_GUID*guid,void**devices){
 EFI_LOCATE_HANDLE_BUFFER locate=(EFI_LOCATE_HANDLE_BUFFER)bs->fn[36];
 EFI_HANDLE_PROTOCOL handle_protocol=(EFI_HANDLE_PROTOCOL)bs->fn[16];
 EFI_FREE_POOL free_pool=(EFI_FREE_POOL)bs->fn[6];
 EFI_HANDLE*handles=0;UINTN count=0;int found=0;
 if(!locate||!handle_protocol||locate(2,guid,0,&count,&handles)!=0||!handles)return 0;
 for(UINTN j=0;j<count&&found<MAX_POINTER_DEVICES;j++){
  void*device=0;
  if(handle_protocol(handles[j],guid,&device)==0&&device)devices[found++]=device;
 }
 if(free_pool)free_pool(handles);
 return found;
}
static EFI_STATUS ABI usb_mouse_report(void*data,UINTN size,void*context,uint32_t result){
 MX_USB_MOUSE*m=(MX_USB_MOUSE*)context;
 if(result){usb_report_errors++;if(!m->error_pending){m->error_pending=1;m->retry_countdown=5000;}return 0;}
 if(!data||size<3)return 0;
 const uint8_t*report=(const uint8_t*)data;
 int x=m->dx+(int8_t)report[1],y=m->dy+(int8_t)report[2];
 m->dx=x>4096?4096:x< -4096?-4096:x;
 m->dy=y>4096?4096:y< -4096?-4096:y;
 m->buttons=report[0];m->has_report=1;
 return 0;
}
static void usb_mouse_scan(void){
 EFI_BOOT_SERVICES*bs=(EFI_BOOT_SERVICES*)st->boot_services;
 EFI_LOCATE_HANDLE_BUFFER locate=bs?(EFI_LOCATE_HANDLE_BUFFER)bs->fn[36]:0;
 EFI_HANDLE_PROTOCOL handle_protocol=bs?(EFI_HANDLE_PROTOCOL)bs->fn[16]:0;
 EFI_FREE_POOL free_pool=bs?(EFI_FREE_POOL)bs->fn[6]:0;
 if(!locate||!handle_protocol||!free_pool)return;
 EFI_GUID ug={0x2b2f68d6,0x0cd2,0x44cf,{0x8e,0x8b,0xbb,0xa2,0x0b,0x1b,0x5b,0x75}};
 EFI_GUID pg={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
 EFI_GUID ag={0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,0x04,0x99,0x2a,0x43}};
 EFI_HANDLE*handles=0;UINTN count=0;
 if(locate(2,&ug,0,&count,&handles)!=0||!handles)return;
 usb_io_count=count>9999?9999:(int)count;
 for(UINTN h=0;h<count&&usb_count<MAX_USB_MICE;h++){
  MX_USB_IO*io=0;
  if(handle_protocol(handles[h],&ug,(void**)&io)!=0||!io||!io->get_interface)continue;
  int duplicate=0;for(int j=0;j<usb_count;j++)if(usb_mice[j].io==io)duplicate=1;
  if(duplicate)continue;
  MX_USB_INTERFACE desc;
  if(io->get_interface(io,&desc)!=0||desc.device_class!=3)continue;
  if(desc.protocol==0&&!qemu_tablet&&io->get_device&&io->control){
   MX_USB_DEVICE dev;
   if(io->get_device(io,&dev)==0&&dev.vendor==0x0627&&dev.product==0x0001){
    qemu_tablet=io;qemu_tablet_interface=desc.number;continue;
   }
  }
  if(desc.subclass!=1||desc.protocol!=2||!io->get_endpoint||!io->async_interrupt)continue;
  usb_boot_count++;
  void*interface=0;
  if(handle_protocol(handles[h],&pg,&interface)==0||handle_protocol(handles[h],&ag,&interface)==0)continue;
  MX_USB_ENDPOINT endpoint;uint8_t address=0,interval=1;uint16_t packet_size=0;
  for(uint8_t e=0;e<desc.endpoints&&e<16;e++){
   if(io->get_endpoint(io,e,&endpoint)==0&&(endpoint.address&0x80)&&(endpoint.attributes&3)==3&&endpoint.max_packet>=3){address=endpoint.address;interval=endpoint.interval?endpoint.interval:1;packet_size=endpoint.max_packet;break;}
  }
  if(!address){usb_start_fail++;continue;}
  if(io->control){
   MX_USB_REQUEST request={0x21,0x0b,0,desc.number,0};uint32_t result=0;
   if(io->control(io,&request,2,1000,0,0,&result)!=0||result){usb_start_fail++;continue;}
  }
  MX_USB_MOUSE*m=&usb_mice[usb_count];m->io=io;m->endpoint=address;m->interval=interval;m->packet_size=packet_size;
  m->dx=0;m->dy=0;m->buttons=0;m->has_report=0;m->error_pending=0;m->retry_countdown=0;m->retry_attempts=0;
  if(io->async_interrupt(io,address,1,interval,packet_size,usb_mouse_report,m)==0)usb_count++;
  else{m->io=0;usb_start_fail++;}
 }
 free_pool(handles);
}
EFI_STATUS ABI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE*table){
 (void)image;st=table;
 mx_lowlevel_init(table);
 mx_wifi_init(table);
 mx_ethernet_init(table);
 EFI_GUID gg={0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
 EFI_GUID pg={0x31878c87,0x0b75,0x11d5,{0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
 EFI_GUID ag={0x8d59d32b,0xc655,0x4ae9,{0x9b,0x15,0xf2,0x59,0x04,0x99,0x2a,0x43}};
 EFI_BOOT_SERVICES*bs=(EFI_BOOT_SERVICES*)st->boot_services;
 if(bs){
  stall_us=(EFI_STALL)bs->fn[28];
  if(bs->locate_protocol)bs->locate_protocol(&gg,0,(void**)&gop);
  pointer_count=collect_pointer_devices(bs,&pg,(void**)pointers);
  absolute_count=collect_pointer_devices(bs,&ag,(void**)absolute_pointers);
  if(bs->locate_protocol){
   if(!pointer_count&&bs->locate_protocol(&pg,0,(void**)&pointers[0])==0&&pointers[0])pointer_count=1;
   if(!absolute_count&&bs->locate_protocol(&ag,0,(void**)&absolute_pointers[0])==0&&absolute_pointers[0])absolute_count=1;
  }
 }
 if(bs&&gop&&gop->mode&&gop->mode->info&&gop->mode->info->pixel_format<=1){EFI_ALLOCATE_POOL alloc=(EFI_ALLOCATE_POOL)bs->fn[5];uint64_t pixels=(uint64_t)gop->mode->info->pixels_per_scan_line*gop->mode->info->vertical_resolution;uint64_t bytes=pixels*4;if(alloc&&pixels&&bytes<=gop->mode->framebuffer_size&&alloc(2,(UINTN)bytes,(void**)&backbuffer)==0)backbuffer_pixels=pixels;else backbuffer=0;}
 mx_main();return 0;
}
