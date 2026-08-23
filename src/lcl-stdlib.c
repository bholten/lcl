#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "lcl-stdlib-internal.h"

/* Bugfix: portable C89 overflow-checked arithmetic on long.  Each
 * returns 1 on success (writes *out), 0 on overflow. */
int lcl_std_safe_add_long(long a, long b, long *out) {
  if (b > 0 && a > LONG_MAX - b) {
    return 0;
  }

  if (b < 0 && a < LONG_MIN - b) {
    return 0;
  }

  *out = a + b;

  return 1;
}

int lcl_std_safe_sub_long(long a, long b, long *out) {
  /* a - LONG_MIN = a + 2^63. -LONG_MIN is not representable, so
   * handle separately via unsigned arithmetic when b == LONG_MIN. */
  if (b == LONG_MIN) {
    if (a >= 0) {
      return 0;
    }

    *out = (long)((unsigned long)a - (unsigned long)b);

    return 1;
  }

  if (b > 0 && a < LONG_MIN + b) {
    return 0;
  }

  if (b < 0 && a > LONG_MAX + b) {
    return 0;
  }

  *out = a - b;

  return 1;
}

/* Bugfix: Unsigned size_t add with overflow detection. Returns 0
 * (false) on overflow; result is left unmodified in that
 * case. C89-friendly: uses `(size_t)-1` for SIZE_MAX rather than
 * <stdint.h>'s SIZE_MAX. */
int lcl_std_safe_add_size(size_t a, size_t b, size_t *out) {
  if (b > (size_t)-1 - a) {
    return 0;
  }

  *out = a + b;

  return 1;
}

int lcl_std_safe_mul_long(long a, long b, long *out) {
  if (a == 0 || b == 0) {
    *out = 0;
    return 1;
  }

  /* |LONG_MIN| isn't representable, so it gets a dedicated path. */
  if (a == LONG_MIN) {
    if (b == 1) {
      *out = LONG_MIN;
      return 1;
    }
    return 0;
  }

  if (b == LONG_MIN) {
    if (a == 1) {
      *out = LONG_MIN;
      return 1;
    }
    return 0;
  }

  {
    long aa = a < 0 ? -a : a;
    long bb = b < 0 ? -b : b;
    if (aa > LONG_MAX / bb) {
      return 0;
    }
  }

  *out = a * b;
  return 1;
}

/* Arity guard. `max` < 0 means "at least min". Returns 1 when argc is
 * acceptable; otherwise records "name: expected ..., got argc" and
 * returns 0. */
int lcl_std_chk_argc(lcl_interp *interp, const char *name, int argc, int min,
                     int max) {
  char msg[160];

  if (argc >= min && (max < 0 || argc <= max)) {
    return 1;
  }

  if (max < 0) {
    snprintf(msg, sizeof(msg), "%.64s: expected at least %d argument%s, got %d",
             name, min, min == 1 ? "" : "s", argc);
  } else if (min == max) {
    snprintf(msg, sizeof(msg), "%.64s: expected %d argument%s, got %d", name,
             min, min == 1 ? "" : "s", argc);
  } else {
    snprintf(msg, sizeof(msg), "%.64s: expected %d to %d arguments, got %d",
             name, min, max, argc);
  }

  LCL_ERR_MSG_DUP(interp, msg);
  return 0;
}

/* Record "name: expected <expected>, got <actual>" where the actual
 * is the value's type name, or the (truncated) text of a string that
 * failed to parse as the expected kind. Always returns LCL_RC_ERR. */
lcl_return_code lcl_std_err_expected_got(lcl_interp *interp, const char *name,
                                         const char *expected, lcl_value *got) {
  char msg[192];

  if (got && got->type == LCL_STRING) {
    const char *s = lcl_value_to_string(got);
    snprintf(msg, sizeof(msg), "%.64s: expected %s, got \"%.48s\"", name,
             expected, s ? s : "");
  } else {
    snprintf(msg, sizeof(msg), "%.64s: expected %s, got %s", name, expected,
             got ? lcl_type_name(got->type) : "no value");
  }

  LCL_ERR_MSG_DUP(interp, msg);
  return LCL_RC_ERR;
}

