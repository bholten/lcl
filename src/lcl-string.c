#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "lcl-values.h"

lcl_value *lcl_string_new(const char *str) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

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
  /** TODO replace sprintf? **/
  int m = sprintf(buf, "%ld", value->as.i);
  value->str_repr = (char *)malloc((size_t)m + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, (size_t)m + 1);
}

static void lcl_reify_str_float(lcl_value *value) {
  char buf[32];
  int m = sprintf(buf, "%.17g", value->as.f);
  value->str_repr = (char *)malloc((size_t)m + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, (size_t)m + 1);
}

/* Check if string needs bracing for Tcl-like list output */
static int needs_braces(const char *s) {
  if (!s || !*s) return 1;  /* empty string needs braces */
  while (*s) {
    char c = *s++;
    if (c == ' ' || c == '\t' || c == '\n' || c == '{' || c == '}' ||
        c == '[' || c == ']' || c == '$' || c == '"' || c == '\\') {
      return 1;
    }
  }
  return 0;
}

static void lcl_reify_str_list(lcl_value *value) {
  size_t len = lcl_list_len(value);
  size_t total = 0;
  size_t i;
  char *buf, *p;

  /* Calculate total size needed */
  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *s;
    if (lcl_list_get(value, i, &elem) != LCL_OK) continue;
    s = lcl_value_to_string(elem);
    total += strlen(s);
    if (needs_braces(s)) total += 2;  /* for {} */
    lcl_ref_dec(elem);
  }
  total += len;  /* for spaces */

  buf = (char *)malloc(total + 1);
  if (!buf) return;

  p = buf;
  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *s;
    size_t slen;
    int braced;

    if (i > 0) *p++ = ' ';

    if (lcl_list_get(value, i, &elem) != LCL_OK) continue;
    s = lcl_value_to_string(elem);
    slen = strlen(s);
    braced = needs_braces(s);

    if (braced) *p++ = '{';
    memcpy(p, s, slen);
    p += slen;
    if (braced) *p++ = '}';

    lcl_ref_dec(elem);
  }
  *p = '\0';

  value->str_repr = buf;
}

static void lcl_reify_str_dict(lcl_value *value) {
  lcl_dict_it it = {0};
  const char *key;
  lcl_value *val;
  size_t total = 0;
  char *buf, *p;
  int first = 1;

  /* First pass: calculate size */
  while (lcl_dict_iter((const lcl_value **)&value, &it, &key, &val) == LCL_OK) {
    const char *vs = lcl_value_to_string(val);
    total += strlen(key) + strlen(vs) + 2;  /* key, value, spaces */
    if (needs_braces(key)) total += 2;
    if (needs_braces(vs)) total += 2;
    lcl_ref_dec(val);
  }

  buf = (char *)malloc(total + 1);
  if (!buf) return;

  p = buf;
  it.i = 0;
  while (lcl_dict_iter((const lcl_value **)&value, &it, &key, &val) == LCL_OK) {
    const char *vs = lcl_value_to_string(val);
    size_t klen = strlen(key);
    size_t vlen = strlen(vs);
    int kbraced = needs_braces(key);
    int vbraced = needs_braces(vs);

    if (!first) *p++ = ' ';
    first = 0;

    if (kbraced) *p++ = '{';
    memcpy(p, key, klen);
    p += klen;
    if (kbraced) *p++ = '}';

    *p++ = ' ';

    if (vbraced) *p++ = '{';
    memcpy(p, vs, vlen);
    p += vlen;
    if (vbraced) *p++ = '}';

    lcl_ref_dec(val);
  }
  *p = '\0';

  value->str_repr = buf;
}

const char *lcl_value_to_string(lcl_value *value) {
  if (!value) return "";
  if (!value->str_repr) {
    switch (value->type) {
    case LCL_INT:
      lcl_reify_str_int(value);
      break;
    case LCL_FLOAT:
      lcl_reify_str_float(value);
      break;
    case LCL_STRING:
      break;
    case LCL_LIST:
      lcl_reify_str_list(value);
      break;
    case LCL_DICT:
      lcl_reify_str_dict(value);
      break;
    case LCL_OPAQUE: {
      const char *tag = value->as.opaque.type_tag;

      if (tag) {
        size_t len = strlen(tag) + 10;  /* "<opaque:>" + tag + null */
        value->str_repr = (char *)malloc(len);

        if (value->str_repr) {
          sprintf(value->str_repr, "<opaque:%s>", tag);
        }
      } else {
        value->str_repr = (char *)malloc(9);

        if (value->str_repr) {
          memcpy(value->str_repr, "<opaque>", 9);
        }
      }
    } break;

    default:
      /* PROC, CPROC, NS, CELL - not directly stringifiable */
      value->str_repr = (char *)malloc(4);
      if (!value->str_repr) return "";
      memcpy(value->str_repr, "<?>", 4);
      break;
    }
  }

  return value->str_repr ? value->str_repr : "";
}

lcl_value *lcl_value_new_string(const char *str) {
  lcl_value *value = (lcl_value *)calloc(1, sizeof(*value));

  if (!value) return NULL;

  if (str) {
    size_t n = strlen(str);
    value->str_repr = (char *)malloc(n + 1);

    if (!value->str_repr) {
      free(value);
      return NULL;
    }

    memcpy(value->str_repr, str, n + 1);
  }
  
  value->type = LCL_STRING;
  value->refc = 1;

  return value;  
}
