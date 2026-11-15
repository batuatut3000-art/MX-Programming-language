#include "mx_browser_js.h"

#define JS_VARS 16
#define JS_PATCHES 16
typedef struct {char name[32],value[512];} VAR;
typedef struct {char id[64],value[512];} PATCH;
typedef struct {VAR vars[JS_VARS];PATCH patches[JS_PATCHES];int var_count,patch_count;char*title;size_t title_cap;char*out;size_t used,cap;} STATE;
static STATE state;
static char lower(char c){return c>='A'&&c<='Z'?(char)(c+32):c;}
static int ident(char c){return c>='a'&&c<='z'||c>='A'&&c<='Z'||c>='0'&&c<='9'||c=='_'||c=='$';}
static int ws(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static const char*skip(const char*p,const char*end){while(p<end&&ws(*p))p++;return p;}
static int begins(const char*p,const char*end,const char*word){while(*word){if(p==end||*p++!=*word++)return 0;}return 1;}
static const char*find_ci(const char*p,const char*end,const char*word){for(;p<end;p++){const char*q=p,*w=word;while(q<end&&*w&&lower(*q)==lower(*w)){q++;w++;}if(!*w)return p;}return 0;}
static void put(char*out,size_t*used,size_t cap,char c){if(*used+1<cap)out[(*used)++]=c;}
static void copy(char*out,size_t cap,const char*p,const char*end){size_t n=0;if(!cap)return;while(p<end&&n+1<cap)out[n++]=*p++;out[n]=0;}
static const char*quoted(const char*p,const char*end,char*out,size_t cap){
 size_t n=0;if(p==end||(*p!='\''&&*p!='"'))return p;
 char quote=*p++;
 while(p<end&&*p!=quote){char c=*p++;if(c=='\\'&&p<end){c=*p++;if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';}put(out,&n,cap,c);}
 if(cap)out[n]=0;
 return p<end?p+1:p;
}
static const char*expression(STATE*s,const char*p,const char*end,char*out,size_t cap){
 size_t n=0;if(cap)out[0]=0;
 for(;;){
  p=skip(p,end);if(p==end)break;
  if(*p=='\''||*p=='"'){char word[512];p=quoted(p,end,word,sizeof(word));for(size_t j=0;word[j];j++)put(out,&n,cap,word[j]);}
  else if(ident(*p)){const char*start=p;while(p<end&&ident(*p))p++;
   int matched=0;for(int i=0;i<s->var_count;i++){size_t j=0;while(start+j<p&&s->vars[i].name[j]&&start[j]==s->vars[i].name[j])j++;
    if(start+j==p&&s->vars[i].name[j]==0){for(size_t k=0;s->vars[i].value[k];k++)put(out,&n,cap,s->vars[i].value[k]);matched=1;break;}}
   if(!matched)break;
  }else break;
  p=skip(p,end);if(p==end||*p!='+')break;p++;
 }
 if(cap)out[n]=0;return p;
}
static const char*statement_end(const char*p,const char*end){char quote=0;for(;p<end;p++){
  if(quote){if(*p=='\\'&&p+1<end){p++;continue;}if(*p==quote)quote=0;}
  else if(*p=='\''||*p=='"')quote=*p;
  else if(*p==';')return p;
 }return end;}
static void execute(STATE*s,const char*code,const char*end){
 int steps=0;
 while(code<end&&steps++<64){const char*stop=statement_end(code,end);const char*p=skip(code,stop);
  if(begins(p,stop,"let ")||begins(p,stop,"var ")||begins(p,stop,"const ")){
   p+=*p=='c'?6:4;p=skip(p,stop);const char*name=p;while(p<stop&&ident(*p))p++;
   size_t len=(size_t)(p-name);p=skip(p,stop);
   if(len&&len<32&&p<stop&&*p=='='&&s->var_count<JS_VARS){VAR*v=&s->vars[s->var_count];copy(v->name,sizeof(v->name),name,name+len);expression(s,p+1,stop,v->value,sizeof(v->value));s->var_count++;}
  }else if(begins(p,stop,"document.write(")||begins(p,stop,"document.writeln(")){
   int newline=begins(p,stop,"document.writeln(");p+=newline?17:15;
   char value[1024];expression(s,p,stop,value,sizeof(value));
   for(size_t j=0;value[j];j++)put(s->out,&s->used,s->cap,value[j]);
   if(newline)put(s->out,&s->used,s->cap,'\n');
  }else if(begins(p,stop,"document.title")){
   p=skip(p+14,stop);if(p<stop&&*p=='='&&s->title&&s->title_cap)expression(s,p+1,stop,s->title,s->title_cap);
  }else if(begins(p,stop,"document.getElementById(")){
   p=skip(p+24,stop);
   if(p<stop&&(*p=='\''||*p=='"')&&s->patch_count<JS_PATCHES){PATCH*patch=&s->patches[s->patch_count];
    p=quoted(p,stop,patch->id,sizeof(patch->id));p=skip(p,stop);
    if(p<stop&&*p==')'){p=skip(p+1,stop);
     if(begins(p,stop,".textContent")||begins(p,stop,".innerHTML")){
      p+=* (p+1)=='t'?12:10;p=skip(p,stop);
      if(p<stop&&*p=='='){expression(s,p+1,stop,patch->value,sizeof(patch->value));s->patch_count++;}
     }
    }
   }
  }
  code=stop<end?stop+1:end;
 }
}
static void patch_html(STATE*s,PATCH*patch){
 const char*end=s->out+s->used;const char*p=s->out;
 while((p=find_ci(p,end,"id="))!=0){const char*q=p+3;q=skip(q,end);char id[64];
  if(q<end&&(*q=='\''||*q=='"'))q=quoted(q,end,id,sizeof(id));
  else {const char*start=q;while(q<end&&!ws(*q)&&*q!='>')q++;copy(id,sizeof(id),start,q);}
  size_t j=0;while(id[j]&&id[j]==patch->id[j])j++;
  if(!id[j]&&!patch->id[j]){
   while(q<end&&*q!='>')q++;if(q==end)return;q++;
   const char*old_end=q;while(old_end<end&&*old_end!='<')old_end++;
   size_t left=(size_t)(q-s->out),right=(size_t)(old_end-s->out),new_len=0;
   while(patch->value[new_len])new_len++;
   if(s->used-(right-left)+new_len+1>=s->cap)return;
   if(new_len>right-left)for(size_t k=s->used+1;k>right;k--)s->out[k+new_len-(right-left)-1]=s->out[k-1];
   else for(size_t k=right;k<=s->used;k++)s->out[k-(right-left)+new_len]=s->out[k];
   for(size_t k=0;k<new_len;k++)s->out[left+k]=patch->value[k];
   s->used=s->used-(right-left)+new_len;return;
  }
  p=q;
 }
}
size_t mx_js_render(const char*html,size_t length,char*out,size_t cap,char*title,size_t title_cap){
 if(!out||!cap)return 0;state.out=out;state.used=0;state.cap=cap;
 state.var_count=state.patch_count=0;state.title=title;state.title_cap=title_cap;if(title&&title_cap)title[0]=0;
 if(!html){out[0]=0;return 0;}
 const char*p=html,*end=html+length;int scripts=0;
 while(p<end){const char*open=find_ci(p,end,"<script");if(!open||scripts>=16)break;
  const char*tag_end=open;while(tag_end<end&&*tag_end!='>')tag_end++;if(tag_end==end)break;
  const char*close=find_ci(tag_end+1,end,"</script");if(!close)break;
  for(const char*q=p;q<open;q++)put(out,&state.used,cap,*q);
  execute(&state,tag_end+1,close);scripts++;
  p=close;while(p<end&&*p!='>')p++;if(p<end)p++;
 }
 while(p<end)put(out,&state.used,cap,*p++);out[state.used]=0;
 for(int i=0;i<state.patch_count;i++)patch_html(&state,&state.patches[i]);
 return state.used;
}