/* "name: undefined variable \"var\"" -- for procs that look names up
 * in the environment themselves. Always returns LCL_RC_ERR. */
lcl_return_code lcl_std_err_undefined(lcl_interp *interp, const char *name,
                                      const char *var) {
  char msg[160];

  snprintf(msg, sizeof(msg), "%.48s: undefined variable \"%.64s\"", name, var);
  LCL_ERR_MSG_DUP(interp, msg);
  return LCL_RC_ERR;
}

/* Typed argument getters: like lcl_value_to_int/_to_float but record
 * a proc-naming diagnostic on failure. Return 1 on success, 0 on
 * error. */
int lcl_std_arg_int(lcl_interp *interp, const char *name, lcl_value *v,
                    long *out) {
  if (lcl_value_to_int(v, out) != LCL_OK) {
    lcl_std_err_expected_got(interp, name, "integer", v);
    return 0;
  }

  return 1;
}

int lcl_std_arg_float(lcl_interp *interp, const char *name, lcl_value *v,
                      double *out) {
  if (lcl_value_to_float(v, out) != LCL_OK) {
    lcl_std_err_expected_got(interp, name, "number", v);
    return 0;
  }

  return 1;
}

int lcl_std_arg_str(lcl_interp *interp, const char *name, lcl_value *v,
                    const char **out) {
  if (!v || v->type != LCL_STRING) {
    lcl_std_err_expected_got(interp, name,
                             "string (use String::from to render)", v);
    return 0;
  }

  return lcl_value_to_cstring(interp, v, out) == LCL_OK;
}

static lcl_return_code c_assert(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *expr;
  lcl_value *result = NULL;
  (void)out;

  if (!lcl_std_chk_argc(interp, "assert", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &expr) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_string(interp, expr, &result) != LCL_RC_OK) {
    if (result) {
      lcl_ref_dec(result);
    }
    return LCL_RC_ERR;
  }

  if (!lcl_value_is_true(result)) {
    lcl_ref_dec(result);

    if (argc >= 2) {
      const char *msg = lcl_value_to_string(argv[1]);
      LCL_ERR_MSG_DUP(interp, msg ? msg : "assertion failed");
    } else {
      LCL_ERR_MSG(interp, "assertion failed");
    }

    return LCL_RC_ERR;
  }

  lcl_ref_dec(result);

  return LCL_RC_OK;
}

