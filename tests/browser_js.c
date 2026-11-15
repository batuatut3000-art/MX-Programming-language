#include "mx_browser_js.h"
#include "mx_browser_layout.h"
static MX_WEB_PAGE page;
static char output[4096],title[112];
static size_t length(const char*s){size_t n=0;while(s[n])n++;return n;}
static int contains(const char*s,const char*part){for(size_t i=0;s[i];i++){size_t j=0;while(part[j]&&s[i+j]==part[j])j++;if(!part[j])return 1;}return 0;}
int main(void){
 const char*html="<html><head><title>Old</title></head><body><div id=\"message\">Before</div><script>let word = 'Hello'; document.write('<h1>' + word + '</h1>'); document.title = 'New title'; document.getElementById('message').textContent = 'After';</script></body></html>";
 size_t used=mx_js_render(html,length(html),output,sizeof(output),title,sizeof(title));
 if(!used||!contains(output,"<h1>Hello</h1>")||!contains(output,">After</div>"))return 1;
 if(!contains(title,"New title")||contains(output,"document.write"))return 2;
 mx_web_layout(&page,output,used,1024,600);
 if(page.line_count<2||!contains(page.lines[0].text,"After"))return 3;
 const char*external="<p>Keep</p><script src=\"app.js\"></script><p>More</p>";
 used=mx_js_render(external,length(external),output,sizeof(output),title,sizeof(title));
 if(!contains(output,"Keep")||!contains(output,"More"))return 4;
 return 0;
}
