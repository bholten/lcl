/*
 * lcl-expect - Expect-style automation for LCL
 *
 * Provides pattern-based matching for automating interactive programs,
 * leveraging LCL's lexical scoping for clean handler closures.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 600

#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <lcl-process.h>
#include <lcl.h>

#define EXPECT_NS "Expect"
#define EXPECT_PATTERN_TYPE_TAG "expect_pattern"

typedef enum {
  EXPECT_PAT_LITERAL,
  EXPECT_PAT_REGEX,
  EXPECT_PAT_TIMEOUT,
  EXPECT_PAT_EOF
} expect_pattern_kind;

typedef struct {
  expect_pattern_kind kind;
  union {
    char *literal;
    regex_t regex;
  } data;
  char *original;
  int case_insensitive;
  int regex_compiled;
} expect_pattern;

static void expect_pattern_finalizer(void *ptr);
static expect_pattern *get_pattern(lcl_value *v);

static void expect_pattern_finalizer(void *ptr) {
  expect_pattern *p = (expect_pattern *)ptr;

  if (!p) {
    return;
  }

  if (p->kind == EXPECT_PAT_LITERAL) {
    free(p->data.literal);
  } else if (p->kind == EXPECT_PAT_REGEX && p->regex_compiled) {
    regfree(&p->data.regex);
  }

  free(p->original);
  free(p);
}

static expect_pattern *get_pattern(lcl_value *v) {
  void *ptr = NULL;

  if (lcl_opaque_get(v, EXPECT_PATTERN_TYPE_TAG, &ptr) != LCL_OK) {
    return NULL;
  }

  return (expect_pattern *)ptr;
}

/*
 * Expect::pattern str ?opts?
 *
 * Create a literal pattern object.
 * Options:
 *   nocase - case insensitive matching (1/0)
 */
static lcl_return_code c_expect_pattern(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  expect_pattern *p;
  const char *str;
  int nocase = 0;

  if (argc < 1) {
    lcl_set_error(interp, "Expect::pattern requires a string argument");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    lcl_value *v = NULL;

    if (lcl_dict_get(argv[1], "nocase", &v) == LCL_OK) {
      long n;

      if (lcl_value_to_int(v, &n) == LCL_OK) {
        nocase = (int)n;
      }

      lcl_ref_dec(v);
    }
  }

  p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return LCL_RC_ERR;
  }

  p->kind = EXPECT_PAT_LITERAL;
  p->data.literal = strdup(str);
  p->original = strdup(str);
  p->case_insensitive = nocase;
  p->regex_compiled = 0;

  if (!p->data.literal || !p->original) {
    expect_pattern_finalizer(p);
    return LCL_RC_ERR;
  }

  *out = lcl_opaque_new(p, EXPECT_PATTERN_TYPE_TAG, expect_pattern_finalizer);

  return LCL_RC_OK;
}

/*
 * Expect::regex str ?opts?
 *
 * Create a regex pattern object.
 * Options:
 *   nocase - case insensitive matching (1/0)
 */
static lcl_return_code c_expect_regex(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  expect_pattern *p;
  const char *str;
  int nocase = 0;
  int flags;
  int ret;

  if (argc < 1) {
    lcl_set_error(interp, "Expect::regex requires a pattern argument");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    lcl_value *v = NULL;

    if (lcl_dict_get(argv[1], "nocase", &v) == LCL_OK) {
      long n;

      if (lcl_value_to_int(v, &n) == LCL_OK) {
        nocase = (int)n;
      }

      lcl_ref_dec(v);
    }
  }

  p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return LCL_RC_ERR;
  }

  p->kind = EXPECT_PAT_REGEX;
  p->original = strdup(str);
  p->case_insensitive = nocase;

  if (!p->original) {
    expect_pattern_finalizer(p);
    return LCL_RC_ERR;
  }

  flags = REG_EXTENDED;

  if (nocase) {
    flags |= REG_ICASE;
  }

  ret = regcomp(&p->data.regex, str, flags);

  if (ret != 0) {
    char errbuf[256];
    regerror(ret, &p->data.regex, errbuf, sizeof(errbuf));
    lcl_set_error(interp, errbuf);
    expect_pattern_finalizer(p);
    return LCL_RC_ERR;
  }

  p->regex_compiled = 1;

  *out = lcl_opaque_new(p, EXPECT_PATTERN_TYPE_TAG, expect_pattern_finalizer);

  return LCL_RC_OK;
}