static lcl_return_code c_puts(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  int i;

  for (i = 0; i < argc; i++) {
    const char *str;

    if (lcl_value_to_cstring(interp, argv[i], &str) != LCL_OK) {
      return LCL_RC_ERR;
    }

    fputs(str, stdout);

    if (i + 1 < argc) {
      fputc(' ', stdout);
    }
  }

  fputc('\n', stdout);
  fflush(stdout);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* type v - name of v's type tag ("string", "int", "list", ...) */
static lcl_return_code c_type(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "type", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(lcl_type_name(argv[0]->type));
  return LCL_RC_OK;
}

/* repr v - type-aware representation: strings quoted, lists (...),
 * dicts #{...}; distinguishes values that stringify identically */
static lcl_return_code c_repr(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  char *s;

  if (!lcl_std_chk_argc(interp, "repr", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  s = lcl_value_repr(argv[0]);

  if (!s) {
    LCL_ERR_MSG(interp, "out of memory");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(s);
  free(s);

  return LCL_RC_OK;
}

int lcl_std_all_args_integral(int argc, lcl_value **argv) {
  int i;

  for (i = 0; i < argc; i++) {
    long dummy;

    if (argv[i]->type == LCL_FLOAT) {
      return 0;
    }

    if (argv[i]->type == LCL_INT) {
      continue;
    }

    if (lcl_value_to_int(argv[i], &dummy) != LCL_OK) {
      return 0;
    }

    {
      const char *s = lcl_value_to_string(argv[i]);

      if (!s) {
        return 0;
      }

      while (*s) {
        if (*s == '.' || *s == 'e' || *s == 'E') {
          return 0;
        }

        s++;
      }
    }
  }

  return 1;
}

static int eq_cycle_guard_check(struct eq_cycle_guard *g, lcl_value *a,
                                lcl_value *b) {
  int i;

  for (i = 0; i < g->depth; i++) {
    if (g->a[i] == a && g->b[i] == b) {
      return 1;
    }

    if (g->a[i] == b && g->b[i] == a) {
      return 1;
    }
  }

  return 0;
}

static int eq_cycle_guard_push(struct eq_cycle_guard *g, lcl_value *a,
                               lcl_value *b) {
  if (g->depth >= EQ_STACK_MAX) {
    return 0;
  }

  g->a[g->depth] = a;
  g->b[g->depth] = b;
  g->depth++;

  return 1;
}

static void eq_cycle_guard_pop(struct eq_cycle_guard *g) {
  if (g->depth > 0) {
    g->depth--;
  }
}

static lcl_value *deref_once(lcl_value *v) {
  if (v && v->type == LCL_CELL) {
    return v->as.cell.inner;
  }

  return v;
}

static int list_equal_deep(lcl_value *a, lcl_value *b,
                           struct eq_cycle_guard *guard) {
  size_t len_a;
  size_t len_b;
  size_t i;
  lcl_value *elem_a;
  lcl_value *elem_b;
  int result;

  len_a = lcl_list_len(a);
  len_b = lcl_list_len(b);

  if (len_a != len_b) {
    return 0;
  }

  for (i = 0; i < len_a; i++) {
    if (lcl_list_get(a, i, &elem_a) != LCL_OK) {
      return 0;
    }

    if (lcl_list_get(b, i, &elem_b) != LCL_OK) {
      lcl_ref_dec(elem_a);
      return 0;
    }

    result = lcl_value_equal_deep(elem_a, elem_b, guard);

    lcl_ref_dec(elem_a);
    lcl_ref_dec(elem_b);

    if (!result) {
      return 0;
    }
  }

  return 1;
}

static int dict_equal_deep(lcl_value *a, lcl_value *b,
                           struct eq_cycle_guard *guard) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val_a;
  lcl_value *val_b;
  int result;

  if (lcl_dict_len(a) != lcl_dict_len(b)) {
    return 0;
  }

  while (hash_table_iterate(a->as.dict.dictionary, &it, &key, &val_a)) {
    if (lcl_dict_get(b, key, &val_b) != LCL_OK) {
      lcl_ref_dec(val_a);
      return 0;
    }

    result = lcl_value_equal_deep(val_a, val_b, guard);

    lcl_ref_dec(val_a);
    lcl_ref_dec(val_b);

    if (!result) {
      return 0;
    }
  }

  return 1;
}

/* check if a value can be interpreted as a number and get its double
 * value -- numeric value or numeric text */
int lcl_std_value_to_double(lcl_value *v, double *out) {
  return lcl_value_to_float(v, out) == LCL_OK;
}

/* Main deep equality function */
int lcl_value_equal_deep(lcl_value *a, lcl_value *b,
                         struct eq_cycle_guard *guard) {
  a = deref_once(a);
  b = deref_once(b);

  if (!a || !b) {
    return a == b;
  }

  if (a == b) {
    return 1;
  }

  if (eq_cycle_guard_check(guard, a, b)) {
    return 1;
  }

  {
    double da;
    double db;
    int a_is_num = lcl_std_value_to_double(a, &da);
    int b_is_num = lcl_std_value_to_double(b, &db);

    if (a_is_num && b_is_num) {
      return da == db;
    }
  }

  if (a->type != b->type) {
    return 0;
  }

  switch (a->type) {
  case LCL_STRING: {
    const char *sa = lcl_value_to_string(a);
    const char *sb = lcl_value_to_string(b);

    if (!sa || !sb) {
      return 0;
    }

    return strcmp(sa, sb) == 0;
  }

  case LCL_INT: return a->as.i == b->as.i;

  case LCL_FLOAT: return a->as.f == b->as.f;

  case LCL_LIST:
    if (!eq_cycle_guard_push(guard, a, b)) {
      return 0;
    }
    {
      int result = list_equal_deep(a, b, guard);
      eq_cycle_guard_pop(guard);
      return result;
    }

  case LCL_DICT:
    if (!eq_cycle_guard_push(guard, a, b)) {
      return 0;
    }
    {
      int result = dict_equal_deep(a, b, guard);
      eq_cycle_guard_pop(guard);
      return result;
    }

  case LCL_PROC:
  case LCL_CPROC:
  case LCL_NAMESPACE:
  case LCL_CELL: return a == b;

  default: return 0;
  }
}

static lcl_return_code c_eq(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)interp;

  if (!lcl_std_chk_argc(interp, "==", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lcl_value_equal_deep(argv[0], argv[1], &guard) ? 1 : 0);
  return LCL_RC_OK;
}

static lcl_return_code c_ne(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)interp;

  if (!lcl_std_chk_argc(interp, "!=", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lcl_value_equal_deep(argv[0], argv[1], &guard) ? 0 : 1);
  return LCL_RC_OK;
}

/* assert_eq actual expected ?msg?
 *
 * Asserts that actual == expected (deep equality).
 */
static lcl_return_code c_assert_eq(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)out;

  if (!lcl_std_chk_argc(interp, "assert_eq", argc, 2, -1)) {
    return LCL_RC_ERR;
  }

  if (!lcl_value_equal_deep(argv[0], argv[1], &guard)) {
    if (argc >= 3) {
      const char *msg = lcl_value_to_string(argv[2]);
      LCL_ERR_MSG_DUP(interp, msg ? msg : "assert_eq failed");
    } else {
      char buf[512];
      const char *expected = lcl_value_to_string(argv[1]);
      const char *actual = lcl_value_to_string(argv[0]);
      snprintf(buf, sizeof(buf), "expected '%s', got '%s'",
               expected ? expected : "<unstringifiable>",
               actual ? actual : "<unstringifiable>");
      LCL_ERR_MSG_DUP(interp, buf);
    }

    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/* assert_neq actual unexpected ?msg?
 *
 * Asserts that actual != unexpected (deep equality).
 */
static lcl_return_code c_assert_neq(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)out;

  if (!lcl_std_chk_argc(interp, "assert_neq", argc, 2, -1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_equal_deep(argv[0], argv[1], &guard)) {
    if (argc >= 3) {
      const char *msg = lcl_value_to_string(argv[2]);
      LCL_ERR_MSG_DUP(interp, msg ? msg : "assert_neq failed");
    } else {
      char buf[512];
      const char *expected = lcl_value_to_string(argv[1]);
      snprintf(buf, sizeof(buf), "expected value to not equal '%s'",
               expected ? expected : "<unstringifiable>");
      LCL_ERR_MSG_DUP(interp, buf);
    }
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/* same? : identity equality (no deref) */
static lcl_return_code c_same(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "same?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0] == argv[1] ? 1 : 0);
  return LCL_RC_OK;
}

/* not-same? : identity inequality */
static lcl_return_code c_not_same(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "not-same?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0] != argv[1] ? 1 : 0);
  return LCL_RC_OK;
}

/* Get a compiled program from a word. Uses the pre-compiled version
 * if available, otherwise compiles at runtime. Sets *owned=1 if the
 * caller must free the program, *owned=0 if the word owns it. */
lcl_return_code lcl_std_get_body_program(lcl_interp *interp, const lcl_word *w,
                                         const char *tag,
                                         lcl_program **prog_out, int *owned) {
  if (w->compiled) {
    *prog_out = w->compiled;
    *owned = 0;
    return LCL_RC_OK;
  }

  {
    lcl_value *body_v = NULL;

    const char *body_src;

    if (lcl_eval_word_to_str(interp, w, &body_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, body_v, &body_src) != LCL_OK) {
      lcl_ref_dec(body_v);
      return LCL_RC_ERR;
    }

    {
      char name[256];
      *prog_out = lcl_compile_report(
          interp, body_src,
          lcl_dyn_source_name(interp, tag, name, sizeof(name)));
    }
    lcl_ref_dec(body_v);

    if (!*prog_out) {
      return LCL_RC_ERR;
    }

    *owned = 1;
    return LCL_RC_OK;
  }
}

void lcl_std_free_if_owned(lcl_program *p, int owned) {
  if (owned) {
    lcl_program_free(p);
  }
}

/* check if a value is "truthy" (non-zero number or non-empty
   string) */
int lcl_value_is_true(lcl_value *v) {
  const char *s;
  long n;
  char *endptr;

  if (!v) {
    return 0;
  }

  if (v->type == LCL_INT) {
    return v->as.i != 0;
  }

  if (v->type == LCL_FLOAT) {
    return v->as.f != 0.0;
  }

  s = lcl_value_to_string(v);
  if (!s || *s == '\0') {
    return 0; /* empty string is false */
  }

  /* Falsy iff the string is integer-shaped numeric text equal to 0
   * ("0", "-0", "+0"). The grammar gate (not raw strtol) keeps libc's
   * whitespace skip out of truthiness: " 0" is truthy. Float-shaped
   * text ("0.0") stays truthy, as ever. */
  if (lcl_num_text_classify(s, strlen(s)) == LCL_NUM_INT) {
    n = strtol(s, &endptr, 10);

    if (*endptr == '\0') {
      return n != 0;
    }
  }

  return 1;
}

/* append n bytes to a dynamic buffer */
int lcl_std_buf_append(char **buf, size_t *len, size_t *cap, const char *s,
                       size_t n) {
  if (*len + n + 1 > *cap) {
    size_t newcap = (*cap == 0) ? 64 : *cap * 2;
    char *newbuf;

    while (newcap < *len + n + 1) {
      newcap *= 2;
    }

    newbuf = realloc(*buf, newcap);
    if (!newbuf) {
      return 0;
    }

    *buf = newbuf;
    *cap = newcap;
  }

  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';

  return 1;
}

int lcl_std_buf_append_char(char **buf, size_t *len, size_t *cap, char c) {
  return lcl_std_buf_append(buf, len, cap, &c, 1);
}

/* lcl_std_find_global_frame: walk to the root of a frame chain. Used
 * to anchor qualified-path root namespaces and require'd modules at
 * the shared global frame regardless of where the call site lives. */
lcl_frame *lcl_std_find_global_frame(lcl_frame *f) {
  if (!f) {
    return NULL;
  }

  for (;;) {
    if (f->parent) {
      f = f->parent;
    } else if (f->caller) {
      f = f->caller;
    } else {
      return f;
    }
  }
}

/* len x - returns length of list, dict, or string */
static lcl_return_code c_len(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "len", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST:
    *out = lcl_int_new((long)lcl_list_len(argv[0]));
    return LCL_RC_OK;

  case LCL_DICT:
    *out = lcl_int_new((long)lcl_dict_len(argv[0]));
    return LCL_RC_OK;

  case LCL_STRING: {
    const char *s;

    if (lcl_value_to_cstring(interp, argv[0], &s) != LCL_OK) {
      return LCL_RC_ERR;
    }

    *out = lcl_int_new((long)strlen(s));
    return LCL_RC_OK;
  }

  case LCL_NAMESPACE:
    *out = lcl_int_new((long)argv[0]->as.namespace.namespace->len);
    return LCL_RC_OK;

  default:
    return lcl_std_err_expected_got(
        interp, "len", "list, dict, string, or namespace", argv[0]);
  }
}

/* empty? x - returns 1 if container is empty */
static lcl_return_code c_empty(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "empty?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST:
    *out = lcl_int_new(lcl_list_len(argv[0]) == 0 ? 1 : 0);
    return LCL_RC_OK;

  case LCL_DICT:
    *out = lcl_int_new(lcl_dict_len(argv[0]) == 0 ? 1 : 0);
    return LCL_RC_OK;

  case LCL_STRING: {
    const char *s;

    if (lcl_value_to_cstring(interp, argv[0], &s) != LCL_OK) {
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(strlen(s) == 0 ? 1 : 0);
    return LCL_RC_OK;
  }

  case LCL_OPAQUE: *out = lcl_int_new(0); return LCL_RC_OK;

  default:
    return lcl_std_err_expected_got(interp, "empty?", "list, dict, or string",
                                    argv[0]);
  }
}

/* get x k [default] - get element by key/index */
static lcl_return_code c_generic_get(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "get", argc, 2, 3)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;

    if (!lcl_std_arg_int(interp, "get", argv[1], &idx)) {
      return LCL_RC_ERR;
    }

    if (lcl_list_get(argv[0], (size_t)idx, out) != LCL_OK) {
      char msg[96];

      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      snprintf(msg, sizeof(msg), "get: index %ld out of range", idx);
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }

    return LCL_RC_OK;
  }

  case LCL_DICT: {
    const char *key;

    if (lcl_value_to_cstring(interp, argv[1], &key) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_dict_get(argv[0], key, out) != LCL_OK) {
      char msg[160];

      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      snprintf(msg, sizeof(msg), "get: key \"%.96s\" not found", key);
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }

    return LCL_RC_OK;
  }

  case LCL_STRING: {
    long idx;
    const char *str;
    char buf[2];

    if (!lcl_std_arg_int(interp, "get", argv[1], &idx)) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, argv[0], &str) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (idx < 0 || (size_t)idx >= strlen(str)) {
      char msg[96];

      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      snprintf(msg, sizeof(msg), "get: index %ld out of range", idx);
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }

    buf[0] = str[idx];
    buf[1] = '\0';

    *out = lcl_string_new(buf);

    return LCL_RC_OK;
  }

  case LCL_NAMESPACE: {
    const char *name;
    lcl_value *val;

    if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_ns_get(argv[0], name, &val) != LCL_OK) {
      char msg[160];

      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      snprintf(msg, sizeof(msg), "get: binding \"%.96s\" not found", name);
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }

    /* var bindings are cells; deref so `get $ns n` matches `$ns::n` */
    if (val->type == LCL_CELL) {
      if (lcl_cell_get(val, out) != LCL_OK) {
        LCL_ERR_MSG(interp, "get: cell has been cleared");
        lcl_ref_dec(val);
        return LCL_RC_ERR;
      }

      lcl_ref_dec(val);
    } else {
      *out = val;
    }

    return LCL_RC_OK;
  }

  default:
    return lcl_std_err_expected_got(
        interp, "get", "list, dict, string, or namespace", argv[0]);
  }
}

