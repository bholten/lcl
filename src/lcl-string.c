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

static size_t quoted_len(const char *s) {
  size_t n = 2;
  const char *p;

  for (p = s; *p; p++) {
    char c = *p;

    if (c == '\\' || c == '$' || c == '[' || c == '"') {
      n += 2;
    } else {
      n += 1;
    }
  }

  return n;
}

static char *write_quoted(char *dst, const char *s) {
  const char *p;
  *dst++ = '"';

  for (p = s; *p; p++) {
    char c = *p;

    if (c == '\\' || c == '$' || c == '[' || c == '"') {
      *dst++ = '\\';
    }

    *dst++ = c;
  }

  *dst++ = '"';
  return dst;
}

static void lcl_reify_str_list(lcl_value *value) {
  size_t len = lcl_list_len(value);
  size_t total = 0;
  size_t i;
  char *buf;
  char *p;

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *s;
    enum elem_style style;

    if (lcl_list_get(value, i, &elem) != LCL_OK) {
      continue;
    }

    s = lcl_value_to_string(elem);
    style = choose_element_style(s);

    switch (style) {
    case ELEM_BARE: total += strlen(s); break;
    case ELEM_BRACED: total += strlen(s) + 2; break;
    case ELEM_QUOTED: total += quoted_len(s); break;
    }

    lcl_ref_dec(elem);
  }

  total += len;

  buf = (char *)malloc(total + 1);

  if (!buf) {
    return;
  }

  p = buf;

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *s;
    size_t slen;
    enum elem_style style;

    if (i > 0) {
      *p++ = ' ';
    }

    if (lcl_list_get(value, i, &elem) != LCL_OK) {
      continue;
    }

    s = lcl_value_to_string(elem);
    slen = strlen(s);
    style = choose_element_style(s);

    switch (style) {
    case ELEM_BARE:
      memcpy(p, s, slen);
      p += slen;
      break;
    case ELEM_BRACED:
      *p++ = '{';
      memcpy(p, s, slen);
      p += slen;
      *p++ = '}';
      break;
    case ELEM_QUOTED: p = write_quoted(p, s); break;
    }

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
  char *buf;
  char *p;
  int first = 1;

  while (lcl_dict_iter((const lcl_value **)&value, &it, &key, &val) == LCL_OK) {
    const char *vs = lcl_value_to_string(val);
    enum elem_style ks = choose_element_style(key);
    enum elem_style vst = choose_element_style(vs);

    switch (ks) {
    case ELEM_BARE: total += strlen(key); break;
    case ELEM_BRACED: total += strlen(key) + 2; break;
    case ELEM_QUOTED: total += quoted_len(key); break;
    }

    switch (vst) {
    case ELEM_BARE: total += strlen(vs); break;
    case ELEM_BRACED: total += strlen(vs) + 2; break;
    case ELEM_QUOTED: total += quoted_len(vs); break;
    }

    total += 2;
    lcl_ref_dec(val);
  }

  buf = (char *)malloc(total + 1);

  if (!buf) {
    return;
  }

  p = buf;
  it.i = 0;

  while (lcl_dict_iter((const lcl_value **)&value, &it, &key, &val) == LCL_OK) {
    const char *vs = lcl_value_to_string(val);
    size_t klen = strlen(key);
    size_t vlen = strlen(vs);
    enum elem_style ks = choose_element_style(key);
    enum elem_style vst = choose_element_style(vs);

    if (!first) {
      *p++ = ' ';
    }

    first = 0;

    switch (ks) {
    case ELEM_BARE:
      memcpy(p, key, klen);
      p += klen;
      break;
    case ELEM_BRACED:
      *p++ = '{';
      memcpy(p, key, klen);
      p += klen;
      *p++ = '}';
      break;
    case ELEM_QUOTED: p = write_quoted(p, key); break;
    }

    *p++ = ' ';

    switch (vst) {
    case ELEM_BARE:
      memcpy(p, vs, vlen);
      p += vlen;
      break;
    case ELEM_BRACED:
      *p++ = '{';
      memcpy(p, vs, vlen);
      p += vlen;
      *p++ = '}';
      break;
    case ELEM_QUOTED: p = write_quoted(p, vs); break;
    }

    lcl_ref_dec(val);
  }

  *p = '\0';

  value->str_repr = buf;
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
