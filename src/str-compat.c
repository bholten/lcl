#include <string.h>

#include "str-compat.h"

size_t strnlen(const char *s, size_t n) {
  const char *p = memchr(s, 0, n);
  return p ? (size_t)(p - s) : n;
}

char *strndup(const char *s, size_t n) {
  size_t l = strnlen(s, n);
  char *d = malloc(l + 1);

  if (!d) return NULL;

  memcpy(d, s, l);
  d[l] = '\0';
  return d;
}