/* put x k v - return new container with element added/replaced */
static lcl_return_code c_put(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "put", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;
    lcl_value *copy;

    if (!lcl_std_arg_int(interp, "put", argv[1], &idx)) {
      return LCL_RC_ERR;
    }

    copy = lcl_ref_inc(argv[0]);

    if (lcl_list_set(&copy, (size_t)idx, argv[2]) != LCL_OK) {
      char msg[96];

      snprintf(msg, sizeof(msg), "put: index %ld out of range", idx);
      LCL_ERR_MSG_DUP(interp, msg);
      lcl_ref_dec(copy);
      return LCL_RC_ERR;
    }

    *out = copy;

    return LCL_RC_OK;
  }

  case LCL_DICT: {
    const char *key;
    lcl_value *copy;

    if (lcl_value_to_cstring(interp, argv[1], &key) != LCL_OK) {
      return LCL_RC_ERR;
    }

    copy = lcl_ref_inc(argv[0]);

    if (lcl_dict_put(&copy, key, argv[2]) != LCL_OK) {
      LCL_ERR_MSG(interp, "put: out of memory");
      lcl_ref_dec(copy);
      return LCL_RC_ERR;
    }

    *out = copy;

    return LCL_RC_OK;
  }

  default:
    return lcl_std_err_expected_got(interp, "put", "list or dict", argv[0]);
  }
}

