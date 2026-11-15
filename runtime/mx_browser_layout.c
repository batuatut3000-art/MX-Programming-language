#include "mx_browser_layout.h"

static char low(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static int space(char c){return c==' '||c=='\n'||c=='\r'||c=='\t';}
static int same(const char*a,size_t n,const char*b){size_t k=0;while(k<n&&b[k]&&low(a[k])==b[k])k++;return k==n&&b[k]==0;}
static const char*find_ci(const char*data,size_t n,const char*needle){
 size_t m=0;while(needle[m])m++;
 for(size_t i=0;i+m<=n;i++){size_t j=0;while(j<m&&low(data[i+j])==low(needle[j]))j++;if(j==m)return data+i;}
 return 0;
}
static int decimal(const char*s,const char*end){int value=0;while(s<end&&*s>='0'&&*s<='9'){value=value*10+*s++-'0';if(value>10000)return 10000;}return value;}
static uint32_t hex_color(const char*s,const char*end,uint32_t fallback){
 if(s>=end||*s!='#')return fallback;
 s++;int digits=0;uint32_t v=0;
 while(s<end&&digits<6){char c=low(*s);int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;if(d<0)break;v=(v<<4)|(uint32_t)d;s++;digits++;}
 if(digits==3)return ((v&0xf00)<<12)|((v&0xf00)<<8)|((v&0x0f0)<<8)|((v&0x0f0)<<4)|((v&0x00f)<<4)|(v&0x00f);
 return digits==6?v:fallback;
}
static const char*property(const char*rule,const char*end,const char*name){
 size_t n=0;while(name[n])n++;
 for(const char*p=rule;p+n<end;p++){
  if(p>rule&&!space(p[-1])&&p[-1]!=';')continue;
  size_t j=0;while(j<n&&low(p[j])==name[j])j++;
  if(j!=n)continue;
  const char*q=p+n;while(q<end&&space(*q))q++;
  if(q<end&&*q==':'){q++;while(q<end&&space(*q))q++;return q;}
 }
 return 0;
}
static uint32_t css_color(const char*rule,const char*end,const char*name,uint32_t fallback){
 const char*p=property(rule,end,name);if(!p)return fallback;
 const char*limit=p;while(limit<end&&*limit!=';'&&*limit!='}')limit++;
 const char*hash=p;while(hash<limit&&*hash!='#')hash++;
 if(hash<limit)return hex_color(hash,limit,fallback);
 if(limit-p>=5&&same(p,5,"white"))return 0xffffff;
 if(limit-p>=5&&same(p,5,"black"))return 0x000000;
 return fallback;
}
static const char*css_rule(const char*start,const char*end,const char*name,const char**rule_end){
 size_t n=0;while(name[n])n++;
 for(const char*p=start;p+n<end;p++){
  if(p>start&&((low(p[-1])>='a'&&low(p[-1])<='z')||p[-1]=='-'||p[-1]=='_'))continue;
  size_t j=0;while(j<n&&low(p[j])==name[j])j++;if(j!=n)continue;
  const char*q=p+n;while(q<end&&space(*q))q++;
  if(q>=end||*q!='{')continue;
  const char*stop=++q;while(stop<end&&*stop!='}')stop++;
  *rule_end=stop;return q;
 }
 return 0;
}
static void parse_css(MX_WEB_PAGE*page,const char*html,size_t length,int view_h,int*width,int*top_pad,int*side_pad){
 const char*style=find_ci(html,length,"<style");if(!style)return;
 const char*end=html+length,*open=style;
 while(open<end&&*open!='>')open++;if(open==end)return;open++;
 const char*close=find_ci(open,(size_t)(end-open),"</style");if(!close)close=end;
 const char*rule_end=0;
 const char*root=css_rule(open,close,"html",&rule_end);
 if(root){page->background=css_color(root,rule_end,"background",page->background);page->foreground=css_color(root,rule_end,"color",page->foreground);}
 const char*body=css_rule(open,close,"body",&rule_end);
 if(body){
  page->background=css_color(body,rule_end,"background",page->background);
  page->foreground=css_color(body,rule_end,"color",page->foreground);
  const char*max=property(body,rule_end,"max-width");
  if(max){int value=decimal(max,rule_end);if(value>0){if(find_ci(max,(size_t)(rule_end-max),"em"))value*=16;if(value<*width)*width=value;}}
  const char*align=property(body,rule_end,"text-align");
  if(align&&find_ci(align,(size_t)(rule_end-align),"center"))page->centered=1;
  const char*padding=property(body,rule_end,"padding");
  if(padding){
   int value=decimal(padding,rule_end);
   if(value>0){
    if(find_ci(padding,(size_t)(rule_end-padding),"vh"))*top_pad=view_h*value/100;
    else *top_pad=value;
   }
   const char*p=padding;while(p<rule_end&&*p!=' ')p++;while(p<rule_end&&space(*p))p++;
   if(p<rule_end&&*p>='0'&&*p<='9'){
    int h=decimal(p,rule_end);if(find_ci(p,(size_t)(rule_end-p),"em"))h*=16;
    if(h>0&&h<200)*side_pad=h;
   }
  }
 }
 const char*a=css_rule(open,close,"a",&rule_end);
 if(a)page->link_color=css_color(a,rule_end,"color",page->link_color);
}
typedef struct {
 MX_WEB_PAGE*page;int left,width,y,kind,link;
 char buffer[2048];size_t used;
 int in_head,skip;
} LAYOUT;
static void add_line(LAYOUT*state,const char*value,size_t length,int scale,int link,int kind){
 MX_WEB_PAGE*page=state->page;
 if(page->line_count>=MX_WEB_MAX_LINES||!length)return;
 MX_WEB_LINE*line=&page->lines[page->line_count++];
 line->scale=scale;line->link=link;line->h=scale==2?40:25;
 line->w=(int)length*8*scale;
 line->x=state->left+(page->centered?(state->width-line->w)/2:0);
 line->y=state->y;line->color=link>=0?page->link_color:page->foreground;
 if(kind==2)line->color=page->foreground;
 for(size_t i=0;i<length;i++)line->text[i]=value[i];line->text[length]=0;
 state->y+=line->h;
}
static void flush(LAYOUT*state){
 size_t start=0,end=state->used;
 while(start<end&&space(state->buffer[start]))start++;
 while(end>start&&space(state->buffer[end-1]))end--;
 if(start==end){state->used=0;return;}
 int scale=state->kind==2?2:1;
 size_t max=(size_t)(state->width/(8*scale));if(max>111)max=111;if(max<1)max=1;
 while(start<end&&state->page->line_count<MX_WEB_MAX_LINES){
  size_t stop=start+max;if(stop>end)stop=end;
  if(stop<end){size_t space_at=stop;while(space_at>start&&state->buffer[space_at]!=' ')space_at--;if(space_at>start)stop=space_at;}
  while(stop>start&&space(state->buffer[stop-1]))stop--;
  if(stop==start)stop=start+1;
  add_line(state,state->buffer+start,stop-start,scale,state->link,state->kind);
  start=stop;while(start<end&&space(state->buffer[start]))start++;
 }
 state->y+=state->kind==2?15:state->link>=0?8:11;
 state->used=0;
}
static void append(LAYOUT*state,char c){
 if(c=='\n'||c=='\r'||c=='\t')c=' ';
 if((unsigned char)c<32||(unsigned char)c>126)c='?';
 if(c==' '&&(state->used==0||state->buffer[state->used-1]==' '))return;
 if(state->used+1>=sizeof(state->buffer))flush(state);
 state->buffer[state->used++]=c;
}
static void attribute(const char*from,const char*end,const char*name,char*out,size_t cap){
 out[0]=0;size_t len=0;while(name[len])len++;
 for(const char*p=from;p+len<end;p++){
  if(p>from&&!space(p[-1]))continue;
  size_t i=0;while(i<len&&low(p[i])==name[i])i++;if(i!=len)continue;
  const char*q=p+len;while(q<end&&space(*q))q++;
  if(q==end||*q!='=')continue;q++;while(q<end&&space(*q))q++;
  char quote=0;if(q<end&&(*q=='"'||*q=='\''))quote=*q++;
  size_t n=0;while(q<end&&n+1<cap&&(quote?*q!=quote:!space(*q)&&*q!='>'))out[n++]=*q++;
  out[n]=0;return;
 }
}
static char entity(const char*html,size_t left,size_t*consumed){
 if(left>=4&&same(html,4,"amp;")){*consumed=4;return '&';}
 if(left>=3&&same(html,3,"lt;")){*consumed=3;return '<';}
 if(left>=3&&same(html,3,"gt;")){*consumed=3;return '>';}
 if(left>=5&&same(html,5,"quot;")){*consumed=5;return '"';}
 if(left>=5&&same(html,5,"nbsp;")){*consumed=5;return ' ';}
 if(left>=5&&same(html,5,"apos;")){*consumed=5;return '\'';}
 return '&';
}
void mx_web_layout(MX_WEB_PAGE*page,const char*html,size_t length,int view_w,int view_h){
 if(!page)return;
 page->background=0xffffff;page->foreground=0x20242c;page->link_color=0x245ac8;
 page->line_count=page->link_count=page->centered=0;page->content_height=view_h;page->title[0]=0;
 if(!html||view_w<=0||view_h<=0)return;
 const char*title=find_ci(html,length,"<title");
 if(title){
  const char*end=html+length;while(title<end&&*title!='>')title++;
  if(title<end){title++;const char*close=find_ci(title,(size_t)(end-title),"</title");
   if(close){size_t n=0;while(title<close&&n+1<sizeof(page->title))page->title[n++]=*title++;page->title[n]=0;}
  }
 }
 int width=view_w-64,top_pad=32,side_pad=32;
 if(width<120)width=view_w>24?view_w-24:view_w;
 parse_css(page,html,length,view_h,&width,&top_pad,&side_pad);
 if(width>view_w-side_pad*2)width=view_w-side_pad*2;
 if(width<80)width=80;
 if(top_pad>view_h)top_pad=view_h/2;
 LAYOUT state;state.page=page;state.width=width;state.left=(view_w-width)/2;state.y=top_pad;state.link=-1;state.kind=1;state.used=0;state.in_head=0;state.skip=0;
 int body_found=find_ci(html,length,"<body")!=0;
 if(body_found)state.in_head=1;
 for(size_t pos=0;pos<length;){
  if(html[pos]=='<'){
   size_t end=pos+1;while(end<length&&html[end]!='>')end++;
   if(end==length)break;
   size_t start=pos+1;int close=0;if(start<end&&html[start]=='/'){close=1;start++;}
   while(start<end&&space(html[start]))start++;
   size_t stop=start;while(stop<end&&((html[stop]>='a'&&html[stop]<='z')||(html[stop]>='A'&&html[stop]<='Z')||(html[stop]>='0'&&html[stop]<='9')))stop++;
   size_t n=stop-start;
   if(same(html+start,n,"head")){state.in_head=!close;pos=end+1;continue;}
   if(same(html+start,n,"body")){state.in_head=0;pos=end+1;continue;}
   if(same(html+start,n,"style")||same(html+start,n,"script")){state.skip=close?0:1;pos=end+1;continue;}
   if(state.in_head||state.skip){pos=end+1;continue;}
   if(same(html+start,n,"p")||same(html+start,n,"div")||same(html+start,n,"section")||same(html+start,n,"article")||same(html+start,n,"br")){
    flush(&state);state.kind=1;
   }else if(same(html+start,n,"h1")||same(html+start,n,"h2")){
    flush(&state);state.kind=close?1:2;
   }else if(same(html+start,n,"a")){
    flush(&state);
    if(close)state.link=-1;
    else if(page->link_count<MX_WEB_MAX_LINKS){
     int link=page->link_count++;
     attribute(html+stop,html+end,"href",page->links[link],sizeof(page->links[link]));
     state.link=page->links[link][0]?link:-1;
    }
   }else if(same(html+start,n,"li")){
    flush(&state);if(!close){append(&state,'*');append(&state,' ');}
   }
   pos=end+1;continue;
  }
  if(!state.in_head&&!state.skip){
   char c=html[pos];
   if(c=='&'){size_t consumed=0;c=entity(html+pos+1,length-pos-1,&consumed);pos+=consumed;}
   append(&state,c);
  }
  pos++;
 }
 flush(&state);
 page->content_height=state.y+32;
 if(page->content_height<view_h)page->content_height=view_h;
}
int mx_web_link_at(const MX_WEB_PAGE*page,int x,int y){
 if(!page)return -1;
 for(int i=0;i<page->line_count;i++){
  const MX_WEB_LINE*line=&page->lines[i];
  if(line->link>=0&&x>=line->x&&x<line->x+line->w&&y>=line->y&&y<line->y+line->h)return line->link;
 }
 return -1;
}
