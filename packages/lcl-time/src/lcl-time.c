#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lcl.h>

#define TIME_NS "time"

static void dict_put_int(lcl_value **d, const char *key, int val) {
  lcl_value *v = lcl_int_new(val);
  lcl_dict_put(d, key, v);
  lcl_ref_dec(v);
}

static lcl_value *tm_to_dict(const struct tm *tm) {
  lcl_value *d = lcl_dict_new();

  if (!d) {
    return NULL;
  }

  dict_put_int(&d, "sec", tm->tm_sec);
  dict_put_int(&d, "min", tm->tm_min);
  dict_put_int(&d, "hour", tm->tm_hour);
  dict_put_int(&d, "mday", tm->tm_mday);
  dict_put_int(&d, "mon", tm->tm_mon + 1);
  dict_put_int(&d, "year", tm->tm_year + 1900);
  dict_put_int(&d, "wday", tm->tm_wday);
  dict_put_int(&d, "yday", tm->tm_yday + 1);
  dict_put_int(&d, "isdst", tm->tm_isdst);

  return d;
}

static int dict_to_tm(lcl_value *d, struct tm *tm) {
  lcl_value *v = NULL;
  long val;

  memset(tm, 0, sizeof(*tm));

  if (lcl_dict_get(d, "sec", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_sec = (int)val;
    }
  }

  if (lcl_dict_get(d, "min", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_min = (int)val;
    }
  }

  if (lcl_dict_get(d, "hour", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_hour = (int)val;
    }
  }

  if (lcl_dict_get(d, "mday", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_mday = (int)val;
    }
  }

  if (lcl_dict_get(d, "mon", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_mon = (int)val - 1;
    }
  }

  if (lcl_dict_get(d, "year", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_year = (int)val - 1900;
    }
  }

  if (lcl_dict_get(d, "isdst", &v) == LCL_OK && v) {
    if (lcl_value_to_int(v, &val) == LCL_OK) {
      tm->tm_isdst = (int)val;
    }
  } else {
    tm->tm_isdst = -1;
  }

  return 0;
}

/* time::time -> current Unix timestamp (seconds since epoch) */
static lcl_return_code c_time(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_int_new((long)time(NULL));
  return LCL_RC_OK;
}

/* time::clock -> CPU time in microseconds */
static lcl_return_code c_clock(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  clock_t c;
  (void)interp;
  (void)argc;
  (void)argv;

  c = clock();
  *out = lcl_int_new((long)(c * 1000000 / CLOCKS_PER_SEC));

  return LCL_RC_OK;
}

/* time::monotonic_us -> microseconds since arbitrary monotonic point */
static lcl_return_code c_monotonic_us(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct timespec ts;
  unsigned long usec;
  (void)interp;
  (void)argc;
  (void)argv;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return LCL_RC_ERR;
  }

  /* On 64-bit platforms `long` is wide enough to hold microseconds
   * since boot for any realistic uptime. On 32-bit it wraps after ~35
   * minutes — acceptable since lcl_int_new takes long. Unsigned
   * arithmetic keeps the wrap well-defined. */
  usec =
      (unsigned long)ts.tv_sec * 1000000UL + (unsigned long)(ts.tv_nsec / 1000);

  *out = lcl_int_new((long)usec);
  return LCL_RC_OK;
}

