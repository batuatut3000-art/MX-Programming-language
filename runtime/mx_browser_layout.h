#ifndef MX_BROWSER_LAYOUT_H
#define MX_BROWSER_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#define MX_WEB_MAX_LINES 256
#define MX_WEB_MAX_LINKS 32
typedef struct {
 int x,y,w,h,scale,link;
 uint32_t color;
 char text[112];
} MX_WEB_LINE;
typedef struct {
 uint32_t background,foreground,link_color;
 int line_count,link_count,content_height,centered;
 char title[112];
 MX_WEB_LINE lines[MX_WEB_MAX_LINES];
 char links[MX_WEB_MAX_LINKS][256];
} MX_WEB_PAGE;
void mx_web_layout(MX_WEB_PAGE*page,const char*html,size_t length,int viewport_width,int viewport_height);
int mx_web_link_at(const MX_WEB_PAGE*page,int x,int y);
#endif
