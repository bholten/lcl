#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>

#define REGEX_TYPE "regex_t"
#define REGEX_NS "regex"

struct lcl_regex {
  regex_t re;
};

struct lcl_regex *lcl_regex_new(void) {
  struct lcl_regex *r = malloc(sizeof(*r));

  if (!r) {
    return NULL;
  }

  return r;
}

void lcl_regex_free(struct lcl_regex *re) {
  if (!re) {
    return;
  }

  regfree(&re->re);
  free(re);
}

/* Resolve a pattern argument: a compiled handle (regex::compile)
 * passes through; anything else is treated as a pattern string and
 * compiled into *scratch — caller must regfree it when *owned. */
static int get_regex(lcl_interp *interp, lcl_value *v, regex_t **re_out,
                     regex_t *scratch, int *owned) {
  struct lcl_regex *r = NULL;
  const char *pattern = NULL;

  *owned = 0;

  if (lcl_opaque_get(v, REGEX_TYPE, (void **)&r) == LCL_OK) {
    *re_out = &r->re;
    return 0;
  }

  if (lcl_value_to_cstring(interp, v, &pattern) != LCL_OK) {
    return -1;
  }

  if (regcomp(scratch, pattern, REG_EXTENDED) != 0) {
    lcl_set_error(interp, "invalid regex pattern");
    return -1;
  }

  *owned = 1;
  *re_out = scratch;

  return 0;
}

static lcl_value *substr_value(const char *s, size_t start, size_t end) {
  size_t n = end - start;
  char *buf = malloc(n + 1);
  lcl_value *v;

  if (!buf) {
    return NULL;
  }

  memcpy(buf, s + start, n);
  buf[n] = '\0';
  v = lcl_string_new(buf);
  free(buf);

  return v;
}

struct rbuf {
  char *s;
  size_t len;
  size_t cap;
};

static int rbuf_append(struct rbuf *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    size_t newcap = b->cap ? b->cap : 64;
    char *p;

    while (newcap < b->len + n + 1) {
      newcap *= 2;
    }

    p = realloc(b->s, newcap);

    if (!p) {
      return 0;
    }

    b->s = p;
    b->cap = newcap;
  }

  memcpy(b->s + b->len, s, n);
  b->len += n;
  b->s[b->len] = '\0';

  return 1;
}

/* regex::regcomp pattern - compile to a reusable handle */
static lcl_return_code c_regcomp(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  struct lcl_regex *re = NULL;
  const char *pattern = NULL;
  int errcode;

  if (argc < 1) {
    lcl_set_error(interp, "regex::compile requires a pattern");
    return LCL_RC_ERR;
  }

  re = lcl_regex_new();

  if (!re) {
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &pattern) != LCL_OK) {
    free(re);
    return LCL_RC_ERR;
  }

  errcode = regcomp(&re->re, pattern, REG_EXTENDED);

  if (errcode != 0) {
    free(re);
    lcl_set_error(interp, "invalid regex pattern");
    return LCL_RC_ERR;
  }

  *out = lcl_opaque_new(re, REGEX_TYPE, (lcl_finalizer)lcl_regex_free);

  return LCL_RC_OK;
}

/* regex::regexec regex string -> 1 if matched, 0 if not */
static lcl_return_code c_regexec(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  struct lcl_regex *re = NULL;
  int status;
  const char *str = NULL;

  if (argc < 2) {
    lcl_set_error(interp, "regex::regexec requires a regex and a string");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], REGEX_TYPE, (void **)&re) != LCL_OK) {
    lcl_set_error(interp, "regex::regexec: not a compiled regex");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  status = regexec(&re->re, str, (size_t)0, NULL, 0);

  *out = lcl_int_new(status == 0 ? 1 : 0);

  return LCL_RC_OK;
}