/* del x k - return new container without element: dict key (lenient)
 * or list index (strict, like get/put) */
static lcl_return_code c_del(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "del", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_DICT: {
    const char *key;
    lcl_value *copy;

    if (lcl_value_to_cstring(interp, argv[1], &key) != LCL_OK) {
      return LCL_RC_ERR;
    }

    copy = lcl_ref_inc(argv[0]);
    lcl_dict_del(&copy, key);

    *out = copy;

    return LCL_RC_OK;
  }

  case LCL_LIST: {
    long idx;
    size_t len;
    size_t i;
    lcl_value *result;

    if (!lcl_std_arg_int(interp, "del", argv[1], &idx)) {
      return LCL_RC_ERR;
    }

    len = lcl_list_len(argv[0]);

    if (idx < 0 || (size_t)idx >= len) {
      char msg[96];

      snprintf(msg, sizeof(msg), "del: index %ld out of range", idx);
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }

    result = lcl_list_new();

    if (!result) {
      LCL_ERR_MSG(interp, "del: out of memory");
      return LCL_RC_ERR;
    }

    for (i = 0; i < len; i++) {
      lcl_value *elem;

      if (i == (size_t)idx) {
        continue;
      }

      if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "del: internal error reading list");
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }

      if (lcl_list_push(&result, elem) != LCL_OK) {
        lcl_ref_dec(elem);
        lcl_ref_dec(result);
        LCL_ERR_MSG(interp, "del: out of memory");
        return LCL_RC_ERR;
      }

      lcl_ref_dec(elem);
    }

    *out = result;

    return LCL_RC_OK;
  }

  default:
    return lcl_std_err_expected_got(interp, "del", "list or dict", argv[0]);
  }
}

