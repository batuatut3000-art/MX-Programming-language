#include "mx_browser_text.h"
static size_t count(const char*s){size_t n=0;while(s[n])n++;return n;}
static int equal(const char*a,const char*b){size_t i=0;while(a[i]&&a[i]==b[i])i++;return a[i]==b[i];}
static int contains(const char*text,const char*word){
 for(size_t i=0;text[i];i++){size_t j=0;while(word[j]&&text[i+j]==word[j])j++;if(!word[j])return 1;}
 return 0;
}
int main(void){
 const char*html="<!doctype html><html><head><title>Example Domain</title><style>body{color:red}</style></head><body><h1>Example Domain</h1><p>This domain is for examples.</p><script>bad()</script></body></html>";
 char chunked[512],plain[512];size_t length=count(html),pos=0;
 const char*hex="0123456789abcdef";char digits[16];int used=0;
 for(size_t value=length;value;value/=16)digits[used++]=hex[value%16];
 while(used)chunked[pos++]=digits[--used];
 chunked[pos++]='\r';chunked[pos++]='\n';
 for(size_t i=0;i<length;i++)chunked[pos++]=html[i];
 chunked[pos++]='\r';chunked[pos++]='\n';
 chunked[pos++]='0';chunked[pos++]='\r';chunked[pos++]='\n';
 chunked[pos++]='\r';chunked[pos++]='\n';chunked[pos]=0;
 length=pos;
 if(!mx_http_dechunk(chunked,&length))return 1;
 if(length!=count(html)||!equal(chunked,html))return 2;
 mx_html_to_text(chunked,length,plain,sizeof(plain));
 if(!contains(plain,"Example Domain")||!contains(plain,"This domain is for examples."))return 3;
 if(contains(plain,"color:red")||contains(plain,"bad()"))return 4;
 char malformed[]="4\r\nabc\r\n0\r\n\r\n";
 length=count(malformed);
 if(mx_http_dechunk(malformed,&length))return 5;
 if(!equal(malformed,"4\r\nabc\r\n0\r\n\r\n"))return 6;
 return 0;
}