/* regex::match pattern|regex string -> 1 if matched, 0 if not */
static lcl_return_code c_match(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *str = NULL;
  int matched;

  if (argc < 2) {
    lcl_set_error(interp, "regex::match requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  matched = (regexec(re, str, 0, NULL, 0) == 0);

  if (owned) {
    regfree(&scratch);
  }

  *out = lcl_int_new(matched ? 1 : 0);

  return LCL_RC_OK;
}

/* regex::find pattern|regex string -> (start end) of the first
 * match (byte offsets, end exclusive), or the empty list */
static lcl_return_code c_find(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *str = NULL;
  regmatch_t m;
  lcl_value *result;

  if (argc < 2) {
    lcl_set_error(interp, "regex::find requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  if (regexec(re, str, 1, &m, 0) == 0) {
    lcl_value *v = lcl_int_new((long)m.rm_so);

    lcl_list_push(&result, v);
    lcl_ref_dec(v);
    v = lcl_int_new((long)m.rm_eo);
    lcl_list_push(&result, v);
    lcl_ref_dec(v);
  }

  if (owned) {
    regfree(&scratch);
  }

  *out = result;

  return LCL_RC_OK;
}

/* regex::captures pattern|regex string -> list of the first match's
 * texts: element 0 is the whole match, 1..n the capture groups
 * (unmatched optional groups become empty strings). Empty list when
 * the pattern does not match. */
static lcl_return_code c_captures(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *str = NULL;
  regmatch_t *m = NULL;
  size_t nmatch;
  lcl_value *result;

  if (argc < 2) {
    lcl_set_error(interp, "regex::captures requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  nmatch = re->re_nsub + 1;
  m = malloc(nmatch * sizeof(*m));

  if (!m) {
    if (owned) {
      regfree(&scratch);
    }

    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  if (regexec(re, str, nmatch, m, 0) == 0) {
    size_t i;

    for (i = 0; i < nmatch; i++) {
      lcl_value *v;

      if (m[i].rm_so == -1) {
        v = lcl_string_new("");
      } else {
        v = substr_value(str, (size_t)m[i].rm_so, (size_t)m[i].rm_eo);
      }

      if (!v) {
        break;
      }

      lcl_list_push(&result, v);
      lcl_ref_dec(v);
    }
  }

  free(m);

  if (owned) {
    regfree(&scratch);
  }

  *out = result;

  return LCL_RC_OK;
}

/* regex::search pattern|regex string ?from? -> offset pairs for the
 * first match at or after byte offset `from` (default 0): element 0
 * is the whole match, 1..n the capture groups, each a (start end)
 * pair of absolute byte offsets into string (end exclusive).
 * Unmatched groups are (-1 -1). Empty list when nothing matches.
 * Iteration contract: resume with from = end when end > start,
 * from = start + 1 on a zero-width match. */
static lcl_return_code c_search(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *str = NULL;
  regmatch_t *m = NULL;
  size_t nmatch;
  long from = 0;
  size_t len;
  lcl_value *result;

  if (argc < 2) {
    lcl_set_error(interp, "regex::search requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (argc > 2) {
    if (lcl_value_to_int(argv[2], &from) != LCL_OK) {
      lcl_set_error(interp, "regex::search: from must be an integer");
      return LCL_RC_ERR;
    }

    if (from < 0) {
      lcl_set_error(interp, "regex::search: from must be >= 0");
      return LCL_RC_ERR;
    }
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  nmatch = re->re_nsub + 1;
  m = malloc(nmatch * sizeof(*m));

  if (!m) {
    if (owned) {
      regfree(&scratch);
    }

    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  len = strlen(str);
  result = lcl_list_new();

  if ((size_t)from <= len &&
      regexec(re, str + from, nmatch, m, from > 0 ? REG_NOTBOL : 0) == 0) {
    size_t i;

    for (i = 0; i < nmatch; i++) {
      lcl_value *pair = lcl_list_new();
      lcl_value *v;
      long so = (long)m[i].rm_so;
      long eo = (long)m[i].rm_eo;

      if (so != -1) {
        so += from;
        eo += from;
      }

      v = lcl_int_new(so);
      lcl_list_push(&pair, v);
      lcl_ref_dec(v);
      v = lcl_int_new(eo);
      lcl_list_push(&pair, v);
      lcl_ref_dec(v);

      lcl_list_push(&result, pair);
      lcl_ref_dec(pair);
    }
  }

  free(m);

  if (owned) {
    regfree(&scratch);
  }

  *out = result;

  return LCL_RC_OK;
}

/* regex::find_all pattern|regex string -> list of every
 * (non-overlapping) whole-match text, left to right */
static lcl_return_code c_find_all(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *str = NULL;
  size_t pos = 0;
  size_t len;
  lcl_value *result;

  if (argc < 2) {
    lcl_set_error(interp, "regex::find_all requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  len = strlen(str);
  result = lcl_list_new();

  while (pos <= len) {
    regmatch_t m;
    int flags = pos > 0 ? REG_NOTBOL : 0;
    lcl_value *v;

    if (regexec(re, str + pos, 1, &m, flags) != 0) {
      break;
    }

    v = substr_value(str + pos, (size_t)m.rm_so, (size_t)m.rm_eo);

    if (!v) {
      break;
    }

    lcl_list_push(&result, v);
    lcl_ref_dec(v);

    /* an empty match must still advance to terminate */
    pos += (m.rm_eo > m.rm_so) ? (size_t)m.rm_eo : (size_t)m.rm_so + 1;
  }

  if (owned) {
    regfree(&scratch);
  }

  *out = result;

  return LCL_RC_OK;
}

/* Append the replacement template, expanding \0..\9 to the match's
 * group texts and \\ to a literal backslash. */
static int append_replacement(struct rbuf *b, const char *tmpl,
                              const char *base, const regmatch_t *m,
                              size_t nmatch) {
  const char *p;

  for (p = tmpl; *p; p++) {
    if (*p == '\\' && p[1]) {
      char c = p[1];

      if (c >= '0' && c <= '9') {
        size_t g = (size_t)(c - '0');

        if (g < nmatch && m[g].rm_so != -1) {
          if (!rbuf_append(b, base + m[g].rm_so,
                           (size_t)(m[g].rm_eo - m[g].rm_so))) {
            return 0;
          }
        }

        p++;
        continue;
      }

      if (c == '\\') {
        if (!rbuf_append(b, "\\", 1)) {
          return 0;
        }

        p++;
        continue;
      }
    }

    if (!rbuf_append(b, p, 1)) {
      return 0;
    }
  }

  return 1;
}

/* regex::replace pattern|regex replacement string -> string with
 * every match replaced. The replacement may reference group texts
 * with \0 (whole match) .. \9; \\ is a literal backslash. */
static lcl_return_code c_replace(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *repl = NULL;
  const char *str = NULL;
  regmatch_t *m = NULL;
  size_t nmatch;
  size_t pos = 0;
  size_t len;
  struct rbuf b;
  int ok = 1;

  if (argc < 3) {
    lcl_set_error(interp,
                  "regex::replace requires pattern, replacement, and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &repl) != LCL_OK ||
      lcl_value_to_cstring(interp, argv[2], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  nmatch = re->re_nsub + 1;
  m = malloc(nmatch * sizeof(*m));

  b.s = NULL;
  b.len = 0;
  b.cap = 0;

  if (!m) {
    ok = 0;
  }

  len = strlen(str);

  while (ok && pos <= len) {
    int flags = pos > 0 ? REG_NOTBOL : 0;

    if (regexec(re, str + pos, nmatch, m, flags) != 0) {
      break;
    }

    ok = rbuf_append(&b, str + pos, (size_t)m[0].rm_so) &&
         append_replacement(&b, repl, str + pos, m, nmatch);

    if (m[0].rm_eo > m[0].rm_so) {
      pos += (size_t)m[0].rm_eo;
    } else {
      /* empty match: emit the next byte and advance to terminate */
      if (ok && (size_t)m[0].rm_so < len - pos) {
        ok = rbuf_append(&b, str + pos + m[0].rm_so, 1);
      }

      pos += (size_t)m[0].rm_so + 1;
    }
  }

  if (ok && pos <= len) {
    ok = rbuf_append(&b, str + pos, len - pos);
  }

  free(m);

  if (owned) {
    regfree(&scratch);
  }

  if (!ok) {
    free(b.s);
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(b.s ? b.s : "");
  free(b.s);

  return LCL_RC_OK;
}

/* regex::split pattern|regex string -> list of the substrings
 * between matches (a match at the start or end contributes an empty
 * leading/trailing element; empty matches are skipped) */
static lcl_return_code c_split(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  regex_t scratch;
  regex_t *re = NULL;
  int owned = 0;
  const char *str = NULL;
  size_t pos = 0;
  size_t len;
  lcl_value *result;

  if (argc < 2) {
    lcl_set_error(interp, "regex::split requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (get_regex(interp, argv[0], &re, &scratch, &owned) != 0) {
    return LCL_RC_ERR;
  }

  len = strlen(str);
  result = lcl_list_new();

  while (pos <= len) {
    regmatch_t m;
    int flags = pos > 0 ? REG_NOTBOL : 0;
    lcl_value *v;

    if (regexec(re, str + pos, 1, &m, flags) != 0) {
      break;
    }

    if (m.rm_eo == m.rm_so) {
      /* empty match: no split point here; scan past it */
      pos += (size_t)m.rm_so + 1;
      continue;
    }

    v = substr_value(str + pos, 0, (size_t)m.rm_so);

    if (!v) {
      break;
    }

    lcl_list_push(&result, v);
    lcl_ref_dec(v);
    pos += (size_t)m.rm_eo;
  }

  {
    lcl_value *v = substr_value(str, pos, len);

    if (v) {
      lcl_list_push(&result, v);
      lcl_ref_dec(v);
    }
  }

  if (owned) {
    regfree(&scratch);
  }

  *out = result;

  return LCL_RC_OK;
}

/* Note: regfree is handled automatically by the opaque value finalizer.
   No explicit free command is needed - regex values are freed when
   they go out of scope. */

/* Note: regerror is not exposed since errors are reported directly
   via the interpreter's error message when regcomp fails. */

void lcl_register_regex(lcl_interp *interp) {
  lcl_value *regex_ns = lcl_ns_new(REGEX_NS);
  lcl_define_take(interp, REGEX_NS, regex_ns);

  lcl_ns_def_take(regex_ns, "compile",
                  lcl_c_proc_new("regex::compile", c_regcomp));
  lcl_ns_def_take(regex_ns, "regcomp",
                  lcl_c_proc_new("regex::regcomp", c_regcomp));
  lcl_ns_def_take(regex_ns, "regexec",
                  lcl_c_proc_new("regex::regexec", c_regexec));
  lcl_ns_def_take(regex_ns, "match", lcl_c_proc_new("regex::match", c_match));
  lcl_ns_def_take(regex_ns, "find", lcl_c_proc_new("regex::find", c_find));
  lcl_ns_def_take(regex_ns, "captures",
                  lcl_c_proc_new("regex::captures", c_captures));
  lcl_ns_def_take(regex_ns, "search",
                  lcl_c_proc_new("regex::search", c_search));
  lcl_ns_def_take(regex_ns, "find_all",
                  lcl_c_proc_new("regex::find_all", c_find_all));
  lcl_ns_def_take(regex_ns, "replace",
                  lcl_c_proc_new("regex::replace", c_replace));
  lcl_ns_def_take(regex_ns, "split", lcl_c_proc_new("regex::split", c_split));
}