/*
 * Expect::timeout
 *
 * Create a timeout sentinel pattern.
 */
static lcl_return_code c_expect_timeout(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  expect_pattern *p;

  (void)interp;
  (void)argc;
  (void)argv;

  p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return LCL_RC_ERR;
  }

  p->kind = EXPECT_PAT_TIMEOUT;
  p->original = strdup("timeout");

  *out = lcl_opaque_new(p, EXPECT_PATTERN_TYPE_TAG, expect_pattern_finalizer);

  return LCL_RC_OK;
}

/*
 * Expect::eof
 *
 * Create an EOF sentinel pattern.
 */
static lcl_return_code c_expect_eof(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  expect_pattern *p;

  (void)interp;
  (void)argc;
  (void)argv;

  p = (expect_pattern *)calloc(1, sizeof(expect_pattern));

  if (!p) {
    return LCL_RC_ERR;
  }

  p->kind = EXPECT_PAT_EOF;
  p->original = strdup("eof");

  *out = lcl_opaque_new(p, EXPECT_PATTERN_TYPE_TAG, expect_pattern_finalizer);

  return LCL_RC_OK;
}

/*
 * Expect::pattern? value
 *
 * Check if value is a pattern object.
 */
static lcl_return_code c_expect_is_pattern(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  (void)interp;

  if (argc < 1) {
    *out = lcl_int_new(0);
    return LCL_RC_OK;
  }

  *out = lcl_int_new(get_pattern(argv[0]) != NULL);
  return LCL_RC_OK;
}

/*
 * Expect::pattern-kind pattern
 *
 * Get the kind of a pattern as a string.
 */
static lcl_return_code c_expect_pattern_kind(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  expect_pattern *p;

  if (argc < 1) {
    lcl_set_error(interp, "Expect::pattern-kind requires a pattern argument");
    return LCL_RC_ERR;
  }

  p = get_pattern(argv[0]);

  if (!p) {
    lcl_set_error(interp, "argument is not a pattern");
    return LCL_RC_ERR;
  }

  switch (p->kind) {
  case EXPECT_PAT_LITERAL: *out = lcl_string_new("literal"); break;
  case EXPECT_PAT_REGEX: *out = lcl_string_new("regex"); break;
  case EXPECT_PAT_TIMEOUT: *out = lcl_string_new("timeout"); break;
  case EXPECT_PAT_EOF: *out = lcl_string_new("eof"); break;
  default: *out = lcl_string_new("unknown"); break;
  }

  return LCL_RC_OK;
}

/*
 * Case-insensitive string search
 */
static const char *strcasestr_impl(const char *haystack, const char *needle) {
  size_t needle_len;
  size_t i;

  if (!needle || !*needle) {
    return haystack;
  }

  needle_len = strlen(needle);

  for (i = 0; haystack[i]; i++) {
    size_t j;
    int match = 1;

    for (j = 0; j < needle_len && haystack[i + j]; j++) {
      char h = haystack[i + j];
      char n = needle[j];

      if (h >= 'A' && h <= 'Z') {
        h += 32;
      }

      if (n >= 'A' && n <= 'Z') {
        n += 32;
      }

      if (h != n) {
        match = 0;
        break;
      }
    }

    if (match && j == needle_len) {
      return &haystack[i];
    }
  }
  return NULL;
}

/*
 * Match a single pattern against a buffer.
 * Returns: -1 if no match, otherwise the position of match start.
 * Sets *match_end to position after match end.
 */
