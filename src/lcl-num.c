#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcl-values.h"

/* Bugfix: Replace the runtime locale's decimal separator with ASCII
 * '.' in `buf`, in place. May shrink the string if the separator is
 * multi-byte.  No-op when the locale already uses '.'. Used to
 * enforce locale-independent (deterministic) float stringification */
void lcl_normalize_decimal_to_c(char *buf) {
  struct lconv *lc;
  const char *sep;
  char *p;
  size_t sep_len;

  if (!buf) {
    return;
  }
  lc = localeconv();
  if (!lc) {
    return;
  }
  sep = lc->decimal_point;
  if (!sep || !*sep || strcmp(sep, ".") == 0) {
    return;
  }

  p = strstr(buf, sep);
  if (!p) {
    return;
  }
  sep_len = strlen(sep);
  *p = '.';
  if (sep_len > 1) {
    memmove(p + 1, p + sep_len, strlen(p + sep_len) + 1);
  }
}

/*
 * Bugfix:
 *
 * Parse `s` as a double using C-locale semantics ('.' is the decimal
 * separator), regardless of the runtime locale. Sets `*out` if any
 * digits matched. Returns the byte offset in `s` where parsing
 * stopped (i.e. parsed `s[0..return)`); returns 0 when nothing
 * matched. Callers can check `s[return] == '\0'` for "fully
 * consumed".
 *
 * Implementation: if the locale uses a non-'.' decimal separator,
 * copies `s` into a small stack buffer with the separator
 * substituted, then calls strtod and maps `endptr` back to a position
 * in `s`. Inputs longer than the buffer fall through to a plain
 * strtod — those won't round-trip in non-'.' locales, but the buffer
 * is sized to comfortably fit our `%.17g` outputs. */
size_t lcl_parse_double_c(const char *s, double *out) {
  char buf[64];
  const char *to_parse = s;
  char *endptr;
  struct lconv *lc;
  const char *sep;
  size_t pre = 0, sep_len = 0;
  int substituted = 0;
  double val;

  if (!s || !out) {
    return 0;
  }

  lc = localeconv();
  sep = lc ? lc->decimal_point : ".";

  if (sep && *sep && strcmp(sep, ".") != 0) {
    const char *dot = strchr(s, '.');

    if (dot) {
      size_t slen = strlen(s);
      pre = (size_t)(dot - s);
      sep_len = strlen(sep);

      /* Total required: pre + sep_len + (slen - pre - 1) + 1 (NUL). */
      if (pre + sep_len + slen - pre <= sizeof(buf)) {
        memcpy(buf, s, pre);
        memcpy(buf + pre, sep, sep_len);
        memcpy(buf + pre + sep_len, dot + 1, slen - pre); /* includes NUL */
        to_parse = buf;
        substituted = 1;
      }
    }
  }

  val = strtod(to_parse, &endptr);
  if (endptr == to_parse) {
    return 0;
  }
  *out = val;

  if (substituted) {
    /* Map endptr (inside buf) back to a position in `s`. */
    size_t off_buf = (size_t)(endptr - buf);

    if (off_buf <= pre) {
      return off_buf; /* stopped before / at the start of the separator */
    }

    /* Past the separator — strtod consumed the multi-byte sep atomically. */
    return pre + 1 + (off_buf - pre - sep_len);
  }

  return (size_t)(endptr - s);
}

lcl_value *lcl_int_new(const long n) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    return NULL;
  }

  v->type = LCL_INT;
  v->refc = 1;
  v->as.i = n;

  lcl_value_to_string(v);

  return v;
}

lcl_value *lcl_float_new(const double f) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    return NULL;
  }

  v->type = LCL_FLOAT;
  v->refc = 1;
  v->as.f = f;

  lcl_value_to_string(v);

  return v;
}

/* Bugfix: Convert a double to long, rejecting NaN, Inf, and
 * out-of-range inputs.
 *
 * The classic safe range is [LONG_MIN, -(double)LONG_MIN): LONG_MIN
 * is exactly representable as a double, and -(double)LONG_MIN equals
 * 2^N (one past LONG_MAX) and is also exactly representable.
 *
 * Casting (long)f is defined for any finite f in this half-open
 * range. */
lcl_result lcl_double_to_long(double f, long *out) {
  if (isnan(f) || isinf(f)) {
    return LCL_ERROR;
  }

  if (f < (double)LONG_MIN || f >= -(double)LONG_MIN) {
    return LCL_ERROR;
  }

  *out = (long)f;

  return LCL_OK;
}

lcl_result lcl_value_to_int(lcl_value *value, long *out) {
  if (!value || !out) {
    return LCL_ERROR;
  }

  switch (value->type) {
  case LCL_INT: *out = value->as.i; return LCL_OK;

  case LCL_FLOAT: return lcl_double_to_long(value->as.f, out);

  case LCL_STRING: {
    char *endptr;
    const char *str = lcl_value_to_string(value);
    long val;

    errno = 0;
    val = strtol(str, &endptr, 10);

    if (endptr != str && *endptr == '\0' && errno != ERANGE) {
      *out = val;

      return LCL_OK;
    }

    break;
  }

  default: break;
  }

  return LCL_ERROR;
}

lcl_result lcl_value_to_float(lcl_value *value, double *out) {
  if (!value || !out) {
    return LCL_ERROR;
  }

  switch (value->type) {
  case LCL_INT: *out = (double)value->as.i; return LCL_OK;

  case LCL_FLOAT: *out = value->as.f; return LCL_OK;

  case LCL_STRING: {
    const char *str = lcl_value_to_string(value);
    double val;

    /* Bugfix: lcl_parse_double_c is locale-independent — strings
     * serialized by lcl_reify_str_float always use ASCII '.',
     * regardless of the runtime locale. */
    if (lcl_parse_double_c(str, &val) > 0) {
      *out = val;

      return LCL_OK;
    }

    break;
  }

  default: break;
  }

  return LCL_ERROR;
}
