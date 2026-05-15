#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "lcl-values.h"

lcl_value *lcl_string_new(const char *str) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    return NULL;
  }

  if (str) {
    size_t n = strlen(str);
    v->str_repr = (char *)malloc(n + 1);

    if (!v->str_repr) {
      free(v);
      return NULL;
    }

    memcpy(v->str_repr, str, n + 1);
  }

  v->refc = 1;
  v->type = LCL_STRING;

  return v;
}

static void lcl_reify_str_int(lcl_value *value) {
  char buf[32];
  int m = snprintf(buf, sizeof(buf), "%ld", value->as.i);
  if (m < 0 || (size_t)m >= sizeof(buf)) {
    return;
  }
  value->str_repr = (char *)malloc((size_t)m + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, (size_t)m + 1);
}

static void lcl_reify_str_float(lcl_value *value) {
  char buf[32];
  int m = snprintf(buf, sizeof(buf), "%.17g", value->as.f);
  if (m < 0 || (size_t)m >= sizeof(buf)) {
    return;
  }
  value->str_repr = (char *)malloc((size_t)m + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, (size_t)m + 1);
}

enum elem_style { ELEM_BARE, ELEM_BRACED, ELEM_QUOTED };

static enum elem_style choose_element_style(const char *s) {
  int has_special = 0;
  int brace_depth = 0;
  const char *p;

  if (!s || !*s) {
    return ELEM_BRACED;
  }

  for (p = s; *p; p++) {
    char c = *p;

    if (c == ' ' || c == '\t' || c == '\n' || c == '{' || c == '}' ||
        c == '[' || c == ']' || c == '$' || c == '"' || c == '\\' || c == ';') {
      has_special = 1;
    }

    if (c == '{') {
      brace_depth++;
    } else if (c == '}') {
      brace_depth--;

      if (brace_depth < 0) {
        return ELEM_QUOTED;
      }
    }
  }

  if (brace_depth != 0) {
    return ELEM_QUOTED;
  }

  return has_special ? ELEM_BRACED : ELEM_BARE;
}

/* Bugfix: Growing string buffer for single-pass list/dict
 * stringification.
 *
 * The previous implementation sized the result with one pass calling
 * `lcl_value_to_string(elem)`, then wrote with a second pass calling
 * it again. If a recursive reify failed in pass 1 (leaving str_repr
 * NULL → "" fallback) and then succeeded in pass 2 (returning the
 * real, longer string), the buffer was undersized and pass 2
 * overflowed it. A single pass with a growing buffer eliminates the
 * inconsistency entirely. */

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} sbuf;

static int sbuf_reserve(sbuf *b, size_t additional) {
  /* Reserve room for `additional` more bytes plus 1 for the trailing NUL. */
  size_t want;
  size_t newcap;
  char *p;

  if (additional > (size_t)-1 - 1 - b->len) {
    return 0;
  }

  want = b->len + additional + 1;

  if (want <= b->cap) {
    return 1;
  }

  newcap = b->cap ? b->cap : 32;

  while (newcap < want) {
    if (newcap > ((size_t)-1) / 2) {
      return 0;
    }

    newcap *= 2;
  }

  p = (char *)realloc(b->buf, newcap);

  if (!p) {
    return 0;
  }

  b->buf = p;
  b->cap = newcap;

  return 1;
}

static int sbuf_append(sbuf *b, const char *s, size_t n) {
  if (!sbuf_reserve(b, n)) {
    return 0;
  }

  memcpy(b->buf + b->len, s, n);
  b->len += n;

  return 1;
}

static int sbuf_putc(sbuf *b, char c) {
  if (!sbuf_reserve(b, 1)) {
    return 0;
  }

  b->buf[b->len++] = c;

  return 1;
}

static char *sbuf_finish(sbuf *b) {
  if (!sbuf_reserve(b, 0)) {
    free(b->buf);
    return NULL;
  }

  b->buf[b->len] = '\0';

  return b->buf;
}

