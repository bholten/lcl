#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 600

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lcl.h>

#define EXPECT_NS "Expect"
#define PATTERN_TAG "expect_pattern"
#define SESSION_TAG "expect_session"

#define DEFAULT_TIMEOUT_MS 10000
#define DEFAULT_LIMIT ((size_t)1024 * 1024)
#define DEFAULT_GRACE_MS 1000
#define DEFAULT_QUIET_MS 100
#define SLICE_MS 50
#define READ_CHUNK 65536
#define TAIL_BYTES 80
#define MAX_GROUPS 10

typedef enum { PAT_LITERAL, PAT_REGEX, PAT_TIMEOUT, PAT_EOF } pattern_kind;

typedef struct {
  pattern_kind kind;
  char *text;
  size_t text_len;
  int nocase;
  int compiled;
  regex_t re;
} expect_pattern;

typedef struct {
  lcl_value *handle;
  char *buf;
  size_t len;
  size_t cap;
  size_t limit;
  int timeout_ms;
  int eof;
  int closed;
  lcl_value *last;
} expect_session;

typedef struct {
  const expect_pattern *p;
  expect_pattern lit;
  size_t clause;
} pat_entry;

typedef struct {
  pat_entry *entries;
  size_t n;
  size_t nclauses;
  const char **bodies;
} pat_table;

typedef struct {
  size_t entry;
  int timed_out;
  int eof;
  regmatch_t m[MAX_GROUPS];
  size_t ngroups;
} wait_outcome;

typedef struct {
  char *s;
  size_t len;
  size_t cap;
} strbuf;

static int sb_put(strbuf *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap : 128;
    char *ns;

    while (cap < b->len + n + 1) {
      cap *= 2;
    }

    ns = (char *)realloc(b->s, cap);

    if (!ns) {
      return 0;
    }

    b->s = ns;
    b->cap = cap;
  }

  memcpy(b->s + b->len, s, n);
  b->len += n;
  b->s[b->len] = '\0';

  return 1;
}

static int sb_puts(strbuf *b, const char *s) {
  return sb_put(b, s, strlen(s));
}

static int sb_putc(strbuf *b, char c) {
  return sb_put(b, &c, 1);
}

static int sb_put_int(strbuf *b, lcl_int n) {
  char tmp[LCL_INT_STRLEN];
  size_t len = lcl_int_format(tmp, n);

  return sb_put(b, tmp, len);
}

static int sb_put_escaped(strbuf *b, const char *s, size_t n) {
  size_t i;

  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    int ok;

    switch (c) {
    case '\n': ok = sb_puts(b, "\\n"); break;
    case '\r': ok = sb_puts(b, "\\r"); break;
    case '\t': ok = sb_puts(b, "\\t"); break;
    case '"': ok = sb_puts(b, "\\\""); break;
    case '\\': ok = sb_puts(b, "\\\\"); break;
    default:
      if (c < 0x20 || c == 0x7f) {
        char hex[8];
        sprintf(hex, "\\x%02x", c);
        ok = sb_puts(b, hex);
      } else {
        ok = sb_putc(b, (char)c);
      }

      break;
    }

    if (!ok) {
      return 0;
    }
  }

  return 1;
}

static int sb_describe_pattern(strbuf *b, const expect_pattern *p) {
  switch (p->kind) {
  case PAT_LITERAL:
    return sb_putc(b, '"') && sb_put_escaped(b, p->text, p->text_len) &&
           sb_putc(b, '"');
  case PAT_REGEX:
    return sb_putc(b, '/') && sb_put_escaped(b, p->text, p->text_len) &&
           sb_putc(b, '/');
  case PAT_TIMEOUT: return sb_puts(b, "timeout");
  case PAT_EOF: return sb_puts(b, "eof");
  }

  return sb_puts(b, "?");
}

static void sb_free(strbuf *b) {
  free(b->s);
  b->s = NULL;
  b->len = 0;
  b->cap = 0;
}

static lcl_i64 now_ms(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }

  return (lcl_i64)ts.tv_sec * 1000 + (lcl_i64)(ts.tv_nsec / 1000000L);
}

static lcl_value *string_from_range(const char *s, size_t n) {
  char *tmp = (char *)malloc(n + 1);
  lcl_value *v;

  if (!tmp) {
    return NULL;
  }

  memcpy(tmp, s, n);
  tmp[n] = '\0';
  v = lcl_string_new(tmp);
  free(tmp);
  return v;
}

static lcl_return_code oom(lcl_interp *interp) {
  lcl_set_error(interp, "out of memory");
  return LCL_RC_ERR;
}

/* TODO I think this is already defined somewhere? */
static void dict_put_take(lcl_value **dict, const char *key, lcl_value *v) {
  if (v) {
    lcl_dict_put(dict, key, v);
    lcl_ref_dec(v);
  }
}