static int match_pattern(const expect_pattern *p, const char *buf,
                         size_t buf_len, size_t *match_end) {
  if (p->kind == EXPECT_PAT_LITERAL) {
    const char *found;

    if (p->case_insensitive) {
      found = strcasestr_impl(buf, p->data.literal);
    } else {
      found = strstr(buf, p->data.literal);
    }

    if (found) {
      size_t pos = (size_t)(found - buf);
      *match_end = pos + strlen(p->data.literal);
      return (int)pos;
    }

    return -1;
  } else if (p->kind == EXPECT_PAT_REGEX) {
    regmatch_t match;

    if (regexec(&p->data.regex, buf, 1, &match, 0) == 0) {
      *match_end = (size_t)match.rm_eo;

      return (int)match.rm_so;
    }

    return -1;
  }

  (void)buf_len;
  return -1;
}

/*
 * Expect::match-buffer buf patterns ?opts?
 *
 * Low-level: match buffer against a list of patterns.
 *
 * Returns: #{matched 0/1 index N pattern <pat> before "..." match "..." after
 * "..."}
 */
static lcl_return_code c_expect_match_buffer(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  const char *buf;
  lcl_value *patterns;
  size_t num_patterns;
  size_t i;
  int best_pos = -1;
  size_t best_idx = 0;
  size_t best_end = 0;
  lcl_value *result;
  lcl_value *tmp;

  if (argc < 2) {
    lcl_set_error(interp, "Expect::match-buffer requires buf and patterns");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &buf) != LCL_OK) {
    return LCL_RC_ERR;
  }

  patterns = argv[1];

  if (lcl_value_type_of(patterns) != LCL_LIST) {
    lcl_set_error(interp, "patterns must be a list");
    return LCL_RC_ERR;
  }

  num_patterns = lcl_list_len(patterns);

  for (i = 0; i < num_patterns; i++) {
    lcl_value *pat_val = NULL;
    expect_pattern *p;
    int pos;
    size_t end;

    if (lcl_list_get(patterns, i, &pat_val) != LCL_OK) {
      continue;
    }

    p = get_pattern(pat_val);

    if (!p) {
      const char *lit = lcl_value_to_string(pat_val);

      if (lit) {
        const char *found = strstr(buf, lit);

        if (found) {
          pos = (int)(found - buf);
          end = (size_t)pos + strlen(lit);

          if (best_pos < 0 || pos < best_pos) {
            best_pos = pos;
            best_idx = i;
            best_end = end;
          }
        }
      }

      lcl_ref_dec(pat_val);
      continue;
    }

    if (p->kind == EXPECT_PAT_TIMEOUT || p->kind == EXPECT_PAT_EOF) {
      lcl_ref_dec(pat_val);
      continue;
    }

    pos = match_pattern(p, buf, strlen(buf), &end);

    if (pos >= 0 && (best_pos < 0 || pos < best_pos)) {
      best_pos = pos;
      best_idx = i;
      best_end = end;
    }

    lcl_ref_dec(pat_val);
  }

  result = lcl_dict_new();

  if (best_pos >= 0) {
    char *before = NULL;
    char *match_str = NULL;
    char *after = NULL;

    before = (char *)malloc((size_t)best_pos + 1);

    if (before) {
      memcpy(before, buf, (size_t)best_pos);
      before[best_pos] = '\0';
    }

    match_str = (char *)malloc(best_end - (size_t)best_pos + 1);

    if (match_str) {
      memcpy(match_str, buf + best_pos, best_end - (size_t)best_pos);
      match_str[best_end - (size_t)best_pos] = '\0';
    }

    after = strdup(buf + best_end);

    tmp = lcl_int_new(1);
    lcl_dict_put(&result, "matched", tmp);
    lcl_ref_dec(tmp);

    tmp = lcl_int_new((long)best_idx);
    lcl_dict_put(&result, "index", tmp);
    lcl_ref_dec(tmp);

    tmp = lcl_string_new(before);
    lcl_dict_put(&result, "before", tmp);
    lcl_ref_dec(tmp);

    tmp = lcl_string_new(match_str);
    lcl_dict_put(&result, "match", tmp);
    lcl_ref_dec(tmp);

    tmp = lcl_string_new(after);
    lcl_dict_put(&result, "after", tmp);
    lcl_ref_dec(tmp);

    free(before);
    free(match_str);
    free(after);
  } else {
    tmp = lcl_int_new(0);
    lcl_dict_put(&result, "matched", tmp);
    lcl_ref_dec(tmp);

    tmp = lcl_int_new(-1);
    lcl_dict_put(&result, "index", tmp);
    lcl_ref_dec(tmp);
  }

  *out = result;

  return LCL_RC_OK;
}

