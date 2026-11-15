#include "mx_browser_text.h"

static char lower(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static int name_is(const char*name,size_t length,const char*word){
 size_t i=0;while(i<length&&word[i]&&lower(name[i])==word[i])i++;
 return i==length&&word[i]==0;
}
static int hex_digit(char c){
 if(c>='0'&&c<='9')return c-'0';
 c=lower(c);return c>='a'&&c<='f'?c-'a'+10:-1;
}
static int chunk_pass(char*body,size_t length,int copy,size_t*written){
 size_t pos=0,out=0;
 for(;;){
  size_t chunk=0;int digits=0;
  while(pos<length){
   int digit=hex_digit(body[pos]);if(digit<0)break;
   if(chunk>(length-(size_t)digit)/16)return 0;
   chunk=chunk*16+(size_t)digit;pos++;digits++;
  }
  if(!digits)return 0;
  if(pos<length&&body[pos]==';')while(pos<length&&body[pos]!='\r'&&body[pos]!='\n')pos++;
  if(pos+1>=length||body[pos]!='\r'||body[pos+1]!='\n')return 0;
  pos+=2;
  if(chunk==0){
   while(pos+1<length){
    if(body[pos]=='\r'&&body[pos+1]=='\n'){
     pos+=2;if(pos==length){*written=out;return 1;}
     return 0;
    }
    while(pos<length&&body[pos]!='\r'&&body[pos]!='\n')pos++;
    if(pos+1>=length||body[pos]!='\r'||body[pos+1]!='\n')return 0;
    pos+=2;
   }
   return 0;
  }
  if(chunk>length-pos)return 0;
  if(copy)for(size_t i=0;i<chunk;i++)body[out+i]=body[pos+i];
  out+=chunk;pos+=chunk;
  if(pos+1>=length||body[pos]!='\r'||body[pos+1]!='\n')return 0;
  pos+=2;
 }
}
int mx_http_dechunk(char*body,size_t*length){
 if(!body||!length)return 0;
 size_t out=0;if(!chunk_pass(body,*length,0,&out))return 0;
 if(!chunk_pass(body,*length,1,&out))return 0;
 *length=out;body[out]=0;return 1;
}
static void put_char(char*out,size_t cap,size_t*used,char c){if(*used+1<cap)out[(*used)++]=c;}
static void break_line(char*out,size_t cap,size_t*used){
 while(*used&&out[*used-1]==' ')(*used)--;
 if(*used&&out[*used-1]!='\n')put_char(out,cap,used,'\n');
}
size_t mx_html_to_text(const char*html,size_t length,char*out,size_t cap){
 if(!out||!cap)return 0;
 size_t used=0;int skip=0;
 for(size_t pos=0;html&&pos<length;){
  char c=html[pos];
  if(c=='<'){
   size_t end=pos+1;while(end<length&&html[end]!='>')end++;
   if(end==length)break;
   size_t start=pos+1;int closing=0;
   if(start<end&&html[start]=='/'){closing=1;start++;}
   while(start<end&&(html[start]==' '||html[start]=='\t'))start++;
   size_t name_end=start;while(name_end<end&&((html[name_end]>='a'&&html[name_end]<='z')||(html[name_end]>='A'&&html[name_end]<='Z')||(html[name_end]>='0'&&html[name_end]<='9')))name_end++;
   size_t n=name_end-start;
   int style=name_is(html+start,n,"style"),script=name_is(html+start,n,"script");
   if(skip){if(closing&&((skip==1&&style)||(skip==2&&script)))skip=0;}
   else if(!closing&&(style||script))skip=style?1:2;
   else if(name_is(html+start,n,"p")||name_is(html+start,n,"div")||name_is(html+start,n,"h1")||name_is(html+start,n,"h2")||name_is(html+start,n,"br")||name_is(html+start,n,"li")||name_is(html+start,n,"title"))break_line(out,cap,&used);
   pos=end+1;continue;
  }
  if(skip){pos++;continue;}
  if(c=='&'){
   size_t start=pos+1,end=start;while(end<length&&end-start<8&&html[end]!=';')end++;
   if(end<length&&html[end]==';'){
    size_t n=end-start;
    if(name_is(html+start,n,"amp"))c='&';
    else if(name_is(html+start,n,"lt"))c='<';
    else if(name_is(html+start,n,"gt"))c='>';
    else if(name_is(html+start,n,"quot"))c='"';
    else if(name_is(html+start,n,"nbsp"))c=' ';
    else if(name_is(html+start,n,"apos"))c='\'';
    else {pos++;put_char(out,cap,&used,'&');continue;}
    pos=end+1;
   }else pos++;
  }else pos++;
  if(c=='\r'||c=='\n'||c=='\t'||c==' ')c=' ';
  if((unsigned char)c<32||(unsigned char)c>126)c='?';
  if(c==' '){if(!used||out[used-1]==' '||out[used-1]=='\n')continue;}
  put_char(out,cap,&used,c);
 }
 while(used&&(out[used-1]==' '||out[used-1]=='\n'))used--;
 out[used]=0;return used;
}