/* time::localtime ?timestamp? -> dict with local time components */
static lcl_return_code c_localtime(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  time_t t;
  struct tm *tm;
  long ts;

  if (argc >= 1) {
    if (lcl_value_to_int(argv[0], &ts) != LCL_OK) {
      lcl_set_error(interp, "time::localtime: expected integer timestamp");
      return LCL_RC_ERR;
    }
    t = (time_t)ts;
  } else {
    t = time(NULL);
  }

  tm = localtime(&t);

  if (!tm) {
    lcl_set_error(interp, "time::localtime: failed to convert timestamp");
    return LCL_RC_ERR;
  }

  *out = tm_to_dict(tm);

  if (!*out) {
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/* time::gmtime ?timestamp? -> dict with UTC time components */
static lcl_return_code c_gmtime(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  time_t t;
  struct tm *tm;
  long ts;

  if (argc >= 1) {
    if (lcl_value_to_int(argv[0], &ts) != LCL_OK) {
      lcl_set_error(interp, "time::gmtime: expected integer timestamp");
      return LCL_RC_ERR;
    }
    t = (time_t)ts;
  } else {
    t = time(NULL);
  }

  tm = gmtime(&t);

  if (!tm) {
    lcl_set_error(interp, "time::gmtime: failed to convert timestamp");
    return LCL_RC_ERR;
  }

  *out = tm_to_dict(tm);

  if (!*out) {
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/* time::mktime dict -> Unix timestamp from time dict */
static lcl_return_code c_mktime(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  struct tm tm;
  time_t t;

  if (argc < 1) {
    lcl_set_error(interp, "time::mktime requires a time dict");
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[0]) != LCL_DICT) {
    lcl_set_error(interp, "time::mktime: expected dict");
    return LCL_RC_ERR;
  }

  dict_to_tm(argv[0], &tm);
  t = mktime(&tm);

  if (t == (time_t)-1) {
    lcl_set_error(interp, "time::mktime: invalid time");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)t);
  return LCL_RC_OK;
}

/* time::strftime format ?timestamp? -> formatted time string */
static lcl_return_code c_strftime(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  const char *fmt;
  time_t t;
  struct tm *tm;
  char buf[256];
  size_t len;
  long ts;

  if (argc < 1) {
    lcl_set_error(interp, "time::strftime requires a format string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &fmt) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    if (lcl_value_to_int(argv[1], &ts) != LCL_OK) {
      lcl_set_error(interp, "time::strftime: expected integer timestamp");
      return LCL_RC_ERR;
    }

    t = (time_t)ts;
  } else {
    t = time(NULL);
  }

  tm = localtime(&t);

  if (!tm) {
    lcl_set_error(interp, "time::strftime: failed to convert timestamp");
    return LCL_RC_ERR;
  }

  len = strftime(buf, sizeof(buf), fmt, tm);

  if (len == 0 && fmt[0] != '\0') {
    lcl_set_error(interp, "time::strftime: format failed or buffer too small");
    return LCL_RC_ERR;
  }

  buf[len] = '\0';
  *out = lcl_string_new(buf);
  return LCL_RC_OK;
}

/* time::difftime t1 t2 -> difference in seconds (t1 - t2) */
static lcl_return_code c_difftime(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  long t1;
  long t2;
  double diff;

  if (argc < 2) {
    lcl_set_error(interp, "time::difftime requires two timestamps");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &t1) != LCL_OK ||
      lcl_value_to_int(argv[1], &t2) != LCL_OK) {
    lcl_set_error(interp, "time::difftime: expected integer timestamps");
    return LCL_RC_ERR;
  }

  diff = difftime((time_t)t1, (time_t)t2);
  *out = lcl_float_new(diff);
  return LCL_RC_OK;
}

/* time::sleep seconds -> sleep for given duration (supports fractions) */
static lcl_return_code c_sleep(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  double secs;
  long isecs;
  struct timespec ts;

  if (argc < 1) {
    lcl_set_error(interp, "time::sleep requires duration in seconds");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &secs) != LCL_OK) {
    if (lcl_value_to_int(argv[0], &isecs) != LCL_OK) {
      lcl_set_error(interp, "time::sleep: expected number");
      return LCL_RC_ERR;
    }
    secs = (double)isecs;
  }

  if (secs < 0) {
    lcl_set_error(interp, "time::sleep: duration must be non-negative");
    return LCL_RC_ERR;
  }

  ts.tv_sec = (time_t)secs;
  ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1000000000.0);

  nanosleep(&ts, NULL);

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* time::ctime ?timestamp? -> human-readable time string */
static lcl_return_code c_ctime(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  time_t t;
  char *str;
  char buf[64];
  long ts;
  size_t len;

  if (argc >= 1) {
    if (lcl_value_to_int(argv[0], &ts) != LCL_OK) {
      lcl_set_error(interp, "time::ctime: expected integer timestamp");
      return LCL_RC_ERR;
    }
    t = (time_t)ts;
  } else {
    t = time(NULL);
  }

  str = ctime(&t);

  if (!str) {
    lcl_set_error(interp, "time::ctime: failed to convert timestamp");
    return LCL_RC_ERR;
  }

  len = strlen(str);

  if (len > 0 && str[len - 1] == '\n') {
    len--;
  }

  if (len >= sizeof(buf)) {
    len = sizeof(buf) - 1;
  }

  memcpy(buf, str, len);
  buf[len] = '\0';

  *out = lcl_string_new(buf);
  return LCL_RC_OK;
}

typedef struct {
  char *name;
  unsigned long calls;
  unsigned long incl_us;
  unsigned long excl_us;
} prof_entry;

typedef struct {
  size_t entry;
  unsigned long start_us;
  unsigned long child_us;
} prof_frame;

