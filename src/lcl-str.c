#include <memory.h>
#include <string.h>

#include "lcl-str.h"

void lcl_str_init(lcl_str *str) {
  str->buf = NULL;
  str->len = 0;
  str->cap = 32;
}

void lcl_str_free(lcl_str *str) {
  if (str->buf) {
    free(str->buf);
  }

  free(str);
}

int lcl_str_reserve(lcl_str *st, size_t need) {
  size_t n = st->len + need + 1;
  size_t cap = st->cap ? st->cap : 64;
  char *p;

  if (n <= st->cap) return 1;

  while (cap < n) {
    size_t next = cap << 1;

    if (next <= cap) {
      return 0;
    }

    cap = next;
  }

  p = (char *)realloc(st->buf, cap);

  if (!p) return 0;

  st->buf = p;
  st->cap = cap;

  return 1;
}

int lcl_str_putc(lcl_str  *st, int c) {
  if (!lcl_str_reserve(st, 1)) return 0;

  st->buf[st->len++] = (char)c;
  st->buf[st->len] = '\0';
  
  return 1;
}

int lcl_str_puts(lcl_str *st, const char *s) {
  return lcl_str_write(st, s, strlen(s));
}

int lcl_str_write(lcl_str *str, const char *data, int n) {
  if (n == 0) return 1;
  if (!lcl_str_reserve(str, n)) return 0;

  memcpy(str->buf + str->len, data, n);
  str->len += n;
  str->buf[str->len] = '\0';

  return 1;
}
