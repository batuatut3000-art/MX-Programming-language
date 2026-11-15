#include "mx_browser_layout.h"
static MX_WEB_PAGE page;
static size_t count(const char*s){size_t n=0;while(s[n])n++;return n;}
static int contains(const char*a,const char*b){for(size_t i=0;a[i];i++){size_t j=0;while(b[j]&&a[i+j]==b[j])j++;if(!b[j])return 1;}return 0;}
int main(void){
 const char*html="<!doctype html><html><head><style>html{color-scheme:light dark}body{background:#eee;color:#222;font:16px sans-serif;max-width:26em;margin:auto;padding:25vh 2em;text-align:center}a{color:#36c}</style></head><body><h1>Example Domain</h1><p>This domain is for examples &amp; tests.</p><p><a href=\"https://www.iana.org/domains/example\">Learn more</a></p><script>bad()</script></body></html>";
 mx_web_layout(&page,html,count(html),1024,610);
 if(page.background!=0xeeeeee||page.foreground!=0x222222||page.link_color!=0x3366cc)return 1;
 if(page.line_count<3||page.link_count!=1||page.content_height<610)return 2;
 if(!contains(page.lines[0].text,"Example Domain")||page.lines[0].scale!=2)return 3;
 int linked=0;
 for(int i=0;i<page.line_count;i++){
  if(contains(page.lines[i].text,"bad()"))return 4;
  if(contains(page.lines[i].text,"Learn more")){
   linked=1;if(mx_web_link_at(&page,page.lines[i].x+1,page.lines[i].y+1)!=0)return 5;
  }
 }
 if(!linked||!contains(page.links[0],"iana.org"))return 6;
 const char*current="<!doctype html><html lang=en><head><title>Example Domain</title><style>html{color-scheme:light dark;background:light-dark(#eee,#222)}body{font:16px/1.6 system-ui,sans-serif;max-width:26em;margin:auto;padding:25vh 2em 2em;text-align:center}</style></head><body><p>This domain is for use in documentation examples without needing permission.</p></body></html>";
 mx_web_layout(&page,current,count(current),1024,610);
 if(page.background!=0xeeeeee||!contains(page.title,"Example Domain"))return 7;
 if(page.line_count<1||!contains(page.lines[0].text,"This domain"))return 8;
 if(page.lines[0].y<140||!page.centered)return 9;
 return 0;
}