typedef struct {
  lcl_interp *interp;
  prof_entry *entries;
  size_t n;
  size_t cap;
  size_t *index;
  size_t index_cap;
  prof_frame *stack;
  size_t depth;
  size_t stack_cap;
  lcl_call_fn prev_fn;
  void *prev_ud;
  int active;
  int failed;
} profiler;

static profiler g_prof;

static unsigned long prof_now_us(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }

  return (unsigned long)ts.tv_sec * 1000000UL +
         (unsigned long)(ts.tv_nsec / 1000);
}

static unsigned long prof_hash(const char *s) {
  unsigned long h = 2166136261UL;

  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 16777619UL;
  }

  return h;
}

static int prof_index_rebuild(profiler *p, size_t cap) {
  size_t *idx = (size_t *)calloc(cap, sizeof(*idx));
  size_t i;

  if (!idx) {
    return 0;
  }

  for (i = 0; i < p->n; i++) {
    size_t slot = prof_hash(p->entries[i].name) & (cap - 1);

    while (idx[slot]) {
      slot = (slot + 1) & (cap - 1);
    }

    idx[slot] = i + 1;
  }

  free(p->index);
  p->index = idx;
  p->index_cap = cap;
  return 1;
}

/* Find or create the entry for `name`; returns its index or (size_t)-1. */
static size_t prof_lookup(profiler *p, const char *name) {
  size_t slot;

  if (p->index_cap == 0 && !prof_index_rebuild(p, 64)) {
    return (size_t)-1;
  }

  slot = prof_hash(name) & (p->index_cap - 1);

  while (p->index[slot]) {
    if (strcmp(p->entries[p->index[slot] - 1].name, name) == 0) {
      return p->index[slot] - 1;
    }

    slot = (slot + 1) & (p->index_cap - 1);
  }

  if (p->n >= p->cap) {
    size_t ncap = p->cap ? p->cap * 2 : 32;
    prof_entry *ne = (prof_entry *)realloc(p->entries, ncap * sizeof(*ne));

    if (!ne) {
      return (size_t)-1;
    }

    p->entries = ne;
    p->cap = ncap;
  }

  p->entries[p->n].name = strdup(name);

  if (!p->entries[p->n].name) {
    return (size_t)-1;
  }

  p->entries[p->n].calls = 0;
  p->entries[p->n].incl_us = 0;
  p->entries[p->n].excl_us = 0;
  p->n++;

  if (p->n * 2 > p->index_cap) {
    if (!prof_index_rebuild(p, p->index_cap * 2)) {
      return (size_t)-1;
    }
  } else {
    p->index[slot] = p->n;
  }

  return p->n - 1;
}

static void prof_hook(lcl_interp *interp, lcl_value *proc, const char *name,
                      int argc, lcl_value **argv, int entering,
                      void *userdata) {
  profiler *p = (profiler *)userdata;
  (void)proc;
  (void)argc;
  (void)argv;

  if (p->interp != interp || p->failed) {
    return;
  }

  if (entering) {
    size_t e = prof_lookup(p, name);

    if (e == (size_t)-1) {
      p->failed = 1;
      return;
    }

    if (p->depth >= p->stack_cap) {
      size_t ncap = p->stack_cap ? p->stack_cap * 2 : 64;
      prof_frame *ns = (prof_frame *)realloc(p->stack, ncap * sizeof(*ns));

      if (!ns) {
        p->failed = 1;
        return;
      }

      p->stack = ns;
      p->stack_cap = ncap;
    }

    p->entries[e].calls++;
    p->stack[p->depth].entry = e;
    p->stack[p->depth].start_us = prof_now_us();
    p->stack[p->depth].child_us = 0;
    p->depth++;
  } else if (p->depth > 0) {
    prof_frame *f = &p->stack[--p->depth];
    unsigned long incl = prof_now_us() - f->start_us;
    unsigned long excl = incl > f->child_us ? incl - f->child_us : 0;

    p->entries[f->entry].incl_us += incl;
    p->entries[f->entry].excl_us += excl;

    if (p->depth > 0) {
      p->stack[p->depth - 1].child_us += incl;
    }
  }
}

static void prof_reset(profiler *p) {
  size_t i;

  for (i = 0; i < p->n; i++) {
    free(p->entries[i].name);
  }

  free(p->entries);
  free(p->index);
  free(p->stack);
  memset(p, 0, sizeof(*p));
}

