#include <string.h>

#include "lcl-name.h"

int lcl_name_is_start(int c) {
  return (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int lcl_name_is_char(int c) {
  return (lcl_name_is_start(c) || (c >= '0' && c <= '9'));
}

lcl_name_kind lcl_name_classify_n(const char *s, size_t n) {
  size_t i = 0;
  int nseg = 0;

  if (s == NULL || n == 0) {
    return LCL_NAME_INVALID;
  }

  for (;;) {
    size_t seg = 0;

    if (i < n && lcl_name_is_start((unsigned char)s[i])) {
      i++;
      seg = 1;

      while (i < n && lcl_name_is_char((unsigned char)s[i])) {
        i++;
      }
    }

    if (!seg) {
      return LCL_NAME_INVALID;
    }

    nseg++;

    if (i == n) {
      return (nseg > 1) ? LCL_NAME_QUALIFIED : LCL_NAME_SIMPLE;
    }

    if (i + 1 < n && s[i] == ':' && s[i + 1] == ':') {
      i += 2;
      continue;
    }

    return LCL_NAME_INVALID;
  }
}

lcl_name_kind lcl_name_classify(const char *s) {
  return s ? lcl_name_classify_n(s, strlen(s)) : LCL_NAME_INVALID;
}

const char *lcl_name_check_ref(const char *s, size_t n) {
  size_t i = 0;

  if (s == NULL || n == 0) {
    return "empty variable name in '${}'";
  }

  for (;;) {
    /* `i` sits where a segment must start. */
    if (i >= n) {
      return "empty segment in qualified variable name"; /* ${foo::} */
    }

    if (s[i] == ':') {
      return "empty segment in qualified variable name"; /* ${::a} ${a::::b} */
    }

    if (!lcl_name_is_start((unsigned char)s[i])) {
      if (lcl_name_is_char((unsigned char)s[i])) {
        return "name segment must start with a letter or '_'"; /* ${a::1b} */
      }

      return "invalid character in variable name"; /* ${a b} */
    }

    i++;

    while (i < n && lcl_name_is_char((unsigned char)s[i])) {
      i++;
    }

    if (i == n) {
      return NULL;
    }

    if (i + 1 < n && s[i] == ':' && s[i + 1] == ':') {
      i += 2;
      continue;
    }

    return "invalid character in variable name"; /* ${a:b}, ${a.b}, ... */
  }
}

int lcl_name_has_sep(const char *s) {
  return s != NULL && strstr(s, "::") != NULL;
}

int lcl_name_has_empty_seg(const char *s) {
  const char *p;

  if (s == NULL || s[0] == '\0' || strncmp(s, "::", 2) == 0) {
    return 1;
  }

  for (p = strstr(s, "::"); p; p = strstr(p, "::")) {
    p += 2;

    if (*p == '\0' || strncmp(p, "::", 2) == 0) {
      return 1;
    }
  }

  return 0;
}
