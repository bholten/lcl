#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lcl.h>

#define TIME_NS "time"

static inline void dict_put_int(lcl_value **d, const char *key, int val) {
  lcl_value *v = lcl_int_new(val);
  lcl_dict_put(d, key, v);
  lcl_ref_dec(v);
}

static inline lcl_value *tm_to_dict(const struct tm *tm) {
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

static inline int dict_to_tm(lcl_value *d, struct tm *tm) {
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
static int c_time(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_int_new((long)time(NULL));
  return LCL_RC_OK;
}

/* time::clock -> CPU time in microseconds */
static int c_clock(lcl_interp *interp, int argc, lcl_value **argv,
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
static int c_monotonic_us(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  struct timespec ts;
  unsigned long long usec;
  (void)interp;
  (void)argc;
  (void)argv;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return LCL_RC_ERR;
  }

  /* On 64-bit platforms `long` is wide enough to hold microseconds
   * since boot for any realistic uptime. On 32-bit it wraps after ~35
   * minutes — acceptable since lcl_int_new takes long. */
  usec = (unsigned long long)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

  *out = lcl_int_new((long)usec);
  return LCL_RC_OK;
}

/* time::localtime ?timestamp? -> dict with local time components */
static int c_localtime(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
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
static int c_gmtime(lcl_interp *interp, int argc, lcl_value **argv,
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
static int c_mktime(lcl_interp *interp, int argc, lcl_value **argv,
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
static int c_strftime(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
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
static int c_difftime(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
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
static int c_sleep(lcl_interp *interp, int argc, lcl_value **argv,
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
  ts.tv_nsec = (long)((secs - ts.tv_sec) * 1000000000);

  nanosleep(&ts, NULL);

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* time::ctime ?timestamp? -> human-readable time string */
static int c_ctime(lcl_interp *interp, int argc, lcl_value **argv,
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

void lcl_register_time(lcl_interp *interp) {
  lcl_value *time_ns = lcl_ns_new(TIME_NS);
  lcl_define_take(interp, TIME_NS, time_ns);

  lcl_ns_def(time_ns, "time", lcl_c_proc_new("time::time", c_time));
  lcl_ns_def(time_ns, "clock", lcl_c_proc_new("time::clock", c_clock));
  lcl_ns_def(time_ns, "monotonic_us",
             lcl_c_proc_new("time::monotonic_s", c_monotonic_us));
  lcl_ns_def(time_ns, "localtime",
             lcl_c_proc_new("time::localtime", c_localtime));
  lcl_ns_def(time_ns, "gmtime", lcl_c_proc_new("time::gmtime", c_gmtime));
  lcl_ns_def(time_ns, "mktime", lcl_c_proc_new("time::mktime", c_mktime));
  lcl_ns_def(time_ns, "strftime", lcl_c_proc_new("time::strftime", c_strftime));
  lcl_ns_def(time_ns, "difftime", lcl_c_proc_new("time::difftime", c_difftime));
  lcl_ns_def(time_ns, "sleep", lcl_c_proc_new("time::sleep", c_sleep));
  lcl_ns_def(time_ns, "ctime", lcl_c_proc_new("time::ctime", c_ctime));
}