static int prof_cmp_excl(const void *a, const void *b) {
  const prof_entry *x = (const prof_entry *)a;
  const prof_entry *y = (const prof_entry *)b;

  if (x->excl_us != y->excl_us) {
    return x->excl_us < y->excl_us ? 1 : -1;
  }

  if (x->incl_us != y->incl_us) {
    return x->incl_us < y->incl_us ? 1 : -1;
  }

  return strcmp(x->name, y->name);
}

static int prof_row_put(lcl_value **row, const char *key, lcl_value *v) {
  int ok;

  if (!v) {
    return 0;
  }

  ok = lcl_dict_put(row, key, v) == LCL_OK;
  lcl_ref_dec(v);
  return ok;
}

/* Build the report: a list of #{name calls incl_us excl_us} rows,
 * sorted by exclusive time, then inclusive, then name. */
static lcl_value *prof_report(profiler *p) {
  lcl_value *rows = lcl_list_new();
  size_t i;

  if (!rows) {
    return NULL;
  }

  qsort(p->entries, p->n, sizeof(*p->entries), prof_cmp_excl);

  for (i = 0; i < p->n; i++) {
    prof_entry *e = &p->entries[i];
    lcl_value *row = lcl_dict_new();

    if (!row || !prof_row_put(&row, "name", lcl_string_new(e->name)) ||
        !prof_row_put(&row, "calls", lcl_int_new((long)e->calls)) ||
        !prof_row_put(&row, "incl_us", lcl_int_new((long)e->incl_us)) ||
        !prof_row_put(&row, "excl_us", lcl_int_new((long)e->excl_us)) ||
        lcl_list_push(&rows, row) != LCL_OK) {
      lcl_ref_dec(row);
      lcl_ref_dec(rows);
      return NULL;
    }

    lcl_ref_dec(row);
  }

  return rows;
}

static lcl_return_code prof_start(lcl_interp *interp, const char *cmd) {
  if (g_prof.active) {
    lcl_set_error(interp, "profiler is already running");
    return LCL_RC_ERR;
  }

  prof_reset(&g_prof);
  g_prof.interp = interp;
  g_prof.active = 1;
  lcl_get_call_hook(interp, &g_prof.prev_fn, &g_prof.prev_ud);
  lcl_set_call_hook(interp, prof_hook, &g_prof);
  (void)cmd;
  return LCL_RC_OK;
}

static lcl_return_code prof_stop(lcl_interp *interp, const char *cmd,
                                 lcl_value **out) {
  lcl_value *rows;

  if (!g_prof.active || g_prof.interp != interp) {
    lcl_set_error(interp, "profiler is not running");
    return LCL_RC_ERR;
  }

  lcl_set_call_hook(interp, g_prof.prev_fn, g_prof.prev_ud);
  g_prof.active = 0;

  if (g_prof.failed) {
    prof_reset(&g_prof);
    lcl_set_error(interp, "profiler ran out of memory; report discarded");
    return LCL_RC_ERR;
  }

  rows = prof_report(&g_prof);
  prof_reset(&g_prof);

  if (!rows) {
    lcl_set_error(interp, "out of memory building the profile report");
    return LCL_RC_ERR;
  }

  (void)cmd;
  *out = rows;
  return LCL_RC_OK;
}

/* time::profile_start! - begin collecting; nests inside any host hook */
static lcl_return_code c_profile_start(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  (void)argv;
  (void)out;

  if (argc != 0) {
    lcl_set_error(interp, "time::profile_start!: expected 0 arguments");
    return LCL_RC_ERR;
  }

  return prof_start(interp, "time::profile_start!");
}

/* time::profile_stop! -> report rows collected since profile_start! */
static lcl_return_code c_profile_stop(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "time::profile_stop!: expected 0 arguments");
    return LCL_RC_ERR;
  }

  return prof_stop(interp, "time::profile_stop!", out);
}

/* time::profile {body} -> report rows for the procs called while
 * running body in the caller's scope; body's own value is dropped. */
static lcl_return_code s_profile(lcl_interp *interp, int argc,
                                 const lcl_word **args, lcl_value **out) {
  lcl_value *body = NULL;
  lcl_value *result = NULL;
  const char *src;
  lcl_return_code rc;

  if (argc != 1) {
    lcl_set_error(interp, "time::profile: expected 1 argument (body)");
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &body) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(body);

  if (!src) {
    lcl_ref_dec(body);
    lcl_set_error(interp, "time::profile: body must be a braced script");
    return LCL_RC_ERR;
  }

  if (prof_start(interp, "time::profile") != LCL_RC_OK) {
    lcl_ref_dec(body);
    return LCL_RC_ERR;
  }

  rc = lcl_eval_string(interp, src, &result);
  lcl_ref_dec(result);
  lcl_ref_dec(body);

  if (rc != LCL_RC_OK) {
    lcl_value *dropped = NULL;

    /* keep the body's error, not the report */
    if (prof_stop(interp, "time::profile", &dropped) == LCL_RC_OK) {
      lcl_ref_dec(dropped);
    }

    return rc;
  }

  return prof_stop(interp, "time::profile", out);
}