static lcl_return_code opt_int(lcl_interp *interp, const char *who,
                               lcl_value *opts, const char *key, lcl_int def,
                               lcl_int *out) {
  lcl_value *v;

  *out = def;

  if (!opts || lcl_value_type_of(opts) != LCL_DICT) {
    return LCL_RC_OK;
  }

  v = lcl_dict_peek(opts, key);

  if (!v) {
    return LCL_RC_OK;
  }

  if (lcl_value_to_int(v, out) != LCL_OK) {
    strbuf b = {NULL, 0, 0};

    if (sb_puts(&b, who) && sb_puts(&b, ": option ") && sb_puts(&b, key) &&
        sb_puts(&b, " must be an integer")) {
      lcl_set_error(interp, b.s);
    } else {
      lcl_set_error(interp, "option must be an integer");
    }

    sb_free(&b);
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

static lcl_return_code check_opts(lcl_interp *interp, const char *who, int argc,
                                  lcl_value **argv, int idx, lcl_value **opts) {
  *opts = NULL;

  if (argc > idx) {
    if (lcl_value_type_of(argv[idx]) != LCL_DICT) {
      strbuf b = {NULL, 0, 0};

      if (sb_puts(&b, who) && sb_puts(&b, ": options must be a dict")) {
        lcl_set_error(interp, b.s);
      }

      sb_free(&b);
      return LCL_RC_ERR;
    }

    *opts = argv[idx];
  }

  return LCL_RC_OK;
}

static lcl_return_code fail(lcl_interp *interp, const char *who,
                            const char *msg) {
  strbuf b = {NULL, 0, 0};

  if (sb_puts(&b, who) && sb_puts(&b, ": ") && sb_puts(&b, msg)) {
    lcl_set_error(interp, b.s);
  } else {
    lcl_set_error(interp, msg);
  }

  sb_free(&b);
  return LCL_RC_ERR;
}

static lcl_return_code call_process(lcl_interp *interp, const char *name,
                                    int argc, lcl_value **argv,
                                    lcl_value **out) {
  char full[64];
  lcl_value *proc = NULL;
  lcl_return_code rc;

  sprintf(full, "::Process::%s", name);

  if (lcl_get(interp, full, &proc) != LCL_OK) {
    lcl_set_error(interp, "Expect: the Process:: package is not available");
    return LCL_RC_ERR;
  }

  rc = lcl_call_proc(interp, proc, argc, argv, out);
  lcl_ref_dec(proc);
  return rc;
}

static lcl_return_code call_process_opts(lcl_interp *interp, const char *name,
                                         lcl_value *handle, lcl_value *opts,
                                         lcl_value **out) {
  lcl_value *args[2];

  args[0] = handle;
  args[1] = opts;
  return call_process(interp, name, opts ? 2 : 1, args, out);
}

static void pattern_finalizer(void *ptr) {
  expect_pattern *p = (expect_pattern *)ptr;

  if (!p) {
    return;
  }

  if (p->compiled) {
    regfree(&p->re);
  }

  free(p->text);
  free(p);
}

static expect_pattern *get_pattern(lcl_value *v) {
  void *ptr = NULL;

  if (!v || lcl_opaque_get(v, PATTERN_TAG, &ptr) != LCL_OK) {
    return NULL;
  }

  return (expect_pattern *)ptr;
}

static lcl_return_code new_sentinel(lcl_interp *interp, pattern_kind kind,
                                    lcl_value **out) {
  expect_pattern *p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return oom(interp);
  }

  p->kind = kind;
  *out = lcl_opaque_new(p, PATTERN_TAG, pattern_finalizer);

  if (!*out) {
    pattern_finalizer(p);
    return oom(interp);
  }

  return LCL_RC_OK;
}

/* Expect::literal text ?opts? -- opts: nocase */
static lcl_return_code c_literal(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  const char *who = "Expect::literal";
  const char *text;
  lcl_value *opts;
  lcl_int nocase;
  expect_pattern *p;

  if (argc < 1 || argc > 2) {
    return fail(interp, who, "expected text and optional options");
  }

  if (lcl_value_to_cstring(interp, argv[0], &text) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (*text == '\0') {
    return fail(interp, who, "pattern must not be empty");
  }

  if (check_opts(interp, who, argc, argv, 1, &opts) != LCL_RC_OK ||
      opt_int(interp, who, opts, "nocase", 0, &nocase) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return oom(interp);
  }

  p->kind = PAT_LITERAL;
  p->text = strdup(text);
  p->text_len = strlen(text);
  p->nocase = nocase != 0;

  if (!p->text) {
    pattern_finalizer(p);
    return oom(interp);
  }

  *out = lcl_opaque_new(p, PATTERN_TAG, pattern_finalizer);

  if (!*out) {
    pattern_finalizer(p);
    return oom(interp);
  }

  return LCL_RC_OK;
}

/* Expect::regex source ?opts? -- opts: nocase, newline */
static lcl_return_code c_regex(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *who = "Expect::regex";
  const char *text;
  lcl_value *opts;
  lcl_int nocase;
  lcl_int newline;
  expect_pattern *p;
  int flags = REG_EXTENDED;
  int ret;

  if (argc < 1 || argc > 2) {
    return fail(interp, who, "expected a regex and optional options");
  }

  if (lcl_value_to_cstring(interp, argv[0], &text) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (*text == '\0') {
    return fail(interp, who, "pattern must not be empty");
  }

  if (check_opts(interp, who, argc, argv, 1, &opts) != LCL_RC_OK ||
      opt_int(interp, who, opts, "nocase", 0, &nocase) != LCL_RC_OK ||
      opt_int(interp, who, opts, "newline", 0, &newline) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (nocase) {
    flags |= REG_ICASE;
  }

  if (newline) {
    flags |= REG_NEWLINE;
  }

  p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return oom(interp);
  }

  p->kind = PAT_REGEX;
  p->text = strdup(text);
  p->text_len = strlen(text);
  p->nocase = nocase != 0;

  if (!p->text) {
    pattern_finalizer(p);
    return oom(interp);
  }

  ret = regcomp(&p->re, text, flags);

  if (ret != 0) {
    char errbuf[200];
    strbuf b = {NULL, 0, 0};

    regerror(ret, &p->re, errbuf, sizeof(errbuf));

    if (sb_puts(&b, who) && sb_puts(&b, ": invalid regex /") &&
        sb_put_escaped(&b, text, strlen(text)) && sb_puts(&b, "/: ") &&
        sb_puts(&b, errbuf)) {
      lcl_set_error(interp, b.s);
    } else {
      lcl_set_error(interp, errbuf);
    }

    sb_free(&b);
    pattern_finalizer(p);
    return LCL_RC_ERR;
  }

  p->compiled = 1;
  *out = lcl_opaque_new(p, PATTERN_TAG, pattern_finalizer);

  if (!*out) {
    pattern_finalizer(p);
    return oom(interp);
  }

  return LCL_RC_OK;
}

static lcl_return_code c_timeout(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  (void)argv;

  if (argc != 0) {
    return fail(interp, "Expect::timeout", "takes no arguments");
  }

  return new_sentinel(interp, PAT_TIMEOUT, out);
}

static lcl_return_code c_eof(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  (void)argv;

  if (argc != 0) {
    return fail(interp, "Expect::eof", "takes no arguments");
  }

  return new_sentinel(interp, PAT_EOF, out);
}

static lcl_return_code c_is_pattern(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  if (argc != 1) {
    return fail(interp, "Expect::pattern?", "expected one argument");
  }

  *out = lcl_int_new(get_pattern(argv[0]) != NULL);
  return LCL_RC_OK;
}

static lcl_return_code c_pattern_kind(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  expect_pattern *p;

  if (argc != 1 || !(p = get_pattern(argv[0]))) {
    return fail(interp, "Expect::pattern-kind", "expected a pattern object");
  }

  switch (p->kind) {
  case PAT_LITERAL: *out = lcl_string_new("literal"); break;
  case PAT_REGEX: *out = lcl_string_new("regex"); break;
  case PAT_TIMEOUT: *out = lcl_string_new("timeout"); break;
  case PAT_EOF: *out = lcl_string_new("eof"); break;
  }

  return LCL_RC_OK;
}

static const char *find_nocase(const char *hay, const char *needle,
                               size_t needle_len) {
  size_t i;

  for (i = 0; hay[i]; i++) {
    size_t j;

    for (j = 0; j < needle_len && hay[i + j]; j++) {
      if (tolower((unsigned char)hay[i + j]) !=
          tolower((unsigned char)needle[j])) {
        break;
      }
    }

    if (j == needle_len) {
      return hay + i;
    }
  }

  return NULL;
}

static int pattern_find(const expect_pattern *p, const char *buf, regmatch_t *m,
                        size_t *ngroups) {
  if (p->kind == PAT_LITERAL) {
    const char *found = p->nocase ? find_nocase(buf, p->text, p->text_len)
                                  : strstr(buf, p->text);

    if (!found) {
      return 0;
    }

    m[0].rm_so = (regoff_t)(found - buf);
    m[0].rm_eo = (regoff_t)(m[0].rm_so + (regoff_t)p->text_len);
    *ngroups = 1;
    return 1;
  }

  if (p->kind == PAT_REGEX) {
    if (regexec(&p->re, buf, MAX_GROUPS, m, 0) != 0) {
      return 0;
    }

    *ngroups = p->re.re_nsub + 1;

    if (*ngroups > MAX_GROUPS) {
      *ngroups = MAX_GROUPS;
    }

    return 1;
  }

  return 0;
}

static void table_free(pat_table *t) {
  free(t->entries);
  free(t->bodies);
  t->entries = NULL;
  t->bodies = NULL;
  t->n = 0;
  t->nclauses = 0;
}

static lcl_return_code table_add(lcl_interp *interp, const char *who,
                                 pat_table *t, lcl_value *v, size_t clause) {
  pat_entry *e = &t->entries[t->n];
  const expect_pattern *p = get_pattern(v);

  memset(e, 0, sizeof(*e));
  e->clause = clause;

  if (p) {
    e->p = p;
  } else {
    const char *text;

    if (lcl_value_type_of(v) == LCL_LIST) {
      return fail(interp, who, "a pattern must be a string or pattern object");
    }

    if (lcl_value_to_cstring(interp, v, &text) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (*text == '\0') {
      return fail(interp, who, "pattern must not be empty");
    }

    e->lit.kind = PAT_LITERAL;
    e->lit.text = (char *)text;
    e->lit.text_len = strlen(text);
    e->p = &e->lit;
  }

  t->n++;
  return LCL_RC_OK;
}

static lcl_return_code table_from_patterns(lcl_interp *interp, const char *who,
                                           lcl_value *patterns, pat_table *t) {
  size_t n =
      lcl_value_type_of(patterns) == LCL_LIST ? lcl_list_len(patterns) : 1;
  size_t i;

  memset(t, 0, sizeof(*t));

  if (n == 0) {
    return fail(interp, who, "pattern list must not be empty");
  }

  t->entries = (pat_entry *)calloc(n, sizeof(pat_entry));

  if (!t->entries) {
    return oom(interp);
  }

  if (lcl_value_type_of(patterns) != LCL_LIST) {
    if (table_add(interp, who, t, patterns, 0) != LCL_RC_OK) {
      table_free(t);
      return LCL_RC_ERR;
    }
  } else {
    for (i = 0; i < n; i++) {
      if (table_add(interp, who, t, lcl_list_peek(patterns, i), i) !=
          LCL_RC_OK) {
        table_free(t);
        return LCL_RC_ERR;
      }
    }
  }

  t->nclauses = n;
  return LCL_RC_OK;
}

static lcl_return_code table_from_clauses(lcl_interp *interp, const char *who,
                                          lcl_value *clauses, pat_table *t) {
  size_t len;
  size_t nclauses;
  size_t total = 0;
  size_t i;

  memset(t, 0, sizeof(*t));

  if (lcl_value_type_of(clauses) != LCL_LIST) {
    return fail(interp, who, "clauses must be a list of pattern/body pairs");
  }

  len = lcl_list_len(clauses);

  if (len == 0 || len % 2 != 0) {
    return fail(interp, who, "clauses must be pattern/body pairs");
  }

  nclauses = len / 2;

  for (i = 0; i < nclauses; i++) {
    lcl_value *pv = lcl_list_peek(clauses, 2 * i);

    total += lcl_value_type_of(pv) == LCL_LIST ? lcl_list_len(pv) : 1;
  }

  if (total == 0) {
    return fail(interp, who, "a clause's pattern list must not be empty");
  }

  t->entries = (pat_entry *)calloc(total, sizeof(pat_entry));
  t->bodies = (const char **)calloc(nclauses, sizeof(const char *));

  if (!t->entries || !t->bodies) {
    table_free(t);
    return oom(interp);
  }

  for (i = 0; i < nclauses; i++) {
    lcl_value *pv = lcl_list_peek(clauses, 2 * i);
    lcl_value *bv = lcl_list_peek(clauses, 2 * i + 1);

    if (lcl_value_get_string(bv, &t->bodies[i]) != LCL_OK) {
      table_free(t);
      return fail(interp, who, "a clause body must be a braced script");
    }

    if (lcl_value_type_of(pv) == LCL_LIST) {
      size_t alt;
      size_t nalt = lcl_list_len(pv);

      if (nalt == 0) {
        table_free(t);
        return fail(interp, who, "a clause's pattern list must not be empty");
      }

      for (alt = 0; alt < nalt; alt++) {
        if (table_add(interp, who, t, lcl_list_peek(pv, alt), i) != LCL_RC_OK) {
          table_free(t);
          return LCL_RC_ERR;
        }
      }
    } else if (table_add(interp, who, t, pv, i) != LCL_RC_OK) {
      table_free(t);
      return LCL_RC_ERR;
    }
  }

  t->nclauses = nclauses;

  return LCL_RC_OK;
}

static int sb_describe_table(strbuf *b, const pat_table *t) {
  size_t i;
  int first = 1;

  for (i = 0; i < t->n; i++) {
    const expect_pattern *p = t->entries[i].p;

    if (p->kind == PAT_TIMEOUT || p->kind == PAT_EOF) {
      continue;
    }

    if (!first && !sb_puts(b, " | ")) {
      return 0;
    }

    if (!sb_describe_pattern(b, p)) {
      return 0;
    }

    first = 0;
  }

  if (first) {
    return sb_puts(b, "(no text patterns)");
  }

  return 1;
}

static void session_finalizer(void *ptr) {
  expect_session *s = (expect_session *)ptr;

  if (!s) {
    return;
  }

  if (s->handle) {
    lcl_ref_dec(s->handle);
  }

  if (s->last) {
    lcl_ref_dec(s->last);
  }

  free(s->buf);
  free(s);
}

static expect_session *get_session(lcl_interp *interp, const char *who,
                                   int argc, lcl_value **argv) {
  void *ptr = NULL;

  if (argc < 1 || lcl_opaque_get(argv[0], SESSION_TAG, &ptr) != LCL_OK) {
    fail(interp, who, "expected a session (from Expect::spawn)");
    return NULL;
  }

  return (expect_session *)ptr;
}

static int buf_reserve(expect_session *s, size_t extra) {
  size_t need = s->len + extra + 1;
  size_t cap;
  char *nb;

  if (need <= s->cap) {
    return 1;
  }

  cap = s->cap ? s->cap : 4096;

  while (cap < need) {
    cap *= 2;
  }

  nb = (char *)realloc(s->buf, cap);

  if (!nb) {
    return 0;
  }

  s->buf = nb;
  s->cap = cap;
  return 1;
}

static int buf_append(expect_session *s, const char *data, size_t n) {
  if (n >= s->limit) {
    data += n - s->limit;
    n = s->limit;
    s->len = 0;
  } else if (s->len + n > s->limit) {
    size_t drop = s->len + n - s->limit;

    memmove(s->buf, s->buf + drop, s->len - drop);
    s->len -= drop;
  }

  if (!buf_reserve(s, n)) {
    return 0;
  }

  memcpy(s->buf + s->len, data, n);
  s->len += n;
  s->buf[s->len] = '\0';
  return 1;
}

static void buf_consume(expect_session *s, size_t n) {
  if (n >= s->len) {
    s->len = 0;
  } else {
    memmove(s->buf, s->buf + n, s->len - n);
    s->len -= n;
  }

  if (s->buf) {
    s->buf[s->len] = '\0';
  }
}

static const char *buf_text(const expect_session *s) {
  return s->buf ? s->buf : "";
}

static lcl_return_code session_fill(lcl_interp *interp, expect_session *s,
                                    lcl_int wait_ms) {
  lcl_value *opts;
  lcl_value *res = NULL;
  const char *chunk;
  lcl_return_code rc;

  if (s->eof) {
    return LCL_RC_OK;
  }

  opts = lcl_dict_new();

  if (!opts) {
    return oom(interp);
  }

  dict_put_take(&opts, "timeout", lcl_int_new(wait_ms));
  dict_put_take(&opts, "n", lcl_int_new(READ_CHUNK));
  rc = call_process_opts(interp, "read", s->handle, opts, &res);
  lcl_ref_dec(opts);

  if (rc != LCL_RC_OK) {
    return rc;
  }

  if (lcl_value_to_cstring(interp, res, &chunk) != LCL_OK) {
    lcl_ref_dec(res);
    return LCL_RC_ERR;
  }

  if (*chunk) {
    if (!buf_append(s, chunk, strlen(chunk))) {
      lcl_ref_dec(res);
      return oom(interp);
    }

    lcl_ref_dec(res);
    return LCL_RC_OK;
  }

  lcl_ref_dec(res);

  rc = call_process_opts(interp, "eof?", s->handle, NULL, &res);

  if (rc != LCL_RC_OK) {
    return rc;
  }

  {
    lcl_int at_eof = 0;

    lcl_value_to_int(res, &at_eof);
    lcl_ref_dec(res);

    if (at_eof) {
      s->eof = 1;
    }
  }

  return LCL_RC_OK;
}

static lcl_value *make_result(const expect_session *s, const pat_table *t,
                              const wait_outcome *o) {
  lcl_value *d = lcl_dict_new();
  lcl_value *groups = lcl_list_new();
  const char *buf = buf_text(s);
  size_t clause = t->entries[o->entry].clause;

  if (!d || !groups) {
    if (d) {
      lcl_ref_dec(d);
    }

    if (groups) {
      lcl_ref_dec(groups);
    }

    return NULL;
  }

  dict_put_take(&d, "index", lcl_int_new((lcl_int)clause));

  if (o->timed_out || o->eof) {
    dict_put_take(&d, "before", lcl_string_new(""));
    dict_put_take(&d, "match", lcl_string_new(""));
    dict_put_take(&d, "buffer", lcl_string_new(""));
  } else {
    size_t so = (size_t)o->m[0].rm_so;
    size_t eo = (size_t)o->m[0].rm_eo;
    size_t g;

    dict_put_take(&d, "before", string_from_range(buf, so));
    dict_put_take(&d, "match", string_from_range(buf + so, eo - so));
    dict_put_take(&d, "buffer", string_from_range(buf, eo));

    if (t->entries[o->entry].p->kind == PAT_REGEX) {
      for (g = 0; g < o->ngroups; g++) {
        lcl_value *gv;

        if (o->m[g].rm_so < 0) {
          gv = lcl_string_new("");
        } else {
          gv = string_from_range(buf + o->m[g].rm_so,
                                 (size_t)(o->m[g].rm_eo - o->m[g].rm_so));
        }

        if (gv) {
          lcl_list_push(&groups, gv);
          lcl_ref_dec(gv);
        }
      }
    }
  }

  lcl_dict_put(&d, "groups", groups);
  lcl_ref_dec(groups);
  dict_put_take(&d, "timeout", lcl_int_new(o->timed_out));
  dict_put_take(&d, "eof", lcl_int_new(o->eof));
  return d;
}

static lcl_return_code wait_failed(lcl_interp *interp, const char *who,
                                   const expect_session *s, const pat_table *t,
                                   int timed_out, lcl_int timeout_ms) {
  strbuf b = {NULL, 0, 0};
  const char *buf = buf_text(s);
  size_t tail = s->len < TAIL_BYTES ? s->len : TAIL_BYTES;
  int ok;

  ok = sb_puts(&b, who);

  if (timed_out) {
    ok = ok && sb_puts(&b, ": timed out after ") &&
         sb_put_int(&b, timeout_ms) && sb_puts(&b, " ms waiting for ");
  } else {
    ok = ok && sb_puts(&b, ": child closed its output before matching ");
  }

  ok = ok && sb_describe_table(&b, t);

  if (s->len == 0) {
    ok = ok && sb_puts(&b, " (buffer empty)");
  } else {
    ok = ok && sb_puts(&b, " (buffer ends with \"") &&
         sb_put_escaped(&b, buf + s->len - tail, tail) && sb_puts(&b, "\")");
  }

  if (ok) {
    lcl_set_error(interp, b.s);
  } else {
    lcl_set_error(interp, timed_out ? "Expect: timed out" : "Expect: eof");
  }

  sb_free(&b);
  return LCL_RC_ERR;
}

static int table_find(const pat_table *t, const char *buf, wait_outcome *o) {
  int found = 0;
  size_t i;

  for (i = 0; i < t->n; i++) {
    regmatch_t m[MAX_GROUPS];
    size_t ngroups = 0;

    if (!pattern_find(t->entries[i].p, buf, m, &ngroups)) {
      continue;
    }

    if (!found || m[0].rm_so < o->m[0].rm_so) {
      found = 1;
      o->entry = i;
      o->ngroups = ngroups;
      memcpy(o->m, m, sizeof(m));
    }
  }

  return found;
}

static lcl_return_code session_wait(lcl_interp *interp, const char *who,
                                    expect_session *s, const pat_table *t,
                                    lcl_int timeout_ms, wait_outcome *o) {
  lcl_i64 deadline = timeout_ms >= 0 ? now_ms() + timeout_ms : -1;
  size_t timeout_idx = t->n;
  size_t eof_idx = t->n;
  size_t i;

  if (s->closed) {
    return fail(interp, who, "session is closed");
  }

  for (i = 0; i < t->n; i++) {
    if (t->entries[i].p->kind == PAT_TIMEOUT && timeout_idx == t->n) {
      timeout_idx = i;
    } else if (t->entries[i].p->kind == PAT_EOF && eof_idx == t->n) {
      eof_idx = i;
    }
  }

  for (;;) {
    lcl_i64 remaining;
    lcl_return_code rc;

    memset(o, 0, sizeof(*o));

    if (table_find(t, buf_text(s), o)) {
      return LCL_RC_OK;
    }

    if (s->eof) {
      if (eof_idx < t->n) {
        o->entry = eof_idx;
        o->eof = 1;
        return LCL_RC_OK;
      }

      return wait_failed(interp, who, s, t, 0, timeout_ms);
    }

    remaining = deadline < 0 ? SLICE_MS : deadline - now_ms();

    if (remaining <= 0) {
      if (timeout_idx < t->n) {
        o->entry = timeout_idx;
        o->timed_out = 1;
        return LCL_RC_OK;
      }

      return wait_failed(interp, who, s, t, 1, timeout_ms);
    }

    rc = session_fill(interp, s,
                      remaining > SLICE_MS ? SLICE_MS : (lcl_int)remaining);

    if (rc != LCL_RC_OK) {
      return rc;
    }
  }
}

static lcl_value *session_commit(expect_session *s, const pat_table *t,
                                 const wait_outcome *o) {
  lcl_value *d = make_result(s, t, o);

  if (!d) {
    return NULL;
  }

  if (!o->timed_out && !o->eof) {
    buf_consume(s, (size_t)o->m[0].rm_eo);
  }

  if (s->last) {
    lcl_ref_dec(s->last);
  }

  s->last = lcl_ref_inc(d);
  return d;
}

static lcl_return_code session_timeout(lcl_interp *interp, const char *who,
                                       expect_session *s, lcl_value *opts,
                                       lcl_int *out) {
  return opt_int(interp, who, opts, "timeout", s->timeout_ms, out);
}

/* Expect::spawn argv ?opts? */
static lcl_return_code c_spawn(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *who = "Expect::spawn";
  lcl_value *opts;
  lcl_value *spawn_opts;
  lcl_value *keys = NULL;
  lcl_value *handle = NULL;
  lcl_value *args[2];
  lcl_int timeout_ms;
  lcl_int limit;
  expect_session *s;
  lcl_return_code rc;
  size_t i;

  if (argc < 1 || argc > 2 || lcl_value_type_of(argv[0]) != LCL_LIST ||
      lcl_list_len(argv[0]) == 0) {
    return fail(interp, who, "expected a non-empty argv list");
  }

  if (check_opts(interp, who, argc, argv, 1, &opts) != LCL_RC_OK ||
      opt_int(interp, who, opts, "timeout", DEFAULT_TIMEOUT_MS, &timeout_ms) !=
          LCL_RC_OK ||
      opt_int(interp, who, opts, "limit", (lcl_int)DEFAULT_LIMIT, &limit) !=
          LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (limit <= 0) {
    return fail(interp, who, "option limit must be positive");
  }

  spawn_opts = lcl_dict_new();

  if (!spawn_opts) {
    return oom(interp);
  }

  if (opts && lcl_dict_keys(opts, &keys) == LCL_OK && keys) {
    for (i = 0; i < lcl_list_len(keys); i++) {
      const char *k = lcl_value_to_string(lcl_list_peek(keys, i));
      lcl_value *v;

      if (!k || strcmp(k, "timeout") == 0 || strcmp(k, "limit") == 0) {
        continue;
      }

      v = lcl_dict_peek(opts, k);

      if (v) {
        lcl_dict_put(&spawn_opts, k, v);
      }
    }

    lcl_ref_dec(keys);
  }

  if (!lcl_dict_peek(spawn_opts, "pty")) {
    dict_put_take(&spawn_opts, "pty", lcl_int_new(1));
  }

  args[0] = argv[0];
  args[1] = spawn_opts;
  rc = call_process(interp, "spawn", 2, args, &handle);
  lcl_ref_dec(spawn_opts);

  if (rc != LCL_RC_OK) {
    return rc;
  }

  s = (expect_session *)calloc(1, sizeof(expect_session));

  if (!s) {
    lcl_ref_dec(handle);
    return oom(interp);
  }

  s->handle = handle;
  s->timeout_ms = (int)timeout_ms;
  s->limit = (size_t)limit;
  *out = lcl_opaque_new(s, SESSION_TAG, session_finalizer);

  if (!*out) {
    session_finalizer(s);
    return oom(interp);
  }

  return LCL_RC_OK;
}

/* Expect::handle session */
static lcl_return_code c_handle(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  expect_session *s = get_session(interp, "Expect::handle", argc, argv);

  if (!s) {
    return LCL_RC_ERR;
  }

  *out = lcl_ref_inc(s->handle);
  return LCL_RC_OK;
}

static lcl_return_code session_send(lcl_interp *interp, const char *who,
                                    expect_session *s, lcl_value *data,
                                    lcl_value **out) {
  lcl_value *args[2];

  if (s->closed) {
    return fail(interp, who, "session is closed");
  }

  if (s->eof) {
    return fail(interp, who, "child has exited");
  }

  args[0] = s->handle;
  args[1] = data;
  return call_process(interp, "send", 2, args, out);
}

/* Expect::send session text */
static lcl_return_code c_send(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  const char *who = "Expect::send";
  expect_session *s = get_session(interp, who, argc, argv);

  if (!s) {
    return LCL_RC_ERR;
  }

  if (argc != 2) {
    return fail(interp, who, "expected session and text");
  }

  return session_send(interp, who, s, argv[1], out);
}

/* Expect::send-line session ?text? */
static lcl_return_code c_send_line(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  const char *who = "Expect::send-line";
  expect_session *s = get_session(interp, who, argc, argv);
  const char *text = "";
  strbuf b = {NULL, 0, 0};
  lcl_value *line;
  lcl_return_code rc;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (argc > 2) {
    return fail(interp, who, "expected session and optional text");
  }

  if (argc == 2 && lcl_value_to_cstring(interp, argv[1], &text) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!sb_puts(&b, text) || !sb_putc(&b, '\n')) {
    sb_free(&b);
    return oom(interp);
  }

  line = lcl_string_new(b.s);
  sb_free(&b);

  if (!line) {
    return oom(interp);
  }

  rc = session_send(interp, who, s, line, out);
  lcl_ref_dec(line);
  return rc;
}

/* Expect::send-ctrl session char -- Ctrl-A..Ctrl-Z, plus @ [ \ ] ^ _ */
static lcl_return_code c_send_ctrl(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  const char *who = "Expect::send-ctrl";
  expect_session *s = get_session(interp, who, argc, argv);
  const char *key;
  int c;
  char ctrl[2];
  lcl_value *data;
  lcl_return_code rc;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (argc != 2 || lcl_value_to_cstring(interp, argv[1], &key) != LCL_OK) {
    return fail(interp, who, "expected session and a key letter");
  }

  c = toupper((unsigned char)key[0]);

  if (key[0] == '\0' || key[1] != '\0' || c < '@' || c > '_') {
    return fail(interp, who, "key must be a letter a-z (or @ [ \\ ] ^ _)");
  }

  ctrl[0] = (char)(c - '@');
  ctrl[1] = '\0';
  data = lcl_string_new(ctrl);

  if (!data) {
    return oom(interp);
  }

  rc = session_send(interp, who, s, data, out);
  lcl_ref_dec(data);
  return rc;
}

/* Expect::wait-for session patterns ?opts? */
static lcl_return_code c_wait_for(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  const char *who = "Expect::wait-for";
  expect_session *s = get_session(interp, who, argc, argv);
  lcl_value *opts;
  lcl_int timeout_ms;
  pat_table t;
  wait_outcome o;
  lcl_return_code rc;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (argc < 2 || argc > 3) {
    return fail(interp, who, "expected session, patterns and optional options");
  }

  if (check_opts(interp, who, argc, argv, 2, &opts) != LCL_RC_OK ||
      session_timeout(interp, who, s, opts, &timeout_ms) != LCL_RC_OK ||
      table_from_patterns(interp, who, argv[1], &t) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  rc = session_wait(interp, who, s, &t, timeout_ms, &o);

  if (rc == LCL_RC_OK) {
    *out = session_commit(s, &t, &o);

    if (!*out) {
      rc = oom(interp);
    }
  }

  table_free(&t);
  return rc;
}

/* Expect::expect session clauses ?opts? */
static lcl_return_code c_expect(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *who = "Expect::expect";
  expect_session *s = get_session(interp, who, argc, argv);
  lcl_value *opts;
  lcl_int timeout_ms;
  pat_table t;
  lcl_return_code rc = LCL_RC_OK;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (argc < 2 || argc > 3) {
    return fail(interp, who, "expected session, clauses and optional options");
  }

  if (check_opts(interp, who, argc, argv, 2, &opts) != LCL_RC_OK ||
      session_timeout(interp, who, s, opts, &timeout_ms) != LCL_RC_OK ||
      table_from_clauses(interp, who, argv[1], &t) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  for (;;) {
    wait_outcome o;
    lcl_value *result;
    lcl_value *body_result = NULL;
    strbuf label = {NULL, 0, 0};
    const char *label_text = "<expect clause>";

    rc = session_wait(interp, who, s, &t, timeout_ms, &o);

    if (rc != LCL_RC_OK) {
      break;
    }

    result = session_commit(s, &t, &o);

    if (!result) {
      rc = oom(interp);
      break;
    }

    /* The session keeps the dict (Expect::out); the body is run for
     * effect, in the caller's scope. */
    if (sb_puts(&label, "<expect ") &&
        sb_describe_pattern(&label, t.entries[o.entry].p) &&
        sb_putc(&label, '>')) {
      label_text = label.s;
    }

    rc = lcl_eval_string_file(interp, t.bodies[t.entries[o.entry].clause],
                              label_text, &body_result);
    sb_free(&label);

    if (rc == LCL_RC_CONTINUE) {
      if (body_result) {
        lcl_ref_dec(body_result);
      }

      lcl_ref_dec(result);
      continue;
    }

    if (rc == LCL_RC_OK || rc == LCL_RC_BREAK) {
      if (body_result) {
        lcl_ref_dec(body_result);
      }

      *out = result;
      rc = LCL_RC_OK;
      break;
    }

    /* `return` (payload travels in body_result) or an error. */
    lcl_ref_dec(result);
    *out = body_result;
    break;
  }

  table_free(&t);
  return rc;
}

/* Expect::out session */
static lcl_return_code c_out(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  expect_session *s = get_session(interp, "Expect::out", argc, argv);

  if (!s) {
    return LCL_RC_ERR;
  }

  *out = s->last ? lcl_ref_inc(s->last) : lcl_dict_new();
  return *out ? LCL_RC_OK : oom(interp);
}

/* Expect::buffer session */
static lcl_return_code c_buffer(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  expect_session *s = get_session(interp, "Expect::buffer", argc, argv);

  if (!s) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(buf_text(s));
  return *out ? LCL_RC_OK : oom(interp);
}

/* Expect::drain session ?opts? -- opts: quiet (ms) */
static lcl_return_code c_drain(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *who = "Expect::drain";
  expect_session *s = get_session(interp, who, argc, argv);
  lcl_value *opts;
  lcl_int quiet;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (check_opts(interp, who, argc, argv, 1, &opts) != LCL_RC_OK ||
      opt_int(interp, who, opts, "quiet", DEFAULT_QUIET_MS, &quiet) !=
          LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (quiet < 0) {
    return fail(interp, who, "option quiet must not be negative");
  }

  while (!s->eof && !s->closed) {
    size_t before = s->len;
    lcl_return_code rc = session_fill(interp, s, quiet);

    if (rc != LCL_RC_OK) {
      return rc;
    }

    if (s->len == before) {
      break;
    }
  }

  *out = lcl_string_new(buf_text(s));
  buf_consume(s, s->len);
  return *out ? LCL_RC_OK : oom(interp);
}

/* Expect::eof? session */
static lcl_return_code c_is_eof(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  expect_session *s = get_session(interp, "Expect::eof?", argc, argv);

  if (!s) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(s->eof || s->closed);
  return LCL_RC_OK;
}

/* Expect::set-timeout session ms */
static lcl_return_code c_set_timeout(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  const char *who = "Expect::set-timeout";
  expect_session *s = get_session(interp, who, argc, argv);
  lcl_int ms;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (argc != 2 || lcl_value_to_int(argv[1], &ms) != LCL_OK) {
    return fail(interp, who, "expected session and milliseconds");
  }

  s->timeout_ms = (int)ms;
  *out = lcl_int_new(ms);
  return LCL_RC_OK;
}

/* Expect::get-timeout session */
static lcl_return_code c_get_timeout(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  expect_session *s = get_session(interp, "Expect::get-timeout", argc, argv);

  if (!s) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(s->timeout_ms);
  return LCL_RC_OK;
}

static lcl_return_code process_wait(lcl_interp *interp, lcl_value *handle,
                                    lcl_int timeout_ms, int *exited,
                                    lcl_value **result) {
  lcl_value *opts = NULL;
  lcl_value *v;
  lcl_return_code rc;

  *exited = 0;
  *result = NULL;

  if (timeout_ms > 0) {
    opts = lcl_dict_new();

    if (!opts) {
      return oom(interp);
    }

    dict_put_take(&opts, "timeout", lcl_int_new(timeout_ms));
  }

  rc = call_process_opts(interp, "wait", handle, opts, result);

  if (opts) {
    lcl_ref_dec(opts);
  }

  if (rc != LCL_RC_OK) {
    return rc;
  }

  v = lcl_dict_peek(*result, "exited");

  if (v) {
    lcl_int n = 0;

    lcl_value_to_int(v, &n);
    *exited = n != 0;
  }

  return LCL_RC_OK;
}

static lcl_return_code process_kill(lcl_interp *interp, lcl_value *handle,
                                    const char *sig) {
  lcl_value *opts = lcl_dict_new();
  lcl_value *res = NULL;
  lcl_return_code rc;

  if (!opts) {
    return oom(interp);
  }

  dict_put_take(&opts, "signal", lcl_string_new(sig));
  rc = call_process_opts(interp, "kill", handle, opts, &res);
  lcl_ref_dec(opts);

  if (res) {
    lcl_ref_dec(res);
  }

  return rc;
}

/* Expect::close session ?opts? -- opts: grace (ms)
 *
 * Close the child's descriptors, then make sure it is gone: wait
 * `grace` ms for it to exit on its own, then SIGTERM and wait again,
 * then SIGKILL. Returns the Process::wait result. */
static lcl_return_code c_close(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *who = "Expect::close";
  expect_session *s = get_session(interp, who, argc, argv);
  lcl_value *opts;
  lcl_value *res = NULL;
  lcl_int grace;
  int exited = 0;
  lcl_return_code rc;
  static const char *const signals[] = {"TERM", "KILL"};
  size_t step;

  if (!s) {
    return LCL_RC_ERR;
  }

  if (check_opts(interp, who, argc, argv, 1, &opts) != LCL_RC_OK ||
      opt_int(interp, who, opts, "grace", DEFAULT_GRACE_MS, &grace) !=
          LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (grace < 0) {
    return fail(interp, who, "option grace must not be negative");
  }

  s->closed = 1;
  rc = call_process_opts(interp, "close", s->handle, NULL, &res);

  if (res) {
    lcl_ref_dec(res);
    res = NULL;
  }

  if (rc != LCL_RC_OK) {
    return rc;
  }

  rc = process_wait(interp, s->handle, grace > 0 ? grace : 1, &exited, &res);

  for (step = 0; rc == LCL_RC_OK && !exited && step < 2; step++) {
    lcl_ref_dec(res);
    res = NULL;
    rc = process_kill(interp, s->handle, signals[step]);

    if (rc != LCL_RC_OK) {
      break;
    }

    rc = process_wait(interp, s->handle, step == 0 ? grace : 0, &exited, &res);
  }

  if (rc != LCL_RC_OK) {
    if (res) {
      lcl_ref_dec(res);
    }

    return rc;
  }

  *out = res;
  return LCL_RC_OK;
}

static lcl_return_code c_search(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *who = "Expect::search";
  const char *text;
  pat_table t;
  wait_outcome o;
  lcl_value *d;

  if (argc != 2) {
    return fail(interp, who, "expected text and patterns");
  }

  if (lcl_value_to_cstring(interp, argv[0], &text) != LCL_OK ||
      table_from_patterns(interp, who, argv[1], &t) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  d = lcl_dict_new();

  if (!d) {
    table_free(&t);
    return oom(interp);
  }

  if (table_find(&t, text, &o)) {
    size_t so = (size_t)o.m[0].rm_so;
    size_t eo = (size_t)o.m[0].rm_eo;
    lcl_value *groups = lcl_list_new();

    if (t.entries[o.entry].p->kind == PAT_REGEX && groups) {
      size_t g;

      for (g = 0; g < o.ngroups; g++) {
        lcl_value *gv =
            o.m[g].rm_so < 0
                ? lcl_string_new("")
                : string_from_range(text + o.m[g].rm_so,
                                    (size_t)(o.m[g].rm_eo - o.m[g].rm_so));

        if (gv) {
          lcl_list_push(&groups, gv);
          lcl_ref_dec(gv);
        }
      }
    }

    dict_put_take(&d, "matched", lcl_int_new(1));
    dict_put_take(&d, "index", lcl_int_new((lcl_int)t.entries[o.entry].clause));
    dict_put_take(&d, "before", string_from_range(text, so));
    dict_put_take(&d, "match", string_from_range(text + so, eo - so));
    dict_put_take(&d, "after", lcl_string_new(text + eo));
    dict_put_take(&d, "groups", groups);
    table_free(&t);
    *out = d;
    return LCL_RC_OK;
  }

  dict_put_take(&d, "matched", lcl_int_new(0));
  table_free(&t);
  *out = d;
  return LCL_RC_OK;
}

void lcl_register_expect(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new(EXPECT_NS);

  lcl_define_take(interp, EXPECT_NS, ns);

  lcl_ns_def_take(ns, "spawn", lcl_c_proc_new("Expect::spawn", c_spawn));
  lcl_ns_def_take(ns, "handle", lcl_c_proc_new("Expect::handle", c_handle));
  lcl_ns_def_take(ns, "send", lcl_c_proc_new("Expect::send", c_send));
  lcl_ns_def_take(ns, "send-line",
                  lcl_c_proc_new("Expect::send-line", c_send_line));
  lcl_ns_def_take(ns, "send-ctrl",
                  lcl_c_proc_new("Expect::send-ctrl", c_send_ctrl));
  lcl_ns_def_take(ns, "expect", lcl_c_proc_new("Expect::expect", c_expect));
  lcl_ns_def_take(ns, "wait-for",
                  lcl_c_proc_new("Expect::wait-for", c_wait_for));
  lcl_ns_def_take(ns, "out", lcl_c_proc_new("Expect::out", c_out));
  lcl_ns_def_take(ns, "buffer", lcl_c_proc_new("Expect::buffer", c_buffer));
  lcl_ns_def_take(ns, "drain", lcl_c_proc_new("Expect::drain", c_drain));
  lcl_ns_def_take(ns, "eof?", lcl_c_proc_new("Expect::eof?", c_is_eof));
  lcl_ns_def_take(ns, "set-timeout",
                  lcl_c_proc_new("Expect::set-timeout", c_set_timeout));
  lcl_ns_def_take(ns, "get-timeout",
                  lcl_c_proc_new("Expect::get-timeout", c_get_timeout));
  lcl_ns_def_take(ns, "close", lcl_c_proc_new("Expect::close", c_close));

  lcl_ns_def_take(ns, "literal", lcl_c_proc_new("Expect::literal", c_literal));
  lcl_ns_def_take(ns, "regex", lcl_c_proc_new("Expect::regex", c_regex));
  lcl_ns_def_take(ns, "timeout", lcl_c_proc_new("Expect::timeout", c_timeout));
  lcl_ns_def_take(ns, "eof", lcl_c_proc_new("Expect::eof", c_eof));
  lcl_ns_def_take(ns, "pattern?",
                  lcl_c_proc_new("Expect::pattern?", c_is_pattern));
  lcl_ns_def_take(ns, "pattern-kind",
                  lcl_c_proc_new("Expect::pattern-kind", c_pattern_kind));
  lcl_ns_def_take(ns, "search", lcl_c_proc_new("Expect::search", c_search));
}
