#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "lcl-values.h"

lcl_value *lcl_string_new(const char *str) {
  /* A STRING value's `str_repr` is its content; unlike lazy-reify
   * types (INT/FLOAT/LIST/DICT), there is no second source of truth
   * to reify from. So `str_repr == NULL` on a STRING is a poison
   * state — anyone stringifying it post-#15 sees a NULL return and
   * raises "out of memory". Treat `str == NULL` as the empty string
   * at construction time; callers like `io::getenv` of an unset
   * variable used to silently get "" out of this path and we want to
   * preserve that behavior. */
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));
  size_t n;

  if (!v) {
    return NULL;
  }

  n = str ? strlen(str) : 0;
  v->str_repr = (char *)malloc(n + 1);

  if (!v->str_repr) {
    free(v);
    return NULL;
  }

  if (n > 0) {
    memcpy(v->str_repr, str, n);
  }
  v->str_repr[n] = '\0';

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
  size_t len;
  int m = snprintf(buf, sizeof(buf), "%.17g", value->as.f);
  if (m < 0 || (size_t)m >= sizeof(buf)) {
    return;
  }

  /* Bugfix: `%g` honors LC_NUMERIC, which would break round-trip
   * parsing on non-'.' locales (spec §10 determinism). Force ASCII
   * '.'. */
  lcl_normalize_decimal_to_c(buf);
  len = strlen(buf);
  value->str_repr = (char *)malloc(len + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, len + 1);
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

    if (!s) {
      lcl_ref_dec(elem);
      free(b.buf);
      return;
    }

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
    enum elem_style ks;
    enum elem_style vst;
    int ok;

    if (!vs) {
      lcl_ref_dec(val);
      free(b.buf);
      return;
    }

    ks = choose_element_style(key);
    vst = choose_element_style(vs);

    if (!first) {
      if (!sbuf_putc(&b, ' ')) {
        lcl_ref_dec(val);
        free(b.buf);
        return;
      }
    }

    first = 0;

    ok = sbuf_append_styled(&b, key, ks) && sbuf_putc(&b, ' ') &&
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
    return NULL;
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
        return NULL;
      }

      memcpy(value->str_repr, "<?>", 4);
      break;
    }
  }

  return value->str_repr;
}

lcl_result lcl_value_to_cstring(lcl_interp *interp, lcl_value *value,
                                const char **out) {
  const char *s = lcl_value_to_string(value);

  if (!s) {
    LCL_ERR_MSG(interp, "out of memory");
    return LCL_ERROR;
  }

  *out = s;
  return LCL_OK;
}

lcl_value *lcl_value_new_string(const char *str) {
  return lcl_string_new(str);
}

const char *lcl_type_name(lcl_type t) {
  switch (t) {
  case LCL_STRING: return "string";
  case LCL_INT: return "int";
  case LCL_FLOAT: return "float";
  case LCL_LIST: return "list";
  case LCL_DICT: return "dict";
  case LCL_CELL: return "cell";
  case LCL_PROC: return "proc";
  case LCL_CPROC: return "cproc";
  case LCL_NAMESPACE: return "namespace";
  case LCL_OPAQUE: return "opaque";
  }

  return "unknown";
}

/* Type-aware representation: strings appear quoted, lists as (...),
 * dicts as #{...} (recursively), everything else in an
 * <angle-bracket> form that names the type. Distinguishes values
 * that stringify identically, e.g. the list (a b) vs the string
 * "a b". */
static int repr_append(sbuf *b, lcl_value *v) {
  if (!v) {
    return sbuf_append(b, "<null>", 6);
  }

  switch (v->type) {
  case LCL_STRING: {
    const char *s = lcl_value_to_string(v);

    if (!s) {
      return 0;
    }

    return sbuf_append_styled(b, s, ELEM_QUOTED);
  }

  case LCL_INT:
  case LCL_FLOAT: {
    const char *s = lcl_value_to_string(v);

    if (!s) {
      return 0;
    }

    return sbuf_append(b, s, strlen(s));
  }

  case LCL_LIST: {
    size_t len = lcl_list_len(v);
    size_t i;

    if (!sbuf_putc(b, '(')) {
      return 0;
    }

    for (i = 0; i < len; i++) {
      lcl_value *elem = NULL;
      int ok;

      if (i > 0 && !sbuf_putc(b, ' ')) {
        return 0;
      }

      if (lcl_list_get(v, i, &elem) != LCL_OK) {
        return 0;
      }

      ok = repr_append(b, elem);
      lcl_ref_dec(elem);

      if (!ok) {
        return 0;
      }
    }

    return sbuf_putc(b, ')');
  }

  case LCL_DICT: {
    lcl_dict_it it;
    const char *key;
    lcl_value *val;
    int first = 1;

    it.i = 0;

    if (!sbuf_append(b, "#{", 2)) {
      return 0;
    }

    while (lcl_dict_iter((const lcl_value **)&v, &it, &key, &val) == LCL_OK) {
      int ok;

      if (!first && !sbuf_putc(b, ' ')) {
        lcl_ref_dec(val);
        return 0;
      }

      first = 0;

      ok = sbuf_append_styled(b, key, ELEM_QUOTED) && sbuf_putc(b, ' ') &&
           repr_append(b, val);
      lcl_ref_dec(val);

      if (!ok) {
        return 0;
      }
    }

    return sbuf_putc(b, '}');
  }

  case LCL_CELL:
    return sbuf_append(b, "<cell ", 6) && repr_append(b, v->as.cell.inner) &&
           sbuf_putc(b, '>');

  case LCL_PROC: {
    lcl_proc *p = v->as.procedure.proc;
    const char *kind = p->is_macro ? "<macro " : "<proc ";

    if (!p->self_name) {
      return sbuf_append(b, "<lambda>", 8);
    }

    return sbuf_append(b, kind, strlen(kind)) &&
           sbuf_append(b, p->self_name, strlen(p->self_name)) &&
           sbuf_putc(b, '>');
  }

  case LCL_CPROC: {
    const char *name = v->as.c_proc.fn->name;

    return sbuf_append(b, "<cproc ", 7) &&
           sbuf_append(b, name ? name : "?", strlen(name ? name : "?")) &&
           sbuf_putc(b, '>');
  }

  case LCL_NAMESPACE: {
    const char *qname = v->as.namespace.qname;

    if (!qname) {
      return sbuf_append(b, "<namespace>", 11);
    }

    return sbuf_append(b, "<namespace ", 11) &&
           sbuf_append(b, qname, strlen(qname)) && sbuf_putc(b, '>');
  }

  case LCL_OPAQUE: {
    const char *s = lcl_value_to_string(v);

    if (!s) {
      return 0;
    }

    return sbuf_append(b, s, strlen(s));
  }
  }

  return 0;
}

char *lcl_value_repr(lcl_value *v) {
  sbuf b;

  b.buf = NULL;
  b.len = 0;
  b.cap = 0;

  if (!repr_append(&b, v)) {
    free(b.buf);
    return NULL;
  }

  return sbuf_finish(&b);
}
