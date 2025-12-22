#ifndef LCL_STR_H
#define LCL_STR_H

#include <stdlib.h>

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} lcl_str;

void lcl_str_init(lcl_str *str);
void lcl_str_free(lcl_str *str);
int lcl_str_reserve(lcl_str *st, size_t need);
int lcl_str_putc(lcl_str  *st, int c);
int lcl_str_puts(lcl_str *st, const char *s);
int lcl_str_write(lcl_str *buf, const char *str, int n);
void lcl_str_append(lcl_str *str, const char *s, size_t n);

#endif