/* has? x k - membership test: list element, dict key, substring, or
 * namespace binding */
static lcl_return_code c_has(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "has?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    struct eq_cycle_guard guard = {{0}, {0}, 0};
    size_t i;
    size_t n = lcl_list_len(argv[0]);
    int found = 0;

    for (i = 0; i < n && !found; i++) {
      lcl_value *elem;

      if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "has?: failed to read list element");
        return LCL_RC_ERR;
      }

      found = lcl_value_equal_deep(elem, argv[1], &guard);
      lcl_ref_dec(elem);
    }

    *out = lcl_int_new(found ? 1 : 0);

    return LCL_RC_OK;
  }

  case LCL_STRING: {
    const char *haystack;
    const char *needle;

    if (lcl_value_to_cstring(interp, argv[0], &haystack) != LCL_OK ||
        lcl_value_to_cstring(interp, argv[1], &needle) != LCL_OK) {
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(strstr(haystack, needle) != NULL ? 1 : 0);

    return LCL_RC_OK;
  }

  case LCL_DICT: {
    const char *key;
    lcl_value *val;

    if (lcl_value_to_cstring(interp, argv[1], &key) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_dict_get(argv[0], key, &val) == LCL_OK) {
      lcl_ref_dec(val);
      *out = lcl_int_new(1);
    } else {
      *out = lcl_int_new(0);
    }

    return LCL_RC_OK;
  }

  case LCL_NAMESPACE: return lcl_std_ns_has(interp, argc, argv, out);

  default:
    return lcl_std_err_expected_got(
        interp, "has?", "list, dict, string, or namespace", argv[0]);
  }
}