static int sbuf_append_styled(sbuf *b, const char *s, enum elem_style style) {
  size_t n;

  if (!s) {
    s = "";
  }
  n = strlen(s);

  switch (style) {
  case ELEM_BARE: return sbuf_append(b, s, n);
  case ELEM_BRACED:
    return sbuf_putc(b, '{') && sbuf_append(b, s, n) && sbuf_putc(b, '}');
  case ELEM_QUOTED: {
    const char *p;

    if (!sbuf_putc(b, '"')) {
      return 0;
    }

    for (p = s; *p; p++) {
      char c = *p;

      if (c == '\\' || c == '$' || c == '[' || c == '"') {
        if (!sbuf_putc(b, '\\')) {
          return 0;
        }
      }

      if (!sbuf_putc(b, c)) {
        return 0;
      }
    }

    return sbuf_putc(b, '"');
  }
  }

  return 0;
}

static void lcl_reify_str_list(lcl_value *value) {
  size_t len = lcl_list_len(value);
  sbuf b;
  size_t i;

  b.buf = NULL;
  b.len = 0;
  b.cap = 0;

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *s;
    enum elem_style style;
    int ok;

    if (i > 0) {
      if (!sbuf_putc(&b, ' ')) {
        free(b.buf);
        return;
      }
    }

    if (lcl_list_get(value, i, &elem) != LCL_OK) {
      continue;
    }

    s = lcl_value_to_string(elem);
    style = choose_element_style(s);
    ok = sbuf_append_styled(&b, s, style);
    lcl_ref_dec(elem);

    if (!ok) {
      free(b.buf);
      return;
    }
  }

  value->str_repr = sbuf_finish(&b);
}

static void lcl_reify_str_dict(lcl_value *value) {
  lcl_dict_it it;
  const char *key;
  lcl_value *val;
  sbuf b;
  int first = 1;

  b.buf = NULL;
  b.len = 0;
  b.cap = 0;
  it.i = 0;

  while (lcl_dict_iter((const lcl_value **)&value, &it, &key, &val) == LCL_OK) {
    const char *vs = lcl_value_to_string(val);
    enum elem_style ks = choose_element_style(key);
    enum elem_style vst = choose_element_style(vs);
    int ok;

    if (!first) {
      if (!sbuf_putc(&b, ' ')) {
        lcl_ref_dec(val);
        free(b.buf);
        return;
      }
    }

    first = 0;

    ok = sbuf_append_styled(&b, key, ks) &&
         sbuf_putc(&b, ' ') &&
         sbuf_append_styled(&b, vs, vst);
    lcl_ref_dec(val);

    if (!ok) {
      free(b.buf);
      return;
    }
  }

  value->str_repr = sbuf_finish(&b);
}

const char *lcl_value_to_string(lcl_value *value) {
  if (!value) {
    return "";
  }

  if (!value->str_repr) {
    switch (value->type) {
    case LCL_INT: lcl_reify_str_int(value); break;
    case LCL_FLOAT: lcl_reify_str_float(value); break;
    case LCL_STRING: break;
    case LCL_LIST: lcl_reify_str_list(value); break;
    case LCL_DICT: lcl_reify_str_dict(value); break;
    case LCL_OPAQUE: {
      const char *tag = value->as.opaque.type_tag;

      if (tag) {
        size_t len = strlen(tag) + 10;
        value->str_repr = (char *)malloc(len);

        if (value->str_repr) {
          snprintf(value->str_repr, len, "<opaque:%s>", tag);
        }
      } else {
        value->str_repr = (char *)malloc(9);

        if (value->str_repr) {
          memcpy(value->str_repr, "<opaque>", 9);
        }
      }
    } break;

    default:
      value->str_repr = (char *)malloc(4);

      if (!value->str_repr) {
        return "";
      }

      memcpy(value->str_repr, "<?>", 4);
      break;
    }
  }

  return value->str_repr ? value->str_repr : "";
}

lcl_value *lcl_value_new_string(const char *str) {
  return lcl_string_new(str);
}
