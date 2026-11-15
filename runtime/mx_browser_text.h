#ifndef MX_BROWSER_TEXT_H
#define MX_BROWSER_TEXT_H
#include <stddef.h>
int mx_http_dechunk(char *body, size_t *length);
size_t mx_html_to_text(const char *html, size_t length, char *text, size_t capacity);
#endif