/* time::profile_format rows -> aligned text table */
static lcl_return_code c_profile_format(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lcl_value *rows;
  size_t n;
  size_t i;
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;

  if (argc != 1 || lcl_value_type_of(argv[0]) != LCL_LIST) {
    lcl_set_error(interp, "time::profile_format: expected a report list");
    return LCL_RC_ERR;
  }

  rows = argv[0];
  n = lcl_list_len(rows);

  for (i = 0; i <= n; i++) {
    char line[512];
    size_t ll;

    if (i == 0) {
      snprintf(line, sizeof(line), "%10s %12s %12s  %s\n", "calls", "incl_us",
               "excl_us", "name");
    } else {
      lcl_value *row = lcl_list_peek(rows, i - 1);
      lcl_value *name = row ? lcl_dict_peek(row, "name") : NULL;
      lcl_value *calls = row ? lcl_dict_peek(row, "calls") : NULL;
      lcl_value *incl = row ? lcl_dict_peek(row, "incl_us") : NULL;
      lcl_value *excl = row ? lcl_dict_peek(row, "excl_us") : NULL;

      if (!name || !calls || !incl || !excl) {
        free(buf);
        lcl_set_error(interp, "time::profile_format: malformed report row");
        return LCL_RC_ERR;
      }

      snprintf(line, sizeof(line), "%10s %12s %12s  %s\n",
               lcl_value_to_string(calls), lcl_value_to_string(incl),
               lcl_value_to_string(excl), lcl_value_to_string(name));
    }

    ll = strlen(line);

    if (len + ll + 1 > cap) {
      size_t ncap = cap ? cap * 2 : 256;
      char *nb;

      while (ncap < len + ll + 1) {
        ncap *= 2;
      }

      nb = (char *)realloc(buf, ncap);

      if (!nb) {
        free(buf);
        lcl_set_error(interp, "time::profile_format: out of memory");
        return LCL_RC_ERR;
      }

      buf = nb;
      cap = ncap;
    }

    memcpy(buf + len, line, ll + 1);
    len += ll;
  }

  /* drop the trailing newline */
  if (len > 0) {
    buf[len - 1] = '\0';
  }

  *out = lcl_string_new(buf ? buf : "");
  free(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

void lcl_register_time(lcl_interp *interp) {
  lcl_value *time_ns = lcl_ns_new(TIME_NS);
  lcl_define_take(interp, TIME_NS, time_ns);

  lcl_ns_def_take(time_ns, "time", lcl_c_proc_new("time::time", c_time));
  lcl_ns_def_take(time_ns, "clock", lcl_c_proc_new("time::clock", c_clock));
  lcl_ns_def_take(time_ns, "monotonic_us",
                  lcl_c_proc_new("time::monotonic_us", c_monotonic_us));
  lcl_ns_def_take(time_ns, "profile",
                  lcl_c_spec_new("time::profile", s_profile));
  lcl_ns_def_take(time_ns, "profile_start!",
                  lcl_c_proc_new("time::profile_start!", c_profile_start));
  lcl_ns_def_take(time_ns, "profile_stop!",
                  lcl_c_proc_new("time::profile_stop!", c_profile_stop));
  lcl_ns_def_take(time_ns, "profile_format",
                  lcl_c_proc_new("time::profile_format", c_profile_format));
  lcl_ns_def_take(time_ns, "localtime",
                  lcl_c_proc_new("time::localtime", c_localtime));
  lcl_ns_def_take(time_ns, "gmtime", lcl_c_proc_new("time::gmtime", c_gmtime));
  lcl_ns_def_take(time_ns, "mktime", lcl_c_proc_new("time::mktime", c_mktime));
  lcl_ns_def_take(time_ns, "strftime",
                  lcl_c_proc_new("time::strftime", c_strftime));
  lcl_ns_def_take(time_ns, "difftime",
                  lcl_c_proc_new("time::difftime", c_difftime));
  lcl_ns_def_take(time_ns, "sleep", lcl_c_proc_new("time::sleep", c_sleep));
  lcl_ns_def_take(time_ns, "ctime", lcl_c_proc_new("time::ctime", c_ctime));
}
