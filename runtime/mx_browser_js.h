#ifndef MX_BROWSER_JS_H
#define MX_BROWSER_JS_H
#include <stddef.h>
/* Bounded inline-script subset. Returns output size; output is always terminated. */
size_t mx_js_render(const char*html,size_t length,char*out,size_t cap,char*title,size_t title_cap);
#endif