static lcl_return_code c_is_opaque(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "opaque?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_OPAQUE ? 1 : 0);

  return LCL_RC_OK;
}

void lcl_register_core(lcl_interp *interp) {
  lcl_register_proc(interp, "assert", c_assert);
  lcl_register_proc(interp, "assert_eq", c_assert_eq);
  lcl_register_proc(interp, "assert_neq", c_assert_neq);
  lcl_register_proc(interp, "puts", c_puts);
  lcl_register_proc(interp, "==", c_eq);
  lcl_register_proc(interp, "!=", c_ne);
  lcl_register_proc(interp, "same?", c_same);
  lcl_register_proc(interp, "not-same?", c_not_same);
  lcl_register_proc(interp, "len", c_len);
  lcl_register_proc(interp, "empty?", c_empty);
  lcl_register_proc(interp, "get", c_generic_get);
  lcl_register_proc(interp, "put", c_put);
  lcl_register_proc(interp, "del", c_del);
  lcl_register_proc(interp, "has?", c_has);
  lcl_register_proc(interp, "opaque?", c_is_opaque);
  lcl_register_proc(interp, "type", c_type);
  lcl_register_proc(interp, "repr", c_repr);

  lcl_std_register_num(interp);
  lcl_std_register_control(interp);
  lcl_std_register_binding(interp);
  lcl_std_register_ns(interp);
  lcl_std_register_module(interp);
  lcl_std_register_eval(interp);
  lcl_std_register_list(interp);
  lcl_std_register_dict(interp);
  lcl_std_register_string(interp);
  lcl_std_register_lex(interp);
}