/*
 * Expect::read-match handle patterns ?opts?
 *
 * Read from process handle until one of the patterns matches, timeout, or EOF.
 *
 * Options:
 *   timeout - ms to wait (default: 10000)
 *
 * Returns: #{matched 0/1 index N data "..." timeout 0/1 eof 0/1}
 */
static lcl_return_code c_expect_read_match(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  lcl_value *handle;
  lcl_value *patterns;
  lcl_value *opts = NULL;
  lcl_value *v = NULL;
  int timeout_ms = 10000;
  char *buf = NULL;
  size_t buf_len = 0;
  size_t buf_cap = 4096;
  int matched = 0;
  int timed_out = 0;
  int got_eof = 0;
  size_t matched_idx = 0;
  int elapsed_ms = 0;
  size_t num_patterns;
  int timeout_pattern_idx = -1;
  int eof_pattern_idx = -1;
  lcl_value *result;
  lcl_value *tmp;
  size_t i;

  if (argc < 2) {
    lcl_set_error(interp, "Expect::read-match requires handle and patterns");
    return LCL_RC_ERR;
  }

  handle = argv[0];
  patterns = argv[1];

  if (argc >= 3) {
    opts = argv[2];

    if (lcl_dict_get(opts, "timeout", &v) == LCL_OK) {
      long n;

      if (lcl_value_to_int(v, &n) == LCL_OK) {
        timeout_ms = (int)n;
      }
      lcl_ref_dec(v);
    }
  }

  if (lcl_value_type_of(patterns) != LCL_LIST) {
    lcl_set_error(interp, "patterns must be a list");
    return LCL_RC_ERR;
  }

  num_patterns = lcl_list_len(patterns);

  for (i = 0; i < num_patterns; i++) {
    lcl_value *pat_val = NULL;
    expect_pattern *p;

    if (lcl_list_get(patterns, i, &pat_val) != LCL_OK) {
      continue;
    }

    p = get_pattern(pat_val);

    if (p) {
      if (p->kind == EXPECT_PAT_TIMEOUT) {
        timeout_pattern_idx = (int)i;
      } else if (p->kind == EXPECT_PAT_EOF) {
        eof_pattern_idx = (int)i;
      }
    }
    lcl_ref_dec(pat_val);
  }

  buf = (char *)malloc(buf_cap);

  if (!buf) {
    return LCL_RC_ERR;
  }

  buf[0] = '\0';

  while (!matched && !timed_out && !got_eof) {
    lcl_value *read_args[2];
    lcl_value *read_opts;
    lcl_value *read_result = NULL;
    const char *chunk;
    int wait_ms;
    lcl_return_code rc;

    for (i = 0; i < num_patterns; i++) {
      lcl_value *pat_val = NULL;
      expect_pattern *p;
      int pos;
      size_t end;

      if (lcl_list_get(patterns, i, &pat_val) != LCL_OK) {
        continue;
      }

      p = get_pattern(pat_val);

      if (!p) {
        const char *lit = lcl_value_to_string(pat_val);

        if (lit && strstr(buf, lit)) {
          matched = 1;
          matched_idx = i;
          lcl_ref_dec(pat_val);
          break;
        }

        lcl_ref_dec(pat_val);
        continue;
      }

      if (p->kind == EXPECT_PAT_TIMEOUT || p->kind == EXPECT_PAT_EOF) {
        lcl_ref_dec(pat_val);
        continue;
      }

      pos = match_pattern(p, buf, buf_len, &end);

      if (pos >= 0) {
        matched = 1;
        matched_idx = i;
        lcl_ref_dec(pat_val);
        break;
      }

      lcl_ref_dec(pat_val);
    }

    if (matched) {
      break;
    }

    wait_ms = timeout_ms - elapsed_ms;

    if (wait_ms <= 0) {
      timed_out = 1;

      if (timeout_pattern_idx >= 0) {
        matched = 1;
        matched_idx = (size_t)timeout_pattern_idx;
      }

      break;
    }

    if (wait_ms > 100) {
      wait_ms = 100;
    }

    read_opts = lcl_dict_new();
    tmp = lcl_int_new(wait_ms);
    lcl_dict_put(&read_opts, "timeout", tmp);
    lcl_ref_dec(tmp);

    read_args[0] = handle;
    read_args[1] = read_opts;

    {
      lcl_value *read_proc = NULL;

      if (lcl_get(interp, "::Process::read", &read_proc) != LCL_OK) {
        lcl_ref_dec(read_opts);
        free(buf);
        lcl_set_error(interp, "Process::read not found");
        return LCL_RC_ERR;
      }

      rc = lcl_call_proc(interp, read_proc, 2, read_args, &read_result);
      lcl_ref_dec(read_proc);
      lcl_ref_dec(read_opts);

      if (rc != LCL_RC_OK) {
        free(buf);
        return rc;
      }
    }

    chunk = lcl_value_to_string(read_result);

    if (!chunk) {
      free(buf);
      lcl_ref_dec(read_result);
      lcl_set_error(interp, "out of memory");
      return LCL_RC_ERR;
    }

    if (strlen(chunk) == 0) {
      lcl_value *alive_proc = NULL;
      lcl_value *alive_result = NULL;
      long is_alive = 1;

      if (lcl_get(interp, "::Process::alive?", &alive_proc) == LCL_OK) {
        read_args[0] = handle;

        if (lcl_call_proc(interp, alive_proc, 1, read_args, &alive_result) ==
            LCL_RC_OK) {
          lcl_value_to_int(alive_result, &is_alive);
          lcl_ref_dec(alive_result);
        }
        lcl_ref_dec(alive_proc);
      }

      if (!is_alive) {
        got_eof = 1;

        if (eof_pattern_idx >= 0) {
          matched = 1;
          matched_idx = (size_t)eof_pattern_idx;
        }
      }

      elapsed_ms += wait_ms;
    } else {
      size_t chunk_len = strlen(chunk);

      if (buf_len + chunk_len + 1 > buf_cap) {
        size_t new_cap = buf_cap * 2;
        char *new_buf;

        while (new_cap < buf_len + chunk_len + 1) {
          new_cap *= 2;
        }

        new_buf = (char *)realloc(buf, new_cap);

        if (!new_buf) {
          free(buf);
          lcl_ref_dec(read_result);
          return LCL_RC_ERR;
        }

        buf = new_buf;
        buf_cap = new_cap;
      }

      memcpy(buf + buf_len, chunk, chunk_len);
      buf_len += chunk_len;
      buf[buf_len] = '\0';
    }

    if (read_result) {
      lcl_ref_dec(read_result);
    }
  }

  result = lcl_dict_new();

  tmp = lcl_int_new(matched);
  lcl_dict_put(&result, "matched", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_int_new((long)matched_idx);
  lcl_dict_put(&result, "index", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_string_new(buf);
  lcl_dict_put(&result, "data", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_int_new(timed_out);
  lcl_dict_put(&result, "timeout", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_int_new(got_eof);
  lcl_dict_put(&result, "eof", tmp);
  lcl_ref_dec(tmp);

  free(buf);
  *out = result;
  return LCL_RC_OK;
}

/*
 * Expect::match handle pairs ?opts?
 *
 * Pattern/handler matching with closures.
 *
 * pairs: list of (pattern handler) pairs
 *   pattern: string, pattern object, or timeout/eof sentinel
 *   handler: callable (proc/lambda) that receives match result dict
 *
 * Options:
 *   timeout - ms to wait (default: 10000)
 *
 * Returns: result of matched handler
 *
 * Example:
 *   Expect::match $h (
 *     ("password:" [lambda {m} { send-line "secret" }])
 *     ("$ "        [lambda {m} { $m }])
 *     (timeout     [lambda {} { error "timed out" }])
 *   )
 */
static lcl_return_code c_expect_match(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lcl_value *handle;
  lcl_value *pairs;
  lcl_value *opts = NULL;
  lcl_value *v = NULL;
  lcl_value *patterns;
  lcl_value **handlers = NULL;
  size_t num_pairs;
  size_t i;
  int timeout_ms = 10000;
  lcl_value *match_result = NULL;
  lcl_value *match_args[3];
  long matched_idx;
  lcl_value *handler;
  lcl_value *handler_args[1];
  lcl_return_code rc;
  lcl_value *tmp;

  if (argc < 2) {
    lcl_set_error(interp, "Expect::match requires handle and pairs");
    return LCL_RC_ERR;
  }

  handle = argv[0];
  pairs = argv[1];

  if (argc >= 3) {
    opts = argv[2];

    if (lcl_dict_get(opts, "timeout", &v) == LCL_OK) {
      long n;

      if (lcl_value_to_int(v, &n) == LCL_OK) {
        timeout_ms = (int)n;
      }

      lcl_ref_dec(v);
    }
  }

  if (lcl_value_type_of(pairs) != LCL_LIST) {
    lcl_set_error(interp, "pairs must be a list");
    return LCL_RC_ERR;
  }

  num_pairs = lcl_list_len(pairs);

  if (num_pairs == 0) {
    lcl_set_error(interp, "pairs list cannot be empty");
    return LCL_RC_ERR;
  }

  patterns = lcl_list_new();
  handlers = (lcl_value **)calloc(num_pairs, sizeof(lcl_value *));

  if (!handlers) {
    lcl_ref_dec(patterns);
    return LCL_RC_ERR;
  }

  for (i = 0; i < num_pairs; i++) {
    lcl_value *pair = NULL;
    lcl_value *pattern = NULL;
    lcl_value *hdlr = NULL;

    if (lcl_list_get(pairs, i, &pair) != LCL_OK) {
      continue;
    }

    if (lcl_value_type_of(pair) != LCL_LIST || lcl_list_len(pair) < 2) {
      lcl_ref_dec(pair);
      lcl_set_error(interp, "each pair must be a list of (pattern handler)");
      goto error;
    }

    lcl_list_get(pair, 0, &pattern);
    lcl_list_get(pair, 1, &hdlr);
    lcl_ref_dec(pair);

    lcl_list_push(&patterns, pattern);
    lcl_ref_dec(pattern);

    handlers[i] = hdlr;
  }

  match_args[0] = handle;
  match_args[1] = patterns;

  {
    lcl_value *match_opts = lcl_dict_new();
    tmp = lcl_int_new(timeout_ms);
    lcl_dict_put(&match_opts, "timeout", tmp);
    lcl_ref_dec(tmp);

    match_args[2] = match_opts;
    rc = c_expect_read_match(interp, 3, match_args, &match_result);
    lcl_ref_dec(match_opts);

    if (rc != LCL_RC_OK) {
      goto error;
    }
  }

  tmp = NULL;

  if (lcl_dict_get(match_result, "index", &tmp) != LCL_OK) {
    lcl_ref_dec(match_result);
    lcl_set_error(interp, "match result missing index");
    goto error;
  }

  if (lcl_value_to_int(tmp, &matched_idx) != LCL_OK || matched_idx < 0 ||
      (size_t)matched_idx >= num_pairs) {
    lcl_ref_dec(tmp);
    lcl_ref_dec(match_result);
    lcl_set_error(interp, "invalid match index");
    goto error;
  }

  lcl_ref_dec(tmp);

  handler = handlers[matched_idx];

  if (!handler || !lcl_is_callable(handler)) {
    lcl_ref_dec(match_result);
    lcl_set_error(interp, "handler is not callable");
    goto error;
  }

  handler_args[0] = match_result;
  rc = lcl_call_proc(interp, handler, 1, handler_args, out);
  lcl_ref_dec(match_result);

  lcl_ref_dec(patterns);

  for (i = 0; i < num_pairs; i++) {
    if (handlers[i]) {
      lcl_ref_dec(handlers[i]);
    }
  }

  free(handlers);

  return rc;

error:
  lcl_ref_dec(patterns);
  if (handlers) {
    for (i = 0; i < num_pairs; i++) {
      if (handlers[i]) {
        lcl_ref_dec(handlers[i]);
      }
    }

    free(handlers);
  }
  return LCL_RC_ERR;
}

/*
 * Expect::loop handle pairs ?opts?
 *
 * Loop with pattern/handler matching until break.
 *
 * Same as Expect::match but loops until:
 *   - A handler returns without calling 'continue'
 *   - A handler calls 'break'
 *   - No pattern matches (error)
 *
 * Handlers should call 'continue' to keep matching or 'break' to exit.
 * Normal return from handler also exits the loop.
 *
 * Example:
 *   Expect::loop $h (
 *     ("More--"    [lambda {m} { send " "; continue }])
 *     ("password:" [lambda {m} { send-line $pw; continue }])
 *     ("$ "        [lambda {m} { break }])
 *   )
 */
static lcl_return_code c_expect_loop(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_return_code rc;
  lcl_value *result = NULL;

  while (1) {
    if (result) {
      lcl_ref_dec(result);
      result = NULL;
    }

    rc = c_expect_match(interp, argc, argv, &result);

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }
    if (rc != LCL_RC_OK && rc != LCL_RC_BREAK) {
      if (result) {
        lcl_ref_dec(result);
      }
      return rc;
    }
    *out = result ? result : lcl_string_new("");
    return LCL_RC_OK;
  }
}

/*
 * Register the Expect:: namespace
 */
void lcl_register_expect(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new(EXPECT_NS);
  lcl_define_take(interp, EXPECT_NS, ns);

  lcl_ns_def_take(ns, "pattern",
                  lcl_c_proc_new("Expect::pattern", c_expect_pattern));
  lcl_ns_def_take(ns, "regex", lcl_c_proc_new("Expect::regex", c_expect_regex));
  lcl_ns_def_take(ns, "timeout",
                  lcl_c_proc_new("Expect::timeout", c_expect_timeout));
  lcl_ns_def_take(ns, "eof", lcl_c_proc_new("Expect::eof", c_expect_eof));
  lcl_ns_def_take(ns, "pattern?",
                  lcl_c_proc_new("Expect::pattern?", c_expect_is_pattern));
  lcl_ns_def_take(
      ns, "pattern-kind",
      lcl_c_proc_new("Expect::pattern-kind", c_expect_pattern_kind));
  lcl_ns_def_take(
      ns, "match-buffer",
      lcl_c_proc_new("Expect::match-buffer", c_expect_match_buffer));
  lcl_ns_def_take(ns, "read-match",
                  lcl_c_proc_new("Expect::read-match", c_expect_read_match));
  lcl_ns_def_take(ns, "match", lcl_c_proc_new("Expect::match", c_expect_match));
  lcl_ns_def_take(ns, "loop", lcl_c_proc_new("Expect::loop", c_expect_loop));
}
