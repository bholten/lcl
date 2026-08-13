#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-path.h"
#include "lcl-values.h"

#include "lcl-stdlib.h"

/* Bugfix: portable C89 overflow-checked arithmetic on long.  Each
 * returns 1 on success (writes *out), 0 on overflow. */
static int safe_add_long(long a, long b, long *out) {
  if (b > 0 && a > LONG_MAX - b) {
    return 0;
  }

  if (b < 0 && a < LONG_MIN - b) {
    return 0;
  }

  *out = a + b;

  return 1;
}

static int safe_sub_long(long a, long b, long *out) {
  /* a - LONG_MIN = a + 2^63. -LONG_MIN is not representable, so handle
   * separately via unsigned arithmetic when b == LONG_MIN. */
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
static int safe_add_size(size_t a, size_t b, size_t *out) {
  if (b > (size_t)-1 - a) {
    return 0;
  }

  *out = a + b;

  return 1;
}

static int safe_mul_long(long a, long b, long *out) {
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

lcl_result lcl_register_proc(lcl_interp *interp, const char *name,
                             lcl_c_proc_fn fn);

lcl_result lcl_register_spec(lcl_interp *interp, const char *name,
                             lcl_c_spec_fn fn);

lcl_result lcl_define(lcl_interp *interp, const char *name, lcl_value *value);

lcl_result lcl_define_take(lcl_interp *interp, const char *name,
                           lcl_value *value);

int lcl_is_callable(lcl_value *value);

lcl_return_code lcl_call_proc(lcl_interp *interp, lcl_value *proc, int argc,
                              lcl_value **argv, lcl_value **out);

lcl_value *lcl_list_new_from_cwords(const char *words);

static int lcl_value_is_true(lcl_value *v);

/* ---------------------------------------------------------------------------
 * Shared diagnostics (#68): every stdlib failure names the proc and
 * states expected vs got, matching the user-proc arity format from
 * #62 ("name: expected X, got Y").
 * ---------------------------------------------------------------------------
 */

/* Arity guard. `max` < 0 means "at least min". Returns 1 when argc is
 * acceptable; otherwise records "name: expected ..., got argc" and
 * returns 0. */
static int chk_argc(lcl_interp *interp, const char *name, int argc, int min,
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

/* Record "name: expected <expected>, got <actual>" where the actual is
 * the value's type name, or the (truncated) text of a string that
 * failed to parse as the expected kind. Always returns LCL_RC_ERR. */
static int err_expected_got(lcl_interp *interp, const char *name,
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
static int err_undefined(lcl_interp *interp, const char *name,
                         const char *var) {
  char msg[160];

  snprintf(msg, sizeof(msg), "%.48s: undefined variable \"%.64s\"", name, var);
  LCL_ERR_MSG_DUP(interp, msg);
  return LCL_RC_ERR;
}

/* Typed argument getters: like lcl_value_to_int/_to_float but record a
 * proc-naming diagnostic on failure. Return 1 on success, 0 on error. */
static int arg_int(lcl_interp *interp, const char *name, lcl_value *v,
                   long *out) {
  if (lcl_value_to_int(v, out) != LCL_OK) {
    err_expected_got(interp, name, "integer", v);
    return 0;
  }

  return 1;
}

static int arg_float(lcl_interp *interp, const char *name, lcl_value *v,
                     double *out) {
  if (lcl_value_to_float(v, out) != LCL_OK) {
    err_expected_got(interp, name, "number", v);
    return 0;
  }

  return 1;
}

static int arg_str(lcl_interp *interp, const char *name, lcl_value *v,
                   const char **out) {
  if (!v || v->type != LCL_STRING) {
    err_expected_got(interp, name, "string (use String::from to render)", v);
    return 0;
  }

  return lcl_value_to_cstring(interp, v, out) == LCL_OK;
}

static int c_assert(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  const char *expr;
  lcl_value *result = NULL;
  (void)out;

  if (!chk_argc(interp, "assert", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &expr) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_string(interp, expr, &result) != LCL_OK) {
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

static int c_puts(lcl_interp *interp, int argc, lcl_value **argv,
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

/* and / or - short-circuit logical special forms (spec §8).
 *
 * Operands are evaluated left to right through lcl_eval_word -- the
 * same per-word machinery ordinary argument evaluation uses -- but
 * conditionally: evaluation stops at the deciding operand, so later
 * operands can rely on guards (`and [in-bounds ...] [index ...]`).
 *
 * Value-returning: `and` yields the first falsy operand (else the
 * final operand); `or` yields the first truthy operand (else the
 * final operand). Identities: [and] -> 1, [or] -> 0.
 *
 * `@` spread is rejected up front: spread operands are already
 * evaluated, so laziness buys nothing -- List::all?/List::any? cover
 * evaluated collections. */
static int and_or_impl(lcl_interp *interp, int argc, const lcl_word **args,
                       lcl_value **out, int stop_when_truthy, long identity,
                       const char *name) {
  lcl_value *val = NULL;
  int i;

  for (i = 0; i < argc; i++) {
    if (args[i]->expand) {
      char msg[96];
      snprintf(msg, sizeof(msg), "%s: @ spread is not supported; use List::%s?",
               name, stop_when_truthy ? "any" : "all");
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }
  }

  if (argc == 0) {
    *out = lcl_int_new(identity);
    return LCL_RC_OK;
  }

  for (i = 0; i < argc; i++) {
    lcl_ref_dec(val);
    val = NULL;

    if (lcl_eval_word(interp, args[i], &val) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_is_true(val) == stop_when_truthy) {
      break;
    }
  }

  *out = val;
  return LCL_RC_OK;
}

static int s_and(lcl_interp *interp, int argc, const lcl_word **args,
                 lcl_value **out) {
  return and_or_impl(interp, argc, args, out, 0, 1, "and");
}

static int s_or(lcl_interp *interp, int argc, const lcl_word **args,
                lcl_value **out) {
  return and_or_impl(interp, argc, args, out, 1, 0, "or");
}

static int c_not(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  int b;

  if (!chk_argc(interp, "not", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  b = !lcl_value_is_true(argv[0]);

  *out = lcl_int_new(b);
  return LCL_RC_OK;
}

/* type v - name of v's type tag ("string", "int", "list", ...) */
static int c_type(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  if (!chk_argc(interp, "type", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(lcl_type_name(argv[0]->type));
  return LCL_RC_OK;
}

/* repr v - type-aware representation: strings quoted, lists (...),
 * dicts #{...}; distinguishes values that stringify identically */
static int c_repr(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  char *s;

  if (!chk_argc(interp, "repr", argc, 1, 1)) {
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

/* String::from v - explicit conversion to v's canonical string */
static int c_string_from(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  const char *s;

  if (!chk_argc(interp, "String::from", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &s) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(s);
  return LCL_RC_OK;
}

static int all_args_integral(int argc, lcl_value **argv) {
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

static int c_add(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  int i;
  (void)interp;

  if (all_args_integral(argc, argv)) {
    long sum = 0;

    for (i = 0; i < argc; i++) {
      long v;
      lcl_value_to_int(argv[i], &v);

      if (!safe_add_long(sum, v, &sum)) {
        LCL_ERR_MSG(interp, "integer overflow in +");

        return LCL_RC_ERR;
      }
    }

    *out = lcl_int_new(sum);
  } else {
    double sum = 0.0;

    for (i = 0; i < argc; i++) {
      double v;

      if (!arg_float(interp, "+", argv[i], &v)) {
        return LCL_RC_ERR;
      }

      sum += v;
    }

    *out = lcl_float_new(sum);
  }

  return LCL_RC_OK;
}

static int c_sub(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  int i;

  if (!chk_argc(interp, "-", argc, 2, -1)) {
    return LCL_RC_ERR;
  }

  if (all_args_integral(argc, argv)) {
    long result;
    lcl_value_to_int(argv[0], &result);

    for (i = 1; i < argc; i++) {
      long v;
      lcl_value_to_int(argv[i], &v);

      if (!safe_sub_long(result, v, &result)) {
        LCL_ERR_MSG(interp, "integer overflow in -");

        return LCL_RC_ERR;
      }
    }

    *out = lcl_int_new(result);
  } else {
    double result;

    if (!arg_float(interp, "-", argv[0], &result)) {
      return LCL_RC_ERR;
    }

    for (i = 1; i < argc; i++) {
      double v;

      if (!arg_float(interp, "-", argv[i], &v)) {
        return LCL_RC_ERR;
      }

      result -= v;
    }

    *out = lcl_float_new(result);
  }

  return LCL_RC_OK;
}

static int c_mult(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  int i;
  (void)interp;

  if (all_args_integral(argc, argv)) {
    long product = 1;

    for (i = 0; i < argc; i++) {
      long v;
      lcl_value_to_int(argv[i], &v);

      if (!safe_mul_long(product, v, &product)) {
        LCL_ERR_MSG(interp, "integer overflow in *");
        return LCL_RC_ERR;
      }
    }

    *out = lcl_int_new(product);
  } else {
    double product = 1.0;

    for (i = 0; i < argc; i++) {
      double v;

      if (!arg_float(interp, "*", argv[i], &v)) {
        return LCL_RC_ERR;
      }

      product *= v;
    }

    *out = lcl_float_new(product);
  }

  return LCL_RC_OK;
}

static int c_div(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  double result;
  double numerator;
  double divisor;

  if (!chk_argc(interp, "/", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!arg_float(interp, "/", argv[0], &numerator)) {
    return LCL_RC_ERR;
  }

  if (!arg_float(interp, "/", argv[1], &divisor)) {
    return LCL_RC_ERR;
  }

  if (divisor == 0.0) {
    LCL_ERR_MSG(interp, "division by zero");
    return LCL_RC_ERR;
  }

  result = numerator / divisor;

  *out = lcl_float_new(result);

  return LCL_RC_OK;
}

static int c_mod(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  long result;
  long dividend;
  long divisor;

  if (!chk_argc(interp, "%", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!arg_int(interp, "%", argv[0], &dividend)) {
    return LCL_RC_ERR;
  }

  if (!arg_int(interp, "%", argv[1], &divisor)) {
    return LCL_RC_ERR;
  }

  if (divisor == 0) {
    LCL_ERR_MSG(interp, "division by zero");
    return LCL_RC_ERR;
  }

  /* Bugfix: LONG_MIN % -1 is UB on most platforms (the quotient
   * LONG_MIN/-1 = -LONG_MIN overflows). The mathematical remainder is
   * 0. */
  if (dividend == LONG_MIN && divisor == -1) {
    *out = lcl_int_new(0);
    return LCL_RC_OK;
  }

  result = dividend % divisor;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
}

/* Bugfix: Compare two values numerically. When both operands are
 * integral, compare as `long` to preserve precision near LONG_MAX;
 * otherwise fall back to double comparison. `op` selects <, <=, >,
 * >=. */
static int c_compare(lcl_interp *interp, const char *name, int argc,
                     lcl_value **argv, int op, lcl_value **out) {
  double left;
  double right;
  long result;

  if (!chk_argc(interp, name, argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (all_args_integral(argc, argv)) {
    long li;
    long ri;

    if (!arg_int(interp, name, argv[0], &li) ||
        !arg_int(interp, name, argv[1], &ri)) {
      return LCL_RC_ERR;
    }

    switch (op) {
    case 0: result = (li < ri); break;
    case 1: result = (li <= ri); break;
    case 2: result = (li > ri); break;
    case 3: result = (li >= ri); break;
    default:
      LCL_ERR_MSG(interp, "internal error: bad comparison op");
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(result);
    return LCL_RC_OK;
  }

  if (!arg_float(interp, name, argv[0], &left) ||
      !arg_float(interp, name, argv[1], &right)) {
    return LCL_RC_ERR;
  }

  switch (op) {
  case 0: result = (left < right); break;
  case 1: result = (left <= right); break;
  case 2: result = (left > right); break;
  case 3: result = (left >= right); break;
  default:
    LCL_ERR_MSG(interp, "internal error: bad comparison op");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(result);
  return LCL_RC_OK;
}

static int c_lt(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  return c_compare(interp, "<", argc, argv, 0, out);
}

static int c_lte(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  return c_compare(interp, "<=", argc, argv, 1, out);
}

static int c_gt(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  return c_compare(interp, ">", argc, argv, 2, out);
}

static int c_gte(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  return c_compare(interp, ">=", argc, argv, 3, out);
}

#define EQ_STACK_MAX 256

struct eq_cycle_guard {
  lcl_value *a[EQ_STACK_MAX];
  lcl_value *b[EQ_STACK_MAX];
  int depth;
};

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

static int lcl_value_equal_deep(lcl_value *a, lcl_value *b,
                                struct eq_cycle_guard *guard);

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
static int value_to_double(lcl_value *v, double *out) {
  return lcl_value_to_float(v, out) == LCL_OK;
}

/* Main deep equality function */
static int lcl_value_equal_deep(lcl_value *a, lcl_value *b,
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
    int a_is_num = value_to_double(a, &da);
    int b_is_num = value_to_double(b, &db);

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

static int c_eq(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)interp;

  if (!chk_argc(interp, "==", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lcl_value_equal_deep(argv[0], argv[1], &guard) ? 1 : 0);
  return LCL_RC_OK;
}

static int c_ne(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)interp;

  if (!chk_argc(interp, "!=", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lcl_value_equal_deep(argv[0], argv[1], &guard) ? 0 : 1);
  return LCL_RC_OK;
}

/* assert_eq actual expected ?msg?
 * Asserts that actual == expected (deep equality).
 */
static int c_assert_eq(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)out;

  if (!chk_argc(interp, "assert_eq", argc, 2, -1)) {
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
 * Asserts that actual != unexpected (deep equality).
 */
static int c_assert_neq(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)out;

  if (!chk_argc(interp, "assert_neq", argc, 2, -1)) {
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
static int c_same(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "same?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0] == argv[1] ? 1 : 0);
  return LCL_RC_OK;
}

/* not-same? : identity inequality */
static int c_not_same(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "not-same?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0] != argv[1] ? 1 : 0);
  return LCL_RC_OK;
}

/* cell? : check if value is a cell */
static int c_is_cell(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "cell?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_CELL ? 1 : 0);
  return LCL_RC_OK;
}

/* binding-cell name : returns the cell object for a binding (special form) */
static int s_binding_cell(lcl_interp *interp, int argc, const lcl_word **args,
                          lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *binding = NULL;
  const char *name;

  if (!chk_argc(interp, "binding-cell", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name, &binding) != LCL_OK) {
    err_undefined(interp, "binding-cell", name);
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);

  if (binding->type != LCL_CELL) {
    err_expected_got(interp, "binding-cell", "cell binding", binding);
    lcl_ref_dec(binding);
    return LCL_RC_ERR;
  }

  *out = binding;
  return LCL_RC_OK;
}

/* same-binding? name1 name2 : check if two bindings refer to the same cell */
static int s_same_binding(lcl_interp *interp, int argc, const lcl_word **args,
                          lcl_value **out) {
  lcl_value *name1_v = NULL;
  lcl_value *name2_v = NULL;
  lcl_value *binding1 = NULL;
  lcl_value *binding2 = NULL;
  const char *name1;
  const char *name2;
  int same;

  if (!chk_argc(interp, "same-binding?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &name1_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &name2_v) != LCL_RC_OK) {
    lcl_ref_dec(name1_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name1_v, &name1) != LCL_OK) {
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name2_v, &name2) != LCL_OK) {
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name1, &binding1) != LCL_OK) {
    err_undefined(interp, "same-binding?", name1);
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name2, &binding2) != LCL_OK) {
    err_undefined(interp, "same-binding?", name2);
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    lcl_ref_dec(binding1);
    return LCL_RC_ERR;
  }

  if (binding1->type != LCL_CELL || binding2->type != LCL_CELL) {
    err_expected_got(interp, "same-binding?", "cell binding",
                     binding1->type != LCL_CELL ? binding1 : binding2);
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    lcl_ref_dec(binding1);
    lcl_ref_dec(binding2);
    return LCL_RC_ERR;
  }

  same = (binding1 == binding2) ? 1 : 0;

  lcl_ref_dec(name1_v);
  lcl_ref_dec(name2_v);
  lcl_ref_dec(binding1);
  lcl_ref_dec(binding2);

  *out = lcl_int_new(same);
  return LCL_RC_OK;
}

static int c_let(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  const char *name;

  if (!chk_argc(interp, "let", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (strstr(name, "::") && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "let: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::def'");
    return LCL_RC_ERR;
  }

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_bind(interp, name, argv[1]) != LCL_OK) {
      LCL_ERR_MSG(interp, "let: out of memory");
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name, argv[1]) != LCL_OK) {
      LCL_ERR_MSG(interp, "let: out of memory");
      return LCL_RC_ERR;
    }
  }

  *out = lcl_ref_inc(argv[1]);

  return LCL_RC_OK;
}

/* gensym ?prefix? - generate a unique symbol name */
static int c_gensym(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  const char *prefix = "_G";
  char buf[128];
  int n;

  if (!chk_argc(interp, "gensym", argc, 0, 1)) {
    return LCL_RC_ERR;
  }

  if (argc == 1) {
    if (lcl_value_to_cstring(interp, argv[0], &prefix) != LCL_OK) {
      return LCL_RC_ERR;
    }
    /* Bugfix: reject overlong prefixes before they silently
     * truncate. Truncation would produce non-unique names (e.g. two
     * different long prefixes that share their first ~100 chars would
     * collide), defeating gensym's contract. Cap conservatively at 96
     * bytes -- leaves room for a 20-digit `unsigned long` plus the
     * NUL. */
    if (strlen(prefix) > 96) {
      LCL_ERR_MSG(interp, "gensym: prefix too long (max 96 bytes)");
      return LCL_RC_ERR;
    }
  }

  /* Bugfix: counter is `unsigned long` to avoid signed-overflow
   * UB on extremely long-running interpreters. Format with `%lu`. */
  interp->gensym_counter++;
  n = snprintf(buf, sizeof(buf), "%s%lu", prefix, interp->gensym_counter);
  if (n < 0 || (size_t)n >= sizeof(buf)) {
    LCL_ERR_MSG(interp, "gensym: name too long");
    return LCL_RC_ERR;
  }
  *out = lcl_string_new(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_ref(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "ref", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_cell_new(argv[0]);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_get(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  lcl_value *val = NULL;
  const char *name;

  if (!chk_argc(interp, "getvar", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name, &val) != LCL_OK) {
    return err_undefined(interp, "getvar", name);
  }

  if (val->type == LCL_CELL) {
    if (lcl_cell_get(val, out) != LCL_OK) {
      LCL_ERR_MSG(interp, "getvar: cell has been cleared");
      lcl_ref_dec(val);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(val);
  } else {
    *out = val;
  }

  return LCL_RC_OK;
}

static int s_set_bang(lcl_interp *interp, int argc, const lcl_word **args,
                      lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *val_v = NULL;
  lcl_value *cell = NULL;
  const char *name_str;

  if (!chk_argc(interp, "set!", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &val_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    return LCL_RC_ERR;
  }

  /* Look up the cell to check for cycles before mutating. If the
   * value is a proc that captures this cell, assigning it would
   * create a reference cycle that can never be freed. */
  if (lcl_env_get_value(interp, name_str, &cell) == LCL_OK) {
    if (cell->type == LCL_CELL && lcl_cell_would_cycle(cell, val_v)) {
      LCL_ERR_MSG(interp, "assignment would create reference cycle "
                          "(mutual recursion not allowed)");
      lcl_ref_dec(cell);
      lcl_ref_dec(name_v);
      lcl_ref_dec(val_v);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(cell);
  }

  if (lcl_env_set_bang(&interp->env, name_str, val_v) != LCL_OK) {
    lcl_value *probe = NULL;

    if (lcl_env_get_value(interp, name_str, &probe) != LCL_OK) {
      err_undefined(interp, "set!", name_str);
    } else {
      err_expected_got(interp, "set!", "cell (declare with 'var')", probe);
      lcl_ref_dec(probe);
    }

    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);

    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  *out = lcl_ref_inc(val_v);
  lcl_ref_dec(val_v);

  return LCL_RC_OK;
}

static int s_var(lcl_interp *interp, int argc, const lcl_word **argv,
                 lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *init_v = NULL;
  const char *name_str;

  if (!chk_argc(interp, "var", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, argv[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (strstr(name_str, "::") && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "var: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::def'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, argv[1], &init_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_var(interp, name_str, init_v) != LCL_OK) {
      LCL_ERR_MSG(interp, "var: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(init_v);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_var(&interp->env, name_str, init_v) != LCL_OK) {
      LCL_ERR_MSG(interp, "var: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(init_v);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(init_v);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

static int s_return(lcl_interp *interp, int argc, const lcl_word **args,
                    lcl_value **out) {
  int rc;

  if (argc == 0) {
    *out = lcl_string_new("");
    return LCL_RC_RETURN;
  }

  rc = lcl_eval_word(interp, args[0], out);

  /* Bugfix: Propagate TAILCALL for self-recursive returns */
  if (rc == LCL_RC_TAILCALL) {
    return LCL_RC_TAILCALL;
  }

  if (rc == LCL_RC_OK) {
    return LCL_RC_RETURN;
  }

  return LCL_RC_ERR;
}

/* break - exit from innermost loop */
static int s_break(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  (void)interp;
  (void)args;

  if (!chk_argc(interp, "break", argc, 0, 0)) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_BREAK;
}

/* continue - skip to next iteration of innermost loop */
static int s_continue(lcl_interp *interp, int argc, const lcl_word **args,
                      lcl_value **out) {
  (void)interp;
  (void)args;

  if (!chk_argc(interp, "continue", argc, 0, 0)) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_CONTINUE;
}

/* error - throw an error with the given message */
int c_error(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  const char *msg;

  (void)out;

  if (!chk_argc(interp, "error", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  msg = lcl_value_to_string(argv[0]);
  LCL_ERR_MSG_DUP(interp, msg ? msg : "error");

  return LCL_RC_ERR;
}

/* Get a compiled program from a word. Uses the pre-compiled version if
 * available, otherwise compiles at runtime. Sets *owned=1 if the caller
 * must free the program, *owned=0 if the word owns it. */
static int get_body_program(lcl_interp *interp, const lcl_word *w,
                            const char *tag, lcl_program **prog_out,
                            int *owned) {
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

    *prog_out = lcl_compile_report(interp, body_src, tag);
    lcl_ref_dec(body_v);

    if (!*prog_out) {
      return LCL_RC_ERR;
    }

    *owned = 1;
    return LCL_RC_OK;
  }
}

static void free_if_owned(lcl_program *p, int owned) {
  if (owned) {
    lcl_program_free(p);
  }
}

/* catch - execute script and catch errors
 * Usage: catch script ?resultVar? ?errorVar?
 * Returns: 0 if script succeeded, 1 if error occurred
 */
static int c_catch(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  lcl_program *prog = NULL;
  int prog_owned = 0;
  lcl_value *result = NULL;
  char *result_var = NULL;
  char *error_var = NULL;
  int rc;

  if (!chk_argc(interp, "catch", argc, 1, 3)) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    lcl_value *rv = NULL;
    const char *rv_s;

    if (lcl_eval_word_to_str(interp, args[1], &rv) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, rv, &rv_s) != LCL_OK) {
      lcl_ref_dec(rv);
      return LCL_RC_ERR;
    }

    result_var = strdup(rv_s);
    lcl_ref_dec(rv);

    if (!result_var) {
      LCL_ERR_MSG(interp, "catch: out of memory");
      return LCL_RC_ERR;
    }
  }

  if (argc >= 3) {
    lcl_value *ev = NULL;
    const char *ev_s;

    if (lcl_eval_word_to_str(interp, args[2], &ev) != LCL_RC_OK) {
      free(result_var);
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, ev, &ev_s) != LCL_OK) {
      free(result_var);
      lcl_ref_dec(ev);
      return LCL_RC_ERR;
    }

    error_var = strdup(ev_s);
    lcl_ref_dec(ev);

    if (!error_var) {
      free(result_var);
      LCL_ERR_MSG(interp, "catch: out of memory");
      return LCL_RC_ERR;
    }
  }

  /* Compile the script here rather than via get_body_program: a
   * substitution failure while producing the script text propagates
   * (the argument never became a script), but a script that fails to
   * compile is a failure of the caught script and takes the
   * caught-error path below. */
  if (args[0]->compiled) {
    prog = args[0]->compiled;
    prog_owned = 0;
  } else {
    lcl_value *body_v = NULL;
    const char *body_src;

    if (lcl_eval_word_to_str(interp, args[0], &body_v) != LCL_RC_OK) {
      free(result_var);
      free(error_var);
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, body_v, &body_src) != LCL_OK) {
      lcl_ref_dec(body_v);
      free(result_var);
      free(error_var);
      return LCL_RC_ERR;
    }

    prog = lcl_compile_report(interp, body_src, "<catch>");
    lcl_ref_dec(body_v);

    if (prog) {
      prog_owned = 1;
    }
  }

  if (!prog) {
    /* compile error already recorded by lcl_compile_report */
    rc = LCL_RC_ERR;
  } else {
    rc = lcl_eval_program(interp, prog, &result);
    free_if_owned(prog, prog_owned);
  }

  if (rc == LCL_RC_ERR) {
    if (error_var) {
      const char *err_msg = interp->err_msg ? interp->err_msg : "unknown error";
      lcl_value *err_v = lcl_string_new(err_msg);
      lcl_define(interp, error_var, err_v);
      lcl_ref_dec(err_v);
    }

    if (result_var) {
      lcl_value *empty = lcl_string_new("");
      lcl_define(interp, result_var, empty);
      lcl_ref_dec(empty);
    }

    LCL_ERR_CLEAR(interp);

    if (result) {
      lcl_ref_dec(result);
    }

    free(result_var);
    free(error_var);

    *out = lcl_int_new(1);
    return LCL_RC_OK;

  } else if (rc == LCL_RC_OK) {
    if (result_var && result) {
      lcl_define(interp, result_var, result);
    }

    if (error_var) {
      lcl_value *empty = lcl_string_new("");
      lcl_define(interp, error_var, empty);
      lcl_ref_dec(empty);
    }

    if (result) {
      lcl_ref_dec(result);
    }

    free(result_var);
    free(error_var);

    *out = lcl_int_new(0);
    return LCL_RC_OK;

  } else {
    free(result_var);
    free(error_var);
    *out = result;
    return rc;
  }
}

/* check if a value is "truthy" (non-zero number or non-empty string) */
static int lcl_value_is_true(lcl_value *v) {
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

/* if condition {then-body} [else] [{else-body}]
 *
 * Simple two-branch conditional:
 * - condition is evaluated (can be [expr], $var, or literal)
 * - then-body executes if condition is truthy
 * - else-body (optional) executes if condition is falsy
 * - The 'else' keyword is optional for readability
 *
 * Valid forms:
 *   if [cond] {then}                 - no else branch
 *   if [cond] {then} {else}          - positional else
 *   if [cond] {then} else {else}     - with else keyword
 */
int s_if(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  int saved_tail_position = interp->in_tail_position;
  lcl_value *cond_v = NULL;
  lcl_value *body_v = NULL;
  lcl_program *body_p = NULL;
  int is_true;
  int rc;
  int body_idx;
  int else_body_idx = -1;

  if (!chk_argc(interp, "if", argc, 2, 4)) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    else_body_idx = 2;
  } else if (argc == 4) {
    /* Bugfix: 4-arg form requires the literal keyword `else` at
       args[2]. */
    const lcl_word *kw = args[2];

    if (kw->np != 1 || kw->wp[0].kind != LCL_WP_LIT ||
        strcmp(kw->wp[0].as.lit.s, "else") != 0) {
      LCL_ERR_MSG(interp, "if: expected 'else' between the two branches");
      return LCL_RC_ERR;
    }

    else_body_idx = 3;
  }

  interp->in_tail_position = 0;

  if (lcl_eval_word(interp, args[0], &cond_v) != LCL_RC_OK) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  is_true = lcl_value_is_true(cond_v);
  lcl_ref_dec(cond_v);

  if (is_true) {
    body_idx = 1;
  } else if (else_body_idx > 0) {
    body_idx = else_body_idx;
  } else {
    interp->in_tail_position = saved_tail_position;
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  if (args[body_idx]->compiled) {
    interp->in_tail_position = saved_tail_position;
    return lcl_eval_program(interp, args[body_idx]->compiled, out);
  }

  if (lcl_eval_word_to_str(interp, args[body_idx], &body_v) != LCL_RC_OK) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  {
    const char *body_src;

    if (lcl_value_to_cstring(interp, body_v, &body_src) != LCL_OK) {
      lcl_ref_dec(body_v);
      interp->in_tail_position = saved_tail_position;
      return LCL_RC_ERR;
    }

    body_p = lcl_compile_report(interp, body_src, "<if>");
  }
  lcl_ref_dec(body_v);

  if (!body_p) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  interp->in_tail_position = saved_tail_position;
  rc = lcl_eval_program(interp, body_p, out);
  lcl_program_free(body_p);

  return rc;
}

static int word_is_literal(const lcl_word *w, const char *lit) {
  if (w->np != 1) {
    return 0;
  }

  if (w->wp[0].kind != LCL_WP_LIT) {
    return 0;
  }

  return strcmp(w->wp[0].as.lit.s, lit) == 0;
}

/* cond test1 expr1 test2 expr2 ... [else exprN]
 * Multi-branch conditional with short-circuit evaluation.
 * Evaluates tests left-to-right until one is truthy, then evaluates
 * and returns that clause's expression. The 'else' keyword marks
 * the default clause (must be last). Error if no clause matches.
 */
static int s_cond(lcl_interp *interp, int argc, const lcl_word **args,
                  lcl_value **out) {
  int saved_tail_position = interp->in_tail_position;
  int i;
  lcl_value *test_v = NULL;
  int is_true;
  int rc;

  if (argc < 2 || (argc % 2) != 0) {
    LCL_ERR_MSG(interp, "cond: requires pairs of test/expr arguments");
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc - 2; i += 2) {
    if (word_is_literal(args[i], "else")) {
      LCL_ERR_MSG(interp, "cond: 'else' must be the last clause");
      return LCL_RC_ERR;
    }
  }

  interp->in_tail_position = 0;

  for (i = 0; i < argc; i += 2) {
    if (word_is_literal(args[i], "else")) {
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);
      return rc;
    }

    if (lcl_eval_word(interp, args[i], &test_v) != LCL_RC_OK) {
      interp->in_tail_position = saved_tail_position;
      return LCL_RC_ERR;
    }

    is_true = lcl_value_is_true(test_v);
    lcl_ref_dec(test_v);

    if (is_true) {
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);
      return rc;
    }
  }

  interp->in_tail_position = saved_tail_position;
  LCL_ERR_MSG(interp, "cond: no matching clause");
  return LCL_RC_ERR;
}

/* case expr key1 expr1 key2 expr2 ... [else exprN]
 * Value dispatch with equality comparison.
 * Evaluates the scrutinee once, then compares keys using == until
 * a match is found. The 'else' keyword marks the default clause
 * (must be last). Error if no clause matches.
 */
static int s_case(lcl_interp *interp, int argc, const lcl_word **args,
                  lcl_value **out) {
  int saved_tail_position = interp->in_tail_position;
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  lcl_value *scrutinee = NULL;
  lcl_value *key_v = NULL;
  int i;
  int is_match;
  int rc;

  if (argc < 3 || (argc % 2) != 1) {
    LCL_ERR_MSG(interp, "case: requires scrutinee and pairs of key/expr");
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc - 2; i += 2) {
    if (word_is_literal(args[i], "else")) {
      LCL_ERR_MSG(interp, "case: 'else' must be the last clause");
      return LCL_RC_ERR;
    }
  }

  interp->in_tail_position = 0;

  if (lcl_eval_word(interp, args[0], &scrutinee) != LCL_RC_OK) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc; i += 2) {
    if (word_is_literal(args[i], "else")) {
      lcl_ref_dec(scrutinee);
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);

      return rc;
    }

    if (lcl_eval_word(interp, args[i], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(scrutinee);
      interp->in_tail_position = saved_tail_position;
      return LCL_RC_ERR;
    }

    is_match = lcl_value_equal_deep(scrutinee, key_v, &guard);
    lcl_ref_dec(key_v);

    if (is_match) {
      lcl_ref_dec(scrutinee);
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);
      return rc;
    }
  }

  lcl_ref_dec(scrutinee);
  interp->in_tail_position = saved_tail_position;
  LCL_ERR_MSG(interp, "case: no matching clause");

  return LCL_RC_ERR;
}

/* while test body - loop while test is true, re-evaluating test each iteration
 */
static int s_while(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  int test_owned = 0;
  int body_owned = 0;
  lcl_value *last = NULL;
  int test_is_braced;
  int rc;

  if (!chk_argc(interp, "while", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[0]->braced;

  if (test_is_braced) {
    if (get_body_program(interp, args[0], "<while-test>", &test_p,
                         &test_owned) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }
  }

  if (get_body_program(interp, args[1], "<while-body>", &body_p, &body_owned) !=
      LCL_RC_OK) {
    free_if_owned(test_p, test_owned);
    return LCL_RC_ERR;
  }

  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);

      if (rc != LCL_RC_OK) {
        free_if_owned(test_p, test_owned);
        free_if_owned(body_p, body_owned);
        if (last) {
          lcl_ref_dec(last);
        }
        return rc;
      }
    } else {
      if (lcl_eval_word(interp, args[0], &cond_v) != LCL_RC_OK) {
        free_if_owned(body_p, body_owned);
        if (last) {
          lcl_ref_dec(last);
        }
        return LCL_RC_ERR;
      }
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (!is_true) {
      break;
    }

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      free_if_owned(test_p, test_owned);
      free_if_owned(body_p, body_owned);
      if (last) {
        lcl_ref_dec(last);
      }
      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      free_if_owned(test_p, test_owned);
      free_if_owned(body_p, body_owned);
      *out = last;
      return LCL_RC_RETURN;
    }
  }

  free_if_owned(test_p, test_owned);
  free_if_owned(body_p, body_owned);
  *out = last ? last : lcl_string_new("");
  return LCL_RC_OK;
}

/* for start test next body - Tcl-style for loop */
static int s_for(lcl_interp *interp, int argc, const lcl_word **args,
                 lcl_value **out) {
  lcl_program *start_p = NULL;
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  lcl_program *next_p = NULL;
  int start_owned = 0;
  int test_owned = 0;
  int body_owned = 0;
  int next_owned = 0;
  lcl_value *last = NULL;
  lcl_value *tmp = NULL;
  int test_is_braced;
  int rc;

  if (!chk_argc(interp, "for", argc, 4, 4)) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[1]->braced;

  if (get_body_program(interp, args[0], "<for-start>", &start_p,
                       &start_owned) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (test_is_braced) {
    if (get_body_program(interp, args[1], "<for-test>", &test_p, &test_owned) !=
        LCL_RC_OK) {
      free_if_owned(start_p, start_owned);
      return LCL_RC_ERR;
    }
  }

  if (get_body_program(interp, args[2], "<for-next>", &next_p, &next_owned) !=
      LCL_RC_OK) {
    free_if_owned(start_p, start_owned);

    if (test_p) {
      free_if_owned(test_p, test_owned);
    }

    return LCL_RC_ERR;
  }

  if (get_body_program(interp, args[3], "<for-body>", &body_p, &body_owned) !=
      LCL_RC_OK) {
    free_if_owned(start_p, start_owned);

    if (test_p) {
      free_if_owned(test_p, test_owned);
    }

    free_if_owned(next_p, next_owned);

    return LCL_RC_ERR;
  }

  rc = lcl_eval_program(interp, start_p, &tmp);
  free_if_owned(start_p, start_owned);

  if (tmp) {
    lcl_ref_dec(tmp);
  }

  if (rc != LCL_RC_OK) {
    if (test_p) {
      free_if_owned(test_p, test_owned);
    }

    free_if_owned(body_p, body_owned);
    free_if_owned(next_p, next_owned);

    return rc;
  }

  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);

      if (rc != LCL_RC_OK) {
        free_if_owned(test_p, test_owned);
        free_if_owned(body_p, body_owned);
        free_if_owned(next_p, next_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        return rc;
      }
    } else {
      if (lcl_eval_word(interp, args[1], &cond_v) != LCL_RC_OK) {
        free_if_owned(body_p, body_owned);
        free_if_owned(next_p, next_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        return LCL_RC_ERR;
      }
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (!is_true) {
      break;
    }

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      tmp = NULL;
      rc = lcl_eval_program(interp, next_p, &tmp);

      if (tmp) {
        lcl_ref_dec(tmp);
      }

      if (rc != LCL_RC_OK && rc != LCL_RC_CONTINUE) {
        if (test_p) {
          free_if_owned(test_p, test_owned);
        }

        free_if_owned(body_p, body_owned);
        free_if_owned(next_p, next_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        return rc;
      }

      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      if (test_p) {
        free_if_owned(test_p, test_owned);
      }

      free_if_owned(body_p, body_owned);
      free_if_owned(next_p, next_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {

      if (test_p) {
        free_if_owned(test_p, test_owned);
      }

      free_if_owned(body_p, body_owned);
      free_if_owned(next_p, next_owned);

      *out = last;

      return LCL_RC_RETURN;
    }

    tmp = NULL;
    rc = lcl_eval_program(interp, next_p, &tmp);

    if (tmp) {
      lcl_ref_dec(tmp);
    }

    if (rc != LCL_RC_OK) {
      if (test_p) {
        free_if_owned(test_p, test_owned);
      }

      free_if_owned(body_p, body_owned);
      free_if_owned(next_p, next_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }
  }

  if (test_p) {
    free_if_owned(test_p, test_owned);
  }

  free_if_owned(body_p, body_owned);
  free_if_owned(next_p, next_owned);

  *out = last ? last : lcl_string_new("");

  return LCL_RC_OK;
}

/* foreach varname list body - iterate over list elements */
static int s_foreach(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_value *varname_v = NULL;
  lcl_value *list_v = NULL;
  lcl_program *body_p = NULL;
  int body_owned = 0;
  lcl_value *last = NULL;
  const char *varname;
  size_t i;
  size_t list_len;
  int rc;

  if (!chk_argc(interp, "foreach", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &varname_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, varname_v, &varname) != LCL_OK) {
    lcl_ref_dec(varname_v);
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &list_v) != LCL_RC_OK) {
    lcl_ref_dec(varname_v);

    return LCL_RC_ERR;
  }

  if (list_v->type != LCL_LIST) {
    const char *list_src;
    lcl_value *parsed;

    if (lcl_value_to_cstring(interp, list_v, &list_src) != LCL_OK) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      return LCL_RC_ERR;
    }

    parsed = lcl_list_new_from_cwords(list_src);
    lcl_ref_dec(list_v);

    if (!parsed) {
      LCL_ERR_MSG(interp, "foreach: invalid list");
      lcl_ref_dec(varname_v);

      return LCL_RC_ERR;
    }

    list_v = parsed;
  }

  if (get_body_program(interp, args[2], "<foreach>", &body_p, &body_owned) !=
      LCL_RC_OK) {
    lcl_ref_dec(varname_v);
    lcl_ref_dec(list_v);

    return LCL_RC_ERR;
  }

  list_len = lcl_list_len(list_v);

  for (i = 0; i < list_len; i++) {
    lcl_value *elem = NULL;

    if (lcl_list_get(list_v, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "foreach: internal error reading list");
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    if (lcl_env_let(&interp->env, varname, elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "foreach: out of memory");
      lcl_ref_dec(elem);
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    lcl_ref_dec(elem); /* bugfix: env_let increments refcount */

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      free_if_owned(body_p, body_owned);

      *out = last;

      return LCL_RC_RETURN;
    }
  }

  lcl_ref_dec(varname_v);
  lcl_ref_dec(list_v);
  free_if_owned(body_p, body_owned);

  *out = last ? last : lcl_string_new("");

  return LCL_RC_OK;
}

lcl_value *lcl_list_new_from_cwords(const char *words) {
  lcl_value *list = lcl_list_new();
  const char *p = words;
  const char *start;

  if (!list) {
    return NULL;
  }
  if (!words) {
    return list;
  }

  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    }

    if (!*p) {
      break;
    }

    start = p;

    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }

    if (p > start) {
      size_t len = (size_t)(p - start);
      char *word = (char *)malloc(len + 1);
      lcl_value *val;

      if (!word) {
        lcl_ref_dec(list);

        return NULL;
      }

      memcpy(word, start, len);
      word[len] = '\0';
      val = lcl_value_new_string(word);
      free(word);

      if (!val) {
        lcl_ref_dec(list);

        return NULL;
      }

      if (lcl_list_push(&list, val) != LCL_OK) {
        lcl_ref_dec(val);
        lcl_ref_dec(list);

        return NULL;
      }

      lcl_ref_dec(val);
    }
  }

  return list;
}

static int make_lambda(lcl_interp *interp, const char *self_name,
                       const lcl_word *params_word, const lcl_word *body_word,
                       lcl_value **out) {
  lcl_value *params_s = NULL;
  lcl_value *body_s = NULL;
  lcl_program *body_p = NULL;
  lcl_param_spec pspec;
  lcl_upvalue *upvals = NULL;
  int nupvals = 0;
  const char *params_str;
  const char *body_str;

  pspec.params = NULL;
  pspec.n_required = 0;
  pspec.n_optional = 0;
  pspec.rest_name = NULL;

  if (lcl_eval_word_to_str(interp, params_word, &params_s) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, body_word, &body_s) != LCL_RC_OK) {
    lcl_ref_dec(params_s);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, params_s, &params_str) != LCL_OK) {
    lcl_ref_dec(params_s);
    lcl_ref_dec(body_s);
    return LCL_RC_ERR;
  }

  if (lcl_parse_params(interp, params_str, &pspec) != LCL_RC_OK) {
    lcl_ref_dec(params_s);
    lcl_ref_dec(body_s);
    return LCL_RC_ERR;
  }
  lcl_ref_dec(params_s);

  if (lcl_value_to_cstring(interp, body_s, &body_str) != LCL_OK) {
    lcl_param_spec_free(&pspec);
    lcl_ref_dec(body_s);
    return LCL_RC_ERR;
  }

  body_p = lcl_compile_report(interp, body_str, "<lambda>");
  lcl_ref_dec(body_s);

  if (!body_p) {
    lcl_param_spec_free(&pspec);
    return LCL_RC_ERR;
  }

  if (lcl_build_upvalues(interp, body_p, &pspec, self_name, &upvals,
                         &nupvals) != LCL_RC_OK) {
    lcl_param_spec_free(&pspec);
    lcl_program_free(body_p);
    return LCL_RC_ERR;
  }

  *out = lcl_proc_new(self_name, upvals, nupvals, &pspec, body_p);

  if (!*out) {
    LCL_ERR_MSG(interp, "lambda: out of memory");
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

static int s_lambda(lcl_interp *interp, int argc, const lcl_word **args,
                    lcl_value **out) {
  /*
   * Two forms:
   *   lambda {params} {body}        - anonymous (argc == 2, first arg braced)
   *   lambda name {params} {body}   - named, can self-recurse (argc == 3)
   *
   * Detection: if argc == 2 → anonymous
   *            if argc == 3 → named (first arg is the name)
   */
  if (argc == 2) {
    return make_lambda(interp, NULL, args[0], args[1], out);
  } else if (argc == 3) {
    lcl_value *name_v = NULL;
    const char *self_name;
    int rc;

    if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, name_v, &self_name) != LCL_OK) {
      lcl_ref_dec(name_v);
      return LCL_RC_ERR;
    }

    rc = make_lambda(interp, self_name, args[1], args[2], out);
    lcl_ref_dec(name_v);
    return rc;
  } else {
    LCL_ERR_MSG(interp, "lambda: expected 2 or 3 arguments");
    return LCL_RC_ERR;
  }
}

static int is_name_char(int c) {
  return (c == '_' || c == ':' || (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
}

static int is_name_start(int c) {
  return (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

/* append n bytes to a dynamic buffer */
static int buf_append(char **buf, size_t *len, size_t *cap, const char *s,
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

static int buf_append_char(char **buf, size_t *len, size_t *cap, char c) {
  return buf_append(buf, len, cap, &c, 1);
}

/* find_global_frame: walk to the root of a frame chain. Used to
 * anchor qualified-path root namespaces and require'd modules at the
 * shared global frame regardless of where the call site lives. */
static lcl_frame *find_global_frame(lcl_frame *f) {
  if (!f) {
    return NULL;
  }

  while (f->parent) {
    f = f->parent;
  }

  return f;
}

/* resolve or create a namespace path like "a::b::c".
 *
 * Creates intermediate namespaces as needed.
 *
 * When the root segment isn't already reachable via the lexical
 * chain, it's created and bound in the *global* frame, not the
 * current frame. This makes qualified-path declarations like
 * `namespace foo::bar { ... }` survive an enclosing namespace builder
 * -- previously the root landed in the builder's overlay and was
 * discarded when the overlay popped, orphaning the sub-namespace.
 *
 * Returns the final namespace with +1 refcount, or NULL on error. */
static lcl_value *resolve_or_create_ns_path(lcl_interp *interp,
                                            const char *path) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  if (!lcl_ns_split(path, first, sizeof(first), &rest)) {
    lcl_value *ns = NULL;
    lcl_frame *global = NULL;

    if (lcl_env_get_value(interp, path, &ns) == LCL_OK) {
      if (ns->type != LCL_NAMESPACE) {
        lcl_ref_dec(ns);
        return NULL;
      }

      return ns;
    }

    ns = lcl_ns_new(path);

    if (!ns) {
      return NULL;
    }

    global = find_global_frame(interp->env.frame);

    if (!global || !hash_table_put(global->locals, path, ns)) {
      lcl_ref_dec(ns);
      return NULL;
    }

    return ns;
  }

  if (lcl_env_get_value(interp, first, &current) != LCL_OK) {
    lcl_frame *global = NULL;

    current = lcl_ns_new(first);

    if (!current) {
      return NULL;
    }

    global = find_global_frame(interp->env.frame);

    if (!global || !hash_table_put(global->locals, first, current)) {
      lcl_ref_dec(current);
      return NULL;
    }
  } else if (current->type != LCL_NAMESPACE) {
    lcl_ref_dec(current);
    return NULL;
  }

  while (rest && *rest) {
    lcl_value *next = NULL;
    char part[256];
    const char *next_rest = NULL;
    const char *part_name;

    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      part_name = part;
    } else {
      part_name = rest;
      next_rest = NULL;
    }

    if (lcl_ns_get(current, part_name, &next) == LCL_OK) {
      if (next->type != LCL_NAMESPACE) {
        lcl_ref_dec(next);
        lcl_ref_dec(current);
        return NULL;
      }
    } else {
      next = lcl_ns_new(part_name);

      if (!next) {
        lcl_ref_dec(current);
        return NULL;
      }

      if (hash_table_put(current->as.namespace.namespace, part_name, next) ==
          0) {
        lcl_ref_dec(next);
        lcl_ref_dec(current);
        return NULL;
      }
    }

    lcl_ref_dec(current);
    current = next;
    rest = next_rest;
  }

  return current;
}

/* isolate { body }
 *
 * Evaluate body in the current frame with the namespace def-target
 * stack temporarily emptied. While the body runs, `let`/`var`/`proc`
 * create local bindings in the current frame instead of writing
 * through to any enclosing `namespace` builder's exports.
 *
 * This is the scope-barrier counterpart to `namespace`: it lets a
 * proc that runs inside a namespace body keep its own `let`s local
 * without leaking them into the namespace under construction. */
static int s_isolate(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_program *prog = NULL;
  int prog_owned = 0;
  int saved_def_floor;
  int saved_def_lookup_floor;
  int saved_tail_position;
  lcl_return_code rc;
  lcl_value *last = NULL;
  int i;

  if (!chk_argc(interp, "isolate", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (get_body_program(interp, args[0], "<isolate>", &prog, &prog_owned) !=
      LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Raise BOTH floors to the current depth: def_floor blocks bare
   * `let`/`var`/`proc` from writing through to an enclosing builder,
   * and def_lookup_floor blocks $foo::X self-references from seeing
   * through to it. A user-proc call raises only def_floor (so helpers
   * stay self-contained for writes but transparent for self-reference
   * reads); isolate is the full-barrier form.
   *
   * The def_stack itself stays intact -- previously this used
   * `def_depth = 0`, which made a nested namespace's push overwrite
   * the enclosing namespace's def_stack slot, corrupting its exports.
   */
  saved_def_floor = interp->def_floor;
  saved_def_lookup_floor = interp->def_lookup_floor;
  saved_tail_position = interp->in_tail_position;
  interp->def_floor = interp->def_depth;
  interp->def_lookup_floor = interp->def_depth;
  rc = LCL_RC_OK;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    int is_last_cmd = (i == prog->ncmd - 1);

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    /* Only the final command inherits the caller's tail position; mid-
     * body tail calls must not escape the isolate block. */
    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->def_floor = saved_def_floor;
      interp->def_lookup_floor = saved_def_lookup_floor;
      interp->in_tail_position = saved_tail_position;
      free_if_owned(prog, prog_owned);

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc != LCL_RC_OK) {
      if (rc != LCL_RC_RETURN) {
        interp->err_line = cmd->line;

        if (interp->err_file_owned && interp->err_file) {
          free((void *)interp->err_file);
        }

        interp->err_file = prog->file ? strdup(prog->file) : NULL;
        interp->err_file_owned = prog->file ? 1 : 0;
      }

      break;
    }
  }

  interp->def_floor = saved_def_floor;
  interp->def_lookup_floor = saved_def_lookup_floor;
  interp->in_tail_position = saved_tail_position;
  free_if_owned(prog, prog_owned);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
    return rc;
  }

  if (last) {
    lcl_ref_dec(last);
  }

  return rc;
}

/* Note: `namespace eval` simplified to `namespace`. */
static int s_namespace(lcl_interp *interp, int argc, const lcl_word **args,
                       lcl_value **out) {
  /* namespace { body }      - anonymous, returns ns value
   * namespace name { body } - named, auto-attaches to registry
   *
   * Re-entering an existing namespace mutates the existing namespace's
   * hash table in place rather than rebuilding and rebinding a new
   * namespace value. This makes `namespace foo { let X 1 }` from
   * inside a proc body persist correctly (the bind wouldn't propagate
   * out of the proc frame otherwise) and makes qualified-path
   * re-entry (`namespace a::b { ... }`) preserve prior bindings. */
  int named;
  const lcl_word *body_word;
  lcl_value *name_v = NULL;
  char *ns_name = NULL;
  lcl_program *prog = NULL;
  int prog_owned = 0;
  lcl_frame *old_frame = NULL;
  lcl_def_target *target;
  lcl_return_code rc;
  int i;
  lcl_value *last = NULL;
  lcl_value *exports = NULL;
  lcl_value *ns = NULL;
  lcl_value *existing_ns = NULL;

  if (!chk_argc(interp, "namespace", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  named = (argc == 2);
  body_word = named ? args[1] : args[0];

  if (named) {
    const char *name_cstr;

    if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }
    if (lcl_value_to_cstring(interp, name_v, &name_cstr) != LCL_OK) {
      lcl_ref_dec(name_v);
      return LCL_RC_ERR;
    }
    ns_name = strdup(name_cstr);
    lcl_ref_dec(name_v);
    if (!ns_name) {
      LCL_ERR_MSG(interp, "namespace: out of memory");
      return LCL_RC_ERR;
    }
  }

  {
    int prog_owned_flag = 0;
    if (get_body_program(interp, body_word, "<namespace>", &prog,
                         &prog_owned_flag) != LCL_RC_OK) {
      free(ns_name);
      return LCL_RC_ERR;
    }
    prog_owned = prog_owned_flag;
  }

  if (lcl_def_target_push(interp, interp->env.frame, ns_name) != LCL_OK) {
    if (interp->def_depth >= LCL_DEF_STACK_MAX) {
      LCL_ERR_MSG(interp, "namespace: builders nested too deeply");
    } else {
      LCL_ERR_MSG(interp, "namespace: out of memory");
    }
    free_if_owned(prog, prog_owned);
    free(ns_name);
    return LCL_RC_ERR;
  }

  target = &interp->def_stack[interp->def_depth - 1];
  old_frame = interp->env.frame;

  /* If re-entering an existing namespace, look it up and pre-populate
   * the overlay with its bindings. existing_ns is held across the
   * body's evaluation so the end-of-build path can mutate it in
   * place. lcl_env_get_value handles both unqualified ("foo") and
   * qualified ("a::b::c") names. */
  if (ns_name) {
    lcl_value *found = NULL;

    if (lcl_env_get_value(interp, ns_name, &found) == LCL_OK) {
      if (found->type == LCL_NAMESPACE) {
        hash_iter it = {0};
        const char *key;
        lcl_value *value;
        int prepop_failed = 0;

        while (hash_table_iterate(found->as.namespace.namespace, &it, &key,
                                  &value)) {
          if (!prepop_failed) {
            if (!hash_table_put(target->overlay->locals, key, value) ||
                lcl_dict_put(&target->exports, key, value) != LCL_OK) {
              prepop_failed = 1;
            }
          }

          lcl_ref_dec(value);
        }

        if (prepop_failed) {
          lcl_value *leaked_exports;
          lcl_ref_dec(found);
          leaked_exports = lcl_def_target_pop(interp);

          if (leaked_exports) {
            lcl_ref_dec(leaked_exports);
          }

          free_if_owned(prog, prog_owned);
          free(ns_name);
          LCL_ERR_MSG(interp,
                      "namespace: out of memory pre-populating builder");
          return LCL_RC_ERR;
        }

        existing_ns = found;
      } else {
        lcl_ref_dec(found);
      }
    }
  }

  interp->env.frame = target->overlay;

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_value *leaked_exports;
    interp->env.frame = old_frame;
    /* Bugfix: lcl_def_target_pop returns the exports dict with +1
     * ref; decref it here on the error path or it leaks. */
    leaked_exports = lcl_def_target_pop(interp);

    if (leaked_exports) {
      lcl_ref_dec(leaked_exports);
    }

    lcl_ref_dec(existing_ns);
    free_if_owned(prog, prog_owned);
    free(ns_name);
    LCL_ERR_MSG(interp, "namespace: max recursion depth exceeded");
    return LCL_RC_ERR;
  }

  interp->depth++;
  rc = LCL_RC_OK;

  {
    /* Bugfix: `namespace eval`'s body runs for side effects; its last
     * command's value is discarded. Suppress tail-position
     * propagation so a self-recursive call inside the body cannot
     * escape via LCL_RC_TAILCALL. */
    int saved_tail_position = interp->in_tail_position;
    interp->in_tail_position = 0;

    for (i = 0; i < prog->ncmd; i++) {
      lcl_command *cmd = &prog->cmd[i];

      if (last) {
        lcl_ref_dec(last);
        last = NULL;
      }

      rc = lcl_call_from_words(interp, cmd, &last);

      if (rc != LCL_RC_OK) {
        if (rc != LCL_RC_RETURN) {
          interp->err_line = cmd->line;

          if (interp->err_file_owned && interp->err_file) {
            free((void *)interp->err_file);
          }

          interp->err_file = prog->file ? strdup(prog->file) : NULL;
          interp->err_file_owned = prog->file ? 1 : 0;
        }

        break;
      }
    }

    interp->in_tail_position = saved_tail_position;
  }

  interp->depth--;
  interp->env.frame = old_frame;
  exports = lcl_def_target_pop(interp);
  free_if_owned(prog, prog_owned);

  if (last) {
    lcl_ref_dec(last);
  }

  if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
    if (exports) {
      lcl_ref_dec(exports);
    }

    lcl_ref_dec(existing_ns);
    free(ns_name);
    return rc;
  }

  /* Re-entry path: mutate the existing namespace in place by dumping
   * the (pre-pop + body) exports into its hash table. This skips
   * rebuild-and-rebind entirely; all live references to the
   * namespace value observe the new bindings immediately, and a
   * proc-body-scoped rebind can no longer shadow the outer
   * binding. */
  if (existing_ns) {
    hash_iter it = {0};
    const char *key;
    lcl_value *value;
    int put_failed = 0;

    while (hash_table_iterate(exports->as.dict.dictionary, &it, &key, &value)) {
      if (!put_failed) {
        if (!hash_table_put(existing_ns->as.namespace.namespace, key, value)) {
          put_failed = 1;
        }
      }

      lcl_ref_dec(value);
    }

    lcl_ref_dec(exports);

    if (put_failed) {
      lcl_ref_dec(existing_ns);
      free(ns_name);
      LCL_ERR_MSG(interp,
                  "namespace: out of memory mutating existing namespace");
      return LCL_RC_ERR;
    }

    free(ns_name);
    *out = existing_ns;
    return LCL_RC_OK;
  }

  ns = lcl_ns_from_dict(exports, ns_name);

  if (!ns) {
    free(ns_name);
    LCL_ERR_MSG(interp, "namespace: failed to create namespace");
    return LCL_RC_ERR;
  }

  if (ns_name) {
    char first[256];
    const char *rest = NULL;

    if (lcl_ns_split(ns_name, first, sizeof(first), &rest)) {
      lcl_value *parent = resolve_or_create_ns_path(interp, first);

      if (!parent) {
        lcl_ref_dec(ns);
        free(ns_name);
        LCL_ERR_MSG(interp, "namespace: failed to resolve parent path");
        return LCL_RC_ERR;
      }

      while (rest && *rest) {
        char part[256];
        const char *next_rest = NULL;
        lcl_value *next = NULL;

        if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
          if (lcl_ns_get(parent, part, &next) != LCL_OK) {
            next = lcl_ns_new(part);

            if (!next ||
                !hash_table_put(parent->as.namespace.namespace, part, next)) {
              LCL_ERR_MSG(interp, "namespace: out of memory");

              if (next) {
                lcl_ref_dec(next);
              }

              lcl_ref_dec(parent);
              lcl_ref_dec(ns);
              free(ns_name);

              return LCL_RC_ERR;
            }
          }

          lcl_ref_dec(parent);
          parent = next;
          rest = next_rest;
        } else {
          if (!hash_table_put(parent->as.namespace.namespace, rest, ns)) {
            lcl_ref_dec(parent);
            lcl_ref_dec(ns);
            free(ns_name);
            LCL_ERR_MSG(interp, "namespace: failed to bind in parent");
            return LCL_RC_ERR;
          }

          lcl_ref_dec(parent);
          rest = NULL;
        }
      }
    } else {
      if (interp->def_depth > interp->def_floor) {
        if (lcl_def_target_bind(interp, ns_name, ns) != LCL_OK) {
          lcl_ref_dec(ns);
          free(ns_name);
          LCL_ERR_MSG(interp, "namespace: failed to bind in parent builder");
          return LCL_RC_ERR;
        }
      } else {
        if (lcl_env_let(&interp->env, ns_name, ns) != LCL_OK) {
          lcl_ref_dec(ns);
          free(ns_name);
          LCL_ERR_MSG(interp, "namespace: failed to attach namespace");
          return LCL_RC_ERR;
        }
      }
    }
    free(ns_name);
  }

  *out = ns;
  return LCL_RC_OK;
}

/* import <namespace> ?name1 name2 ...?
 * Imports bindings from a namespace into the current scope.
 * If no names given, imports all bindings.
 * Errors if any name already exists in the current frame. */
static int s_import(lcl_interp *interp, int argc, const lcl_word **argv,
                    lcl_value **out) {
  lcl_value *ns_name_v = NULL;
  lcl_value *ns = NULL;
  const char *ns_name;
  int i;

  if (!chk_argc(interp, "import", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, argv[0], &ns_name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (ns_name_v->type == LCL_NAMESPACE) {
    ns = ns_name_v;
  } else if (ns_name_v->type == LCL_CELL && ns_name_v->as.cell.inner != NULL &&
             ns_name_v->as.cell.inner->type == LCL_NAMESPACE) {
    ns = lcl_ref_inc(ns_name_v->as.cell.inner);
    lcl_ref_dec(ns_name_v);
  } else {
    if (lcl_value_to_cstring(interp, ns_name_v, &ns_name) != LCL_OK) {
      lcl_ref_dec(ns_name_v);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_value(interp, ns_name, &ns) != LCL_OK) {
      LCL_ERR_MSG(interp, "import: namespace not found");
      lcl_ref_dec(ns_name_v);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(ns_name_v);

    if (ns->type == LCL_CELL) {
      lcl_value *inner;

      /* Bugfxi: Cell may have been cleared (NULL inner) by
       * lcl_frame_clear / lcl_frame_free's cycle-breaker. */
      if (!ns->as.cell.inner) {
        LCL_ERR_MSG(interp, "import: cell is empty");
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      inner = lcl_ref_inc(ns->as.cell.inner);
      lcl_ref_dec(ns);
      ns = inner;
    }
  }

  if (ns->type != LCL_NAMESPACE) {
    err_expected_got(interp, "import", "namespace", ns);
    lcl_ref_dec(ns);
    return LCL_RC_ERR;
  }

  if (argc == 1) {
    hash_iter it = {0};
    const char *key;
    lcl_value *value;

    while (hash_table_iterate(ns->as.namespace.namespace, &it, &key, &value)) {
      lcl_value *existing = NULL;

      if (hash_table_get(interp->env.frame->locals, key, &existing)) {
        char buf[256];
        sprintf(buf, "import: '%s' already exists in current scope", key);
        lcl_ref_dec(existing);
        lcl_ref_dec(value);
        lcl_ref_dec(ns);
        LCL_ERR_MSG_DUP(interp, buf);
        return LCL_RC_ERR;
      }

      if (interp->def_depth > interp->def_floor) {
        if (lcl_def_target_bind(interp, key, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      } else {
        if (lcl_env_let(&interp->env, key, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      }
      lcl_ref_dec(value); /* Balance iterate */
    }
  } else {
    for (i = 1; i < argc; i++) {
      lcl_value *name_v = NULL;
      const char *name_str;
      lcl_value *value = NULL;
      lcl_value *existing = NULL;

      if (lcl_eval_word_to_str(interp, argv[i], &name_v) != LCL_RC_OK) {
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
        lcl_ref_dec(name_v);
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      if (lcl_ns_get(ns, name_str, &value) != LCL_OK) {
        char buf[256];
        sprintf(buf, "import: '%s' not found in namespace", name_str);
        LCL_ERR_MSG_DUP(interp, buf);
        lcl_ref_dec(name_v);
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      if (hash_table_get(interp->env.frame->locals, name_str, &existing)) {
        char buf[256];
        sprintf(buf, "import: '%s' already exists in current scope", name_str);
        lcl_ref_dec(existing);
        lcl_ref_dec(value);
        lcl_ref_dec(name_v);
        lcl_ref_dec(ns);
        LCL_ERR_MSG_DUP(interp, buf);
        return LCL_RC_ERR;
      }

      if (interp->def_depth > interp->def_floor) {
        if (lcl_def_target_bind(interp, name_str, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(name_v);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      } else {
        if (lcl_env_let(&interp->env, name_str, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(name_v);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      }

      lcl_ref_dec(value);
      lcl_ref_dec(name_v);
    }
  }

  lcl_ref_dec(ns);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static int s_subst(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  lcl_value *input_v = NULL;
  const char *src;
  size_t src_len;
  size_t i;
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;

  if (!chk_argc(interp, "subst", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, input_v, &src) != LCL_OK) {
    lcl_ref_dec(input_v);
    return LCL_RC_ERR;
  }
  src_len = strlen(src);

  for (i = 0; i < src_len;) {
    char c = src[i];

    if (c == '\\' && i + 1 < src_len) {
      char next = src[i + 1];
      char esc;

      switch (next) {
      case 'n': esc = '\n'; break;
      case 't': esc = '\t'; break;
      case 'r': esc = '\r'; break;
      case '\\': esc = '\\'; break;
      case '[': esc = '['; break;
      case ']': esc = ']'; break;
      case '$': esc = '$'; break;
      case '{': esc = '{'; break;
      case '}': esc = '}'; break;
      case '"': esc = '"'; break;
      default:
        if (!buf_append_char(&result, &result_len, &result_cap, '\\')) {
          goto err;
        }
        esc = next;
        break;
      }

      if (!buf_append_char(&result, &result_len, &result_cap, esc)) {
        goto err;
      }

      i += 2;
      continue;
    }

    if (c == '$') {
      i++;

      if (i < src_len && src[i] == '{') {
        size_t start = ++i;

        while (i < src_len && src[i] != '}') {
          i++;
        }

        if (i >= src_len) {
          goto err;
        }

        {
          size_t name_len = i - start;
          char *name = malloc(name_len + 1);
          lcl_value *val = NULL;
          const char *val_str;

          if (!name) {
            goto err;
          }

          memcpy(name, src + start, name_len);
          name[name_len] = '\0';

          if (lcl_env_get_value(interp, name, &val) != LCL_OK) {
            free(name);
            goto err;
          }

          free(name);

          if (val->type == LCL_CELL) {
            lcl_value *content = NULL;

            if (lcl_cell_get(val, &content) != LCL_OK) {
              lcl_ref_dec(val);
              goto err;
            }

            lcl_ref_dec(val);
            val = content;
          }

          if (lcl_value_to_cstring(interp, val, &val_str) != LCL_OK) {
            lcl_ref_dec(val);
            goto err;
          }

          if (!buf_append(&result, &result_len, &result_cap, val_str,
                          strlen(val_str))) {
            lcl_ref_dec(val);
            goto err;
          }

          lcl_ref_dec(val);
        }

        i++; /* bugfix: skip closing } */
        continue;
      }

      if (i < src_len && is_name_start((unsigned char)src[i])) {
        size_t start = i;

        i++;

        while (i < src_len && is_name_char((unsigned char)src[i])) {
          i++;
        }

        {
          size_t name_len = i - start;
          char *name = malloc(name_len + 1);
          lcl_value *val = NULL;
          const char *val_str;

          if (!name) {
            goto err;
          }

          memcpy(name, src + start, name_len);
          name[name_len] = '\0';

          if (lcl_env_get_value(interp, name, &val) != LCL_OK) {
            free(name);
            goto err;
          }

          free(name);

          if (val->type == LCL_CELL) {
            lcl_value *content = NULL;

            if (lcl_cell_get(val, &content) != LCL_OK) {
              lcl_ref_dec(val);
              goto err;
            }

            lcl_ref_dec(val);
            val = content;
          }

          if (lcl_value_to_cstring(interp, val, &val_str) != LCL_OK) {
            lcl_ref_dec(val);
            goto err;
          }

          if (!buf_append(&result, &result_len, &result_cap, val_str,
                          strlen(val_str))) {
            lcl_ref_dec(val);
            goto err;
          }

          lcl_ref_dec(val);
        }

        continue;
      }

      if (!buf_append_char(&result, &result_len, &result_cap, '$')) {
        goto err;
      }

      continue;
    }

    if (c == '[') {
      size_t start = ++i;
      int depth = 1;

      while (i < src_len && depth > 0) {
        if (src[i] == '[') {
          depth++;
        } else if (src[i] == ']') {
          depth--;
        } else if (src[i] == '\\' && i + 1 < src_len) {
          i++;
        }

        if (depth > 0) {
          i++;
        }
      }

      if (depth != 0) {
        /* bugfix: this is an unterminated [...] */
        goto err;
      }

      {
        size_t subcmd_len = i - start;
        char *subcmd_src = malloc(subcmd_len + 1);
        lcl_program *prog;
        lcl_value *subcmd_result = NULL;
        int rc;
        const char *result_str;

        if (!subcmd_src) {
          goto err;
        }

        memcpy(subcmd_src, src + start, subcmd_len);
        subcmd_src[subcmd_len] = '\0';

        prog = lcl_compile_report(interp, subcmd_src, "<subst>");
        free(subcmd_src);

        if (!prog) {
          goto err;
        }

        if (interp->max_depth && interp->depth >= interp->max_depth) {
          lcl_program_free(prog);
          goto err;
        }

        interp->depth++;

        rc = LCL_RC_OK;

        {
          int j;
          /* Bugfix: `subst`'s embedded commands run for their string
           * values; they are not in tail position relative to subst's
           * caller. Suppress tail-position propagation. */
          int saved_tail_position = interp->in_tail_position;
          interp->in_tail_position = 0;

          for (j = 0; j < prog->ncmd; j++) {
            lcl_command *cmd = &prog->cmd[j];

            if (subcmd_result) {
              lcl_ref_dec(subcmd_result);
              subcmd_result = NULL;
            }

            rc = lcl_call_from_words(interp, cmd, &subcmd_result);

            if (rc != LCL_RC_OK) {
              if (rc != LCL_RC_RETURN) {
                interp->err_line = cmd->line;

                if (interp->err_file_owned && interp->err_file) {
                  free((void *)interp->err_file);
                }

                interp->err_file = prog->file ? strdup(prog->file) : NULL;
                interp->err_file_owned = prog->file ? 1 : 0;
              }

              break;
            }
          }

          interp->in_tail_position = saved_tail_position;
        }

        interp->depth--;
        lcl_program_free(prog);

        if (rc != LCL_RC_OK) {
          if (subcmd_result) {
            lcl_ref_dec(subcmd_result);
          }
          goto err;
        }

        if (subcmd_result) {
          if (lcl_value_to_cstring(interp, subcmd_result, &result_str) !=
              LCL_OK) {
            lcl_ref_dec(subcmd_result);
            goto err;
          }
        } else {
          result_str = "";
        }

        if (!buf_append(&result, &result_len, &result_cap, result_str,
                        strlen(result_str))) {
          if (subcmd_result) {
            lcl_ref_dec(subcmd_result);
          }
          goto err;
        }

        if (subcmd_result) {
          lcl_ref_dec(subcmd_result);
        }
      }

      i++;
      continue;
    }

    if (!buf_append_char(&result, &result_len, &result_cap, c)) {
      goto err;
    }

    i++;
  }

  lcl_ref_dec(input_v);
  *out = lcl_value_new_string(result ? result : "");
  free(result);

  return *out ? LCL_RC_OK : LCL_RC_ERR;

err:
  if (!interp->err_msg) {
    LCL_ERR_MSG(interp, "subst: out of memory");
  }

  lcl_ref_dec(input_v);
  free(result);

  return LCL_RC_ERR;
}

/* quasiquote {template} - template with unquote (,expr) and splice (,@expr)
 *
 * Supports depth tracking for nested quasiquotes:
 * - At depth 1: ,expr evaluates expr and inserts result
 * - At depth > 1: ,expr passes through literally
 * - ,,expr at depth 2: evaluates expr, outputs ,<result>
 *
 * Unquote takes one normal LCL word:
 * - ,$var, ,${name}, ,[cmd], ,(list), ,#{dict}
 */

typedef enum { QQ_LITERAL, QQ_EVAL, QQ_SPLICE } qq_node_kind;

typedef struct qq_node {
  qq_node_kind kind;
  char *text;
  int prefix_commas;
  struct qq_node *next;
} qq_node;

static void qq_node_free(qq_node *node) {
  while (node) {
    qq_node *next = node->next;
    free(node->text);
    free(node);
    node = next;
  }
}

static qq_node *qq_node_new(qq_node_kind kind, const char *text, size_t len) {
  qq_node *node = (qq_node *)calloc(1, sizeof(qq_node));
  if (!node) {
    return NULL;
  }

  node->kind = kind;

  if (text && len > 0) {
    node->text = (char *)malloc(len + 1);
    if (!node->text) {
      free(node);
      return NULL;
    }

    memcpy(node->text, text, len);
    node->text[len] = '\0';
  }

  return node;
}

/* Skip balanced braces, returning position after closing brace */
static size_t skip_braces(const char *src, size_t len, size_t start) {
  int depth = 1;
  size_t i = start;
  while (i < len && depth > 0) {
    if (src[i] == '{') {
      depth++;
    } else if (src[i] == '}') {
      depth--;
    } else if (src[i] == '\\' && i + 1 < len) {
      i++;
    }

    if (depth > 0) {
      i++;
    }
  }

  return i;
}

/* Skip balanced brackets, returning position after closing bracket */
static size_t skip_brackets(const char *src, size_t len, size_t start) {
  int depth = 1;
  size_t i = start;

  while (i < len && depth > 0) {
    if (src[i] == '[') {
      depth++;
    } else if (src[i] == ']') {
      depth--;
    } else if (src[i] == '\\' && i + 1 < len) {
      i++;
    }

    if (depth > 0) {
      i++;
    }
  }

  return i;
}

/* Skip balanced parens, returning position after closing paren */
static size_t skip_parens(const char *src, size_t len, size_t start) {
  int depth = 1;
  size_t i = start;
  while (i < len && depth > 0) {
    if (src[i] == '(') {
      depth++;
    } else if (src[i] == ')') {
      depth--;
    } else if (src[i] == '\\' && i + 1 < len) {
      i++;
    }

    if (depth > 0) {
      i++;
    }
  }

  return i;
}

static int is_nested_quasiquote(const char *src, size_t len, size_t pos) {
  const char *kw = "quasiquote";
  size_t kw_len = 10;
  size_t i;

  if (pos + kw_len >= len) {
    return 0;
  }

  if (memcmp(src + pos, kw, kw_len) != 0) {
    return 0;
  }

  i = pos + kw_len;

  while (i < len && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n')) {
    i++;
  }

  return (i < len && src[i] == '{');
}

static size_t find_quasiquote_end(const char *src, size_t len, size_t start) {
  return skip_braces(src, len, start + 1) + 1;
}

static int parse_unquote_word(const char *src, size_t len, size_t pos,
                              size_t *word_start, size_t *word_end) {
  size_t i = pos;

  while (i < len && (src[i] == ' ' || src[i] == '\t')) {
    i++;
  }

  if (i >= len) {
    return 0;
  }

  *word_start = i;

  if (src[i] == '$') {
    i++;
    if (i < len && src[i] == '{') {
      i++;
      while (i < len && src[i] != '}') {
        i++;
      }
      if (i < len) {
        i++; /* skip } */
      }
    } else {
      while (i < len && is_name_char((unsigned char)src[i])) {
        i++;
      }
    }

    *word_end = i;
    return 1;
  }

  if (src[i] == '[') {
    i++;
    i = skip_brackets(src, len, i);

    if (i <= len) {
      i++;
    }

    *word_end = i;

    return 1;
  }

  if (src[i] == '{') {
    i++;
    i = skip_braces(src, len, i);

    if (i <= len) {
      i++;
    }

    *word_end = i;

    return 1;
  }

  if (src[i] == '(') {
    i++;
    i = skip_parens(src, len, i);

    if (i <= len) {
      i++;
    }

    *word_end = i;
    return 1;
  }

  if (src[i] == '#' && i + 1 < len && src[i + 1] == '{') {
    i += 2;
    i = skip_braces(src, len, i);

    if (i <= len) {
      i++;
    }

    *word_end = i;
    return 1;
  }

  /* Bugfix: bare word (alphanumeric, underscore, hyphen, colon for
     namespaces)
   */
  if (isalnum((unsigned char)src[i]) || src[i] == '_' || src[i] == '-') {
    while (i < len && (is_name_char((unsigned char)src[i]) || src[i] == ':' ||
                       src[i] == '-')) {
      i++;
    }

    *word_end = i;
    return 1;
  }

  return 0;
}

/* Recursive quasiquote parser with depth tracking */
static qq_node *qq_parse(const char *src, size_t len, int depth,
                         const char **err_msg) {
  qq_node *head = NULL;
  qq_node *tail = NULL;
  size_t i = 0;
  size_t lit_start = 0;

  while (i < len) {
    char c = src[i];

    if (c == '\\' && i + 1 < len) {
      if (i > lit_start) {
        qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      {
        qq_node *node = qq_node_new(QQ_LITERAL, src + i + 1, 1);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      i += 2;
      lit_start = i;
      continue;
    }

    if (is_nested_quasiquote(src, len, i)) {
      if (i > lit_start) {
        qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      {
        size_t kw_start = i;
        size_t brace_pos = i + 10; /* "quasiquote" */
        size_t inner_start;
        size_t inner_end;
        qq_node *inner_nodes;
        qq_node *node;

        while (brace_pos < len && src[brace_pos] != '{') {
          brace_pos++;
        }
        inner_start = brace_pos + 1;
        inner_end = find_quasiquote_end(src, len, brace_pos) - 1;
        node = qq_node_new(QQ_LITERAL, src + kw_start, inner_start - kw_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;

        inner_nodes = qq_parse(src + inner_start, inner_end - inner_start,
                               depth + 1, err_msg);
        if (!inner_nodes && *err_msg) {
          qq_node_free(head);
          return NULL;
        }

        if (inner_nodes) {
          tail->next = inner_nodes;
          while (tail->next) {
            tail = tail->next;
          }
        }

        node = qq_node_new(QQ_LITERAL, "}", 1);

        if (!node) {
          goto parse_err;
        }

        tail->next = node;
        tail = node;
        i = inner_end + 1;
        lit_start = i;
      }

      continue;
    }

    if (c == '"') {
      i++;

      while (i < len && src[i] != '"') {
        if (src[i] == '\\' && i + 1 < len) {
          i++;
        }
        i++;
      }

      if (i < len) {
        i++;
      }

      continue;
    }

    if (c == ',') {
      int num_commas = 0;
      int splice = 0;
      size_t comma_start = i;
      size_t word_start;
      size_t word_end;

      if (i > lit_start) {
        qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      while (i < len && src[i] == ',') {
        num_commas++;
        i++;
      }

      if (i < len && src[i] == '@') {
        splice = 1;
        i++;
      }

      if (!parse_unquote_word(src, len, i, &word_start, &word_end)) {
        *err_msg =
            "invalid unquote: expected $var, [cmd], {literal}, (list), or #{dict}";
        qq_node_free(head);
        return NULL;
      }

      if (num_commas > depth) {
        *err_msg = "too many unquotes for current quasiquote depth";
        qq_node_free(head);
        return NULL;
      }

      if (num_commas < depth) {
        size_t total_len = (word_end - comma_start);
        qq_node *node = qq_node_new(QQ_LITERAL, src + comma_start, total_len);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      } else {
        qq_node *node;

        if (splice) {
          node =
              qq_node_new(QQ_SPLICE, src + word_start, word_end - word_start);
        } else {
          node = qq_node_new(QQ_EVAL, src + word_start, word_end - word_start);
        }

        if (!node) {
          goto parse_err;
        }

        node->prefix_commas = num_commas - 1;

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      i = word_end;
      lit_start = i;
      continue;
    }

    i++;
  }

  if (i > lit_start) {
    qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

    if (!node) {
      goto parse_err;
    }

    if (tail) {
      tail->next = node;
    } else {
      head = node;
    }

    tail = node;
  }

  return head;

parse_err:
  *err_msg = "out of memory";
  qq_node_free(head);
  return NULL;
}

/* Check if a string needs braces to be a valid unquote word. Returns
 * 1 if braces needed, 0 if bare word is fine. */
static int qq_needs_braces(const char *s) {
  size_t i;
  if (!s || !*s) {
    return 1;
  }

  if (s[0] == '$' || s[0] == '[' || s[0] == '{' || s[0] == '(' || s[0] == '#') {
    return 1;
  }

  for (i = 0; s[i]; i++) {
    unsigned char c = (unsigned char)s[i];

    if (!isalnum(c) && c != '_' && c != '-' && c != '.' && c != ':') {
      return 1;
    }
  }

  return 0;
}

/* Build result string from IR, evaluating as needed */
static int qq_build(lcl_interp *interp, qq_node *nodes, char **result,
                    size_t *result_len, size_t *result_cap) {
  qq_node *node;

  for (node = nodes; node; node = node->next) {
    switch (node->kind) {
    case QQ_LITERAL:
      if (node->text) {
        if (!buf_append(result, result_len, result_cap, node->text,
                        strlen(node->text))) {
          return 0;
        }
      }
      break;

    case QQ_EVAL:
    case QQ_SPLICE: {
      lcl_scan sc;
      lcl_word w;
      lcl_value *val = NULL;
      const char *saved_file;
      int saved_line;
      int scan_rc;
      int eval_rc;
      int j;

      for (j = 0; j < node->prefix_commas; j++) {
        if (!buf_append_char(result, result_len, result_cap, ',')) {
          return 0;
        }
      }

      /* The unquote text is a single syntactic word ($var, [...],
       * {...}, (...), #{...}, or a bare word). Evaluate it as a word
       * so a variable substitution like ,$var yields the variable's
       * value -- not a one-word program that would dispatch that value
       * as a zero-arg command if its string form happened to name a
       * proc or built-in. */
      memset(&w, 0, sizeof(w));
      lcl_scan_init(&sc, node->text);
      scan_rc = lcl_scan_word(&sc, &w);

      if (scan_rc < 0) {
        lcl_word_free_contents(&w);
        LCL_ERR_MSG(interp, "quasiquote: failed to parse unquote expression");
        return 0;
      }

      saved_file = interp->cur_file;
      saved_line = interp->cur_line;

      eval_rc = lcl_eval_word(interp, &w, &val);

      interp->cur_file = saved_file;
      interp->cur_line = saved_line;

      lcl_word_free_contents(&w);

      if (eval_rc != LCL_RC_OK) {
        return 0;
      }

      if (node->kind == QQ_SPLICE) {
        lcl_value *list_val = val;

        if (val->type != LCL_LIST) {
          const char *val_src;

          if (lcl_value_to_cstring(interp, val, &val_src) != LCL_OK) {
            lcl_ref_dec(val);
            return 0;
          }

          list_val = lcl_list_new_from_cwords(val_src);
          lcl_ref_dec(val);

          if (!list_val) {
            return 0;
          }

          val = list_val;
        }

        {
          size_t len = lcl_list_len(list_val);
          size_t k;

          for (k = 0; k < len; k++) {
            lcl_value *elem = NULL;
            const char *elem_str;

            if (lcl_list_get(list_val, k, &elem) != LCL_OK) {
              lcl_ref_dec(val);
              return 0;
            }

            if (lcl_value_to_cstring(interp, elem, &elem_str) != LCL_OK) {
              lcl_ref_dec(elem);
              lcl_ref_dec(val);
              return 0;
            }

            if (k > 0) {
              if (!buf_append_char(result, result_len, result_cap, ' ')) {
                lcl_ref_dec(elem);
                lcl_ref_dec(val);
                return 0;
              }
            }

            if (!buf_append(result, result_len, result_cap, elem_str,
                            strlen(elem_str))) {
              lcl_ref_dec(elem);
              lcl_ref_dec(val);
              return 0;
            }

            lcl_ref_dec(elem);
          }
        }
      } else {
        const char *val_str;
        int needs_braces;

        if (lcl_value_to_cstring(interp, val, &val_str) != LCL_OK) {
          lcl_ref_dec(val);
          return 0;
        }
        /* When prefix_commas > 0, wrap in braces only if needed.
         * Simple values like "42" or "hello" can be bare words.
         * Values with spaces or special chars need braces. */
        needs_braces = node->prefix_commas > 0 && qq_needs_braces(val_str);
        if (needs_braces) {
          if (!buf_append_char(result, result_len, result_cap, '{')) {
            lcl_ref_dec(val);
            return 0;
          }
        }

        if (!buf_append(result, result_len, result_cap, val_str,
                        strlen(val_str))) {
          lcl_ref_dec(val);
          return 0;
        }

        if (needs_braces) {
          if (!buf_append_char(result, result_len, result_cap, '}')) {
            lcl_ref_dec(val);
            return 0;
          }
        }
      }

      lcl_ref_dec(val);
    } break;
    }
  }

  return 1;
}

static int s_quasiquote(lcl_interp *interp, int argc, const lcl_word **args,
                        lcl_value **out) {
  lcl_value *input_v = NULL;
  const char *src;
  size_t src_len;
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;
  qq_node *ir = NULL;
  const char *err_msg = NULL;

  if (!chk_argc(interp, "quasiquote", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, input_v, &src) != LCL_OK) {
    lcl_ref_dec(input_v);
    return LCL_RC_ERR;
  }

  src_len = strlen(src);
  ir = qq_parse(src, src_len, 1, &err_msg);

  if (!ir && err_msg) {
    LCL_ERR_MSG(interp, err_msg);
    lcl_ref_dec(input_v);
    return LCL_RC_ERR;
  }

  if (!qq_build(interp, ir, &result, &result_len, &result_cap)) {
    qq_node_free(ir);
    lcl_ref_dec(input_v);
    free(result);
    return LCL_RC_ERR;
  }

  qq_node_free(ir);
  lcl_ref_dec(input_v);

  *out = lcl_value_new_string(result ? result : "");
  free(result);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int s_eval(lcl_interp *interp, int argc, const lcl_word **args,
                  lcl_value **out) {
  int i;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int saved_tail_position = interp->in_tail_position;

  if (!chk_argc(interp, "eval", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  /* MVP: single argument */
  if (argc == 1) {
    lcl_value *script_v = NULL;
    const char *script_src;

    if (lcl_eval_word_to_str(interp, args[0], &script_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, script_v, &script_src) != LCL_OK) {
      lcl_ref_dec(script_v);
      return LCL_RC_ERR;
    }

    prog = lcl_compile_report(interp, script_src, "<eval>");
    lcl_ref_dec(script_v);
  } else {
    size_t total_len = 0;
    lcl_value **parts = NULL;
    char *script_str = NULL;
    char *p;

    parts = malloc(sizeof(lcl_value *) * (size_t)argc);
    if (!parts) {
      LCL_ERR_MSG(interp, "eval: out of memory");
      return LCL_RC_ERR;
    }

    for (i = 0; i < argc; i++) {
      size_t part_len;
      const char *part_str;

      if (lcl_eval_word_to_str(interp, args[i], &parts[i]) != LCL_RC_OK) {
        int j;

        for (j = 0; j < i; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        return LCL_RC_ERR;
      }
      if (lcl_value_to_cstring(interp, parts[i], &part_str) != LCL_OK) {
        int j;

        for (j = 0; j <= i; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        return LCL_RC_ERR;
      }
      part_len = strlen(part_str);

      if (!safe_add_size(total_len, part_len, &total_len)) {
        int j;
        /* Bugfix: parts[0..i] inclusive are populated; clean them
           up. */
        for (j = 0; j <= i; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        LCL_ERR_MSG(interp, "eval: combined script length overflows size_t");
        return LCL_RC_ERR;
      }
    }

    /* Bugfix: Account for the (argc - 1) inter-arg spaces and the
       trailing NUL. */
    if (!safe_add_size(total_len, (size_t)(argc - 1), &total_len) ||
        !safe_add_size(total_len, 1, &total_len)) {
      for (i = 0; i < argc; i++) {
        lcl_ref_dec(parts[i]);
      }
      free(parts);
      LCL_ERR_MSG(interp, "eval: combined script length overflows size_t");
      return LCL_RC_ERR;
    }

    script_str = malloc(total_len);
    if (!script_str) {
      LCL_ERR_MSG(interp, "eval: out of memory");
      for (i = 0; i < argc; i++) {
        lcl_ref_dec(parts[i]);
      }

      free(parts);
      return LCL_RC_ERR;
    }

    p = script_str;
    for (i = 0; i < argc; i++) {
      const char *s;
      size_t l;

      if (lcl_value_to_cstring(interp, parts[i], &s) != LCL_OK) {
        int j;

        for (j = 0; j < argc; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        free(script_str);
        return LCL_RC_ERR;
      }

      l = strlen(s);
      memcpy(p, s, l);
      p += l;

      if (i + 1 < argc) {
        *p++ = ' ';
      }
    }

    *p = '\0';

    for (i = 0; i < argc; i++) {
      lcl_ref_dec(parts[i]);
    }

    free(parts);

    prog = lcl_compile_report(interp, script_str, "<eval>");
    free(script_str);
  }

  if (!prog) {
    return LCL_RC_ERR;
  }

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    LCL_ERR_MSG(interp, "eval: max recursion depth exceeded");
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    int is_last_cmd = (i == prog->ncmd - 1);

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    /* Bugfix: Only the final command of the eval body inherits the
     * caller's tail position. Mid-body commands must not be in tail
     * position, or a self-recursive call inside the body would escape
     * via LCL_RC_TAILCALL and abandon the rest of the script. */
    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->in_tail_position = saved_tail_position;
      interp->depth--;
      lcl_program_free(prog);

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc != LCL_RC_OK) {
      if (rc != LCL_RC_RETURN) {
        interp->err_line = cmd->line;

        if (interp->err_file_owned && interp->err_file) {
          free((void *)interp->err_file);
        }

        interp->err_file = prog->file ? strdup(prog->file) : NULL;
        interp->err_file_owned = prog->file ? 1 : 0;
      }

      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->depth--;
  lcl_program_free(prog);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) {
      lcl_ref_dec(last);
    }
  }

  return rc;
}

static char *read_file(const char *path, size_t *out_len) {
  FILE *f;
  long len;
  char *buf;
  size_t nread;

  f = fopen(path, "rb");

  if (!f) {
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  len = ftell(f);
  if (len < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  nread = fread(buf, 1, (size_t)len, f);
  fclose(f);

  if ((long)nread != len) {
    free(buf);
    return NULL;
  }

  buf[len] = '\0';

  if (out_len) {
    *out_len = (size_t)len;
  }

  return buf;
}

/* apply callable arg1 arg2 ... argN
 *
 * Explicit value-dispatch primitive. The complement to `eval`:
 * - `eval`  takes a *source string* and runs it as code.
 * - `apply` takes a *value* and dispatches it as a call.
 *
 * Resolution:
 *   LCL_PROC (non-macro)  -> call via lcl_call_user_proc
 *   LCL_CPROC, normal     -> call fn.proc(interp, N, &argv[1], out)
 *   LCL_CPROC, special    -> error: cannot apply special form
 *   LCL_PROC, is_macro    -> error: cannot apply macro
 *   LCL_STRING            -> resolve as a command name, recurse
 *   anything else         -> error: not callable
 *
 * this is the explicit-dispatch keyword that replaces the implicit
 * one-word-program dispatch once the parse-time rule lands. */
static int c_apply(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  lcl_value *callee;
  int call_argc;
  lcl_value **call_argv;
  lcl_value *resolved = NULL;

  if (!chk_argc(interp, "apply", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  callee = argv[0];
  call_argc = argc - 1;
  call_argv = (call_argc > 0) ? &argv[1] : NULL;

  /* Follow STRING -> command lookup. A single hop is enough in
   * practice (commands resolve to PROC/CPROC values), but we loop to
   * handle the pathological case of a string that resolves to another
   * string. */
  while (callee->type == LCL_STRING) {
    const char *name;
    lcl_value *next = NULL;

    if (lcl_value_to_cstring(interp, callee, &name) != LCL_OK) {
      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_command(interp, name, &next) != LCL_OK) {
      const size_t name_len = strlen(name);
      const size_t prefix_len = 17;
      char *buf = (char *)malloc(name_len + prefix_len + 1);

      if (buf) {
        memcpy(buf, "unknown command: ", prefix_len);
        memcpy(buf + prefix_len, name, name_len + 1);
        LCL_ERR_MSG_DUP(interp, buf);
        free(buf);
      } else {
        LCL_ERR_MSG(interp, "apply: unknown command");
      }

      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(resolved);
    resolved = next;
    callee = resolved;
  }

  if (callee->type == LCL_CPROC) {
    int rc;

    if (callee->as.c_proc.fn->kind == LCL_CK_SPECIAL) {
      LCL_ERR_MSG(interp, "apply: cannot apply special form");
      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    rc = callee->as.c_proc.fn->fn.proc(interp, call_argc, call_argv, out);
    lcl_ref_dec(resolved);
    return rc;
  }

  if (callee->type == LCL_PROC) {
    lcl_proc *p = (lcl_proc *)callee->as.procedure.proc;
    int rc;

    if (p->is_macro) {
      LCL_ERR_MSG(interp, "apply: cannot apply macro");
      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    rc = lcl_call_user_proc(interp, callee, p, NULL, call_argc, call_argv, out);
    lcl_ref_dec(resolved);
    return rc;
  }

  err_expected_got(interp, "apply", "callable", callee);
  lcl_ref_dec(resolved);
  return LCL_RC_ERR;
}

static int s_load(lcl_interp *interp, int argc, const lcl_word **args,
                  lcl_value **out) {
  lcl_value *path_v = NULL;
  const char *path;
  char *src = NULL;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int i;
  int saved_tail_position = interp->in_tail_position;

  if (!chk_argc(interp, "load", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, path_v, &path) != LCL_OK) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  src = read_file(path, NULL);

  if (!src) {
    char msg[192];

    snprintf(msg, sizeof(msg), "load: could not read file \"%.128s\"", path);
    LCL_ERR_MSG_DUP(interp, msg);
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  prog = lcl_compile_report(interp, src, path);
  free(src);

  if (!prog) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(path_v);

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    LCL_ERR_MSG(interp, "load: max recursion depth exceeded");
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    int is_last_cmd = (i == prog->ncmd - 1);

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    /* Bugfix: Only the loaded file's final command inherits the
     * caller's tail position. Mid-file commands run as ordinary
     * top-level scripts. */
    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->in_tail_position = saved_tail_position;
      interp->depth--;
      lcl_program_free(prog);

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc != LCL_RC_OK) {
      if (rc != LCL_RC_RETURN) {
        interp->err_line = cmd->line;

        if (interp->err_file_owned && interp->err_file) {
          free((void *)interp->err_file);
        }

        interp->err_file = prog->file ? strdup(prog->file) : NULL;
        interp->err_file_owned = prog->file ? 1 : 0;
      }

      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->depth--;
  lcl_program_free(prog);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) {
      lcl_ref_dec(last);
    }
  }

  return rc;
}

/* lift_namespaces_to_caller
 *
 * Iterate a dict of (name -> namespace value) and bind each entry into
 * the caller's frame (or into the surrounding namespace builder when
 * `require` is itself called from inside a `namespace` block).
 *
 * Returns LCL_OK on success. On failure, partial bindings may have
 * already been made; caller is responsible for error reporting. */
static lcl_result lift_namespaces_to_caller(lcl_interp *interp,
                                            lcl_value *cached_dict) {
  hash_iter it = {0};
  const char *key;
  lcl_value *value;
  lcl_result final_rc = LCL_OK;

  while (
      hash_table_iterate(cached_dict->as.dict.dictionary, &it, &key, &value)) {
    lcl_result r;

    if (interp->def_depth > interp->def_floor) {
      r = lcl_def_target_bind(interp, key, value);
    } else {
      r = lcl_env_let(&interp->env, key, value);
    }

    lcl_ref_dec(value);

    if (r != LCL_OK) {
      final_rc = LCL_ERROR;
    }
  }

  return final_rc;
}

/* require_str_append: grow-and-append for building dynamic error
 * messages. Returns 1 on success, 0 on OOM (buffer freed, *buf
 * NULLed, so the caller can bail with a static message). */
static int require_str_append(char **buf, size_t *len, size_t *cap,
                              const char *s) {
  size_t n = strlen(s);

  if (*len + n + 1 > *cap) {
    size_t grown_cap = *cap ? *cap : 64;
    char *grown;

    while (*len + n + 1 > grown_cap) {
      grown_cap *= 2;
    }

    grown = (char *)realloc(*buf, grown_cap);

    if (!grown) {
      free(*buf);
      *buf = NULL;
      return 0;
    }

    *buf = grown;
    *cap = grown_cap;
  }

  memcpy(*buf + *len, s, n + 1);
  *len += n;
  return 1;
}

/* require_current_dir
 *
 * Lexical directory of the file currently being evaluated
 * (interp->cur_file), as a malloc'd string. Returns "." for a bare
 * filename (which is CWD-relative anyway), and NULL when evaluation
 * is not file-backed (cur_file unset or the "<bytes>" placeholder
 * from string eval). */
static char *require_current_dir(const lcl_interp *interp) {
  const char *file = interp->cur_file;

  if (!file || strcmp(file, "<bytes>") == 0) {
    return NULL;
  }

  return lcl_path_dirname(file);
}

/* require_resolve
 *
 * Resolve the `require` argument to a cleaned lexical path per the
 * module-loader contract:
 *   - rooted paths ("/...") are used as-is;
 *   - "./" and "../" paths join to the lexical directory of the file
 *     whose evaluation triggered the require (interp->cur_file),
 *     falling back to the argument itself (host-CWD-relative) when
 *     evaluation is not file-backed (REPL, eval of a string);
 *   - bare paths are searched under the registered require roots in
 *     registration order (lcl_add_require_root); with no roots
 *     registered they resolve against the CWD (legacy behavior).
 *
 * Resolution is lexical (lcl-path.h): candidates are normalized by
 * string rules alone and probed with fopen. The filesystem is never
 * asked to canonicalize — symlinks are not resolved, and the result
 * stays relative when its inputs are relative; what a relative name
 * means is host state, per the contract in lcl.h.
 *
 * On success returns the malloc'd lexical path of the first openable
 * candidate. On failure returns NULL with an interp error naming the
 * argument and every candidate path attempted. */
static char *require_resolve(lcl_interp *interp, const char *arg) {
  char **candidates = NULL;
  size_t ncand = 0;
  size_t i;
  char *resolved = NULL;
  int oom = 0;
  int is_dot_relative =
      (arg[0] == '.' && (arg[1] == '/' || (arg[1] == '.' && arg[2] == '/')));

  if (arg[0] == '\0') {
    LCL_ERR_MSG(interp, "require: empty path");
    return NULL;
  }

  if (is_dot_relative) {
    char *base = require_current_dir(interp);

    candidates = (char **)malloc(sizeof(*candidates));
    ncand = 1;

    if (candidates) {
      candidates[0] = base ? lcl_path_join(base, arg) : lcl_path_clean(arg);
    }

    free(base);
  } else if (arg[0] != '/' && interp->require_roots_len > 0) {
    ncand = interp->require_roots_len;
    candidates = (char **)malloc(ncand * sizeof(*candidates));

    if (candidates) {
      for (i = 0; i < ncand; i++) {
        candidates[i] = lcl_path_join(interp->require_roots[i], arg);
      }
    }
  } else {
    /* Rooted path, or a bare path with no search roots registered:
     * use the argument as-is (bare stays CWD-relative for
     * compatibility). */
    candidates = (char **)malloc(sizeof(*candidates));
    ncand = 1;

    if (candidates) {
      candidates[0] = lcl_path_clean(arg);
    }
  }

  if (!candidates) {
    LCL_ERR_MSG(interp, "require: out of memory resolving path");
    return NULL;
  }

  /* Existence is not a question separate from opening: probe each
   * cleaned candidate with fopen and keep the first that opens. */
  for (i = 0; i < ncand; i++) {
    if (!candidates[i]) {
      oom = 1;
      continue;
    }

    if (!resolved) {
      FILE *probe = fopen(candidates[i], "rb");

      if (probe) {
        fclose(probe);
        resolved = candidates[i];
        candidates[i] = NULL;
      }
    }
  }

  if (!resolved) {
    if (oom) {
      LCL_ERR_MSG(interp, "require: out of memory resolving path");
    } else {
      char *msg = NULL;
      size_t len = 0;
      size_t cap = 0;
      int ok = require_str_append(&msg, &len, &cap, "require: cannot find \"");

      ok = ok && require_str_append(&msg, &len, &cap, arg);
      ok = ok && require_str_append(&msg, &len, &cap, "\" (tried");

      for (i = 0; ok && i < ncand; i++) {
        ok = require_str_append(&msg, &len, &cap, i == 0 ? " \"" : ", \"");
        ok = ok && require_str_append(&msg, &len, &cap, candidates[i]);
        ok = ok && require_str_append(&msg, &len, &cap, "\"");
      }

      ok = ok && require_str_append(&msg, &len, &cap, ")");

      if (ok) {
        LCL_ERR_MSG_DUP(interp, msg);
        free(msg);
      } else {
        LCL_ERR_MSG(interp, "require: cannot resolve path");
      }
    }
  }

  for (i = 0; i < ncand; i++) {
    free(candidates[i]);
  }

  free(candidates);
  return resolved;
}

/* require_stack_contains: is the module identified by `key` currently
 * being evaluated by an in-progress require? */
static int require_stack_contains(const lcl_interp *interp, const char *key) {
  size_t i;

  for (i = 0; i < interp->require_stack_len; i++) {
    if (strcmp(interp->require_stack[i].key, key) == 0) {
      return 1;
    }
  }

  return 0;
}

/* require_stack_push: record a module as in-progress. `key` is the
 * identity compared by require_stack_contains; `path` is the lexical
 * path shown in cycle diagnostics. Returns 1 on success, 0 on OOM. */
static int require_stack_push(lcl_interp *interp, const char *key,
                              const char *path) {
  char *key_copy = strdup(key);
  char *path_copy = strdup(path);

  if (!key_copy || !path_copy) {
    free(key_copy);
    free(path_copy);
    return 0;
  }

  if (interp->require_stack_len == interp->require_stack_cap) {
    size_t cap = interp->require_stack_cap ? interp->require_stack_cap * 2 : 8;
    lcl_require_entry *grown = (lcl_require_entry *)realloc(
        interp->require_stack, cap * sizeof(*grown));

    if (!grown) {
      free(key_copy);
      free(path_copy);
      return 0;
    }

    interp->require_stack = grown;
    interp->require_stack_cap = cap;
  }

  interp->require_stack[interp->require_stack_len].key = key_copy;
  interp->require_stack[interp->require_stack_len].path = path_copy;
  interp->require_stack_len++;
  return 1;
}

static void require_stack_pop(lcl_interp *interp) {
  if (interp->require_stack_len > 0) {
    interp->require_stack_len--;
    free(interp->require_stack[interp->require_stack_len].key);
    free(interp->require_stack[interp->require_stack_len].path);
    interp->require_stack[interp->require_stack_len].key = NULL;
    interp->require_stack[interp->require_stack_len].path = NULL;
  }
}

/* require_cycle_error: build "require: dependency cycle: a -> b -> a"
 * from the in-progress stack, starting at the first occurrence of
 * `key`. The chain is printed with lexical paths. */
static void require_cycle_error(lcl_interp *interp, const char *key,
                                const char *path) {
  char *msg = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t start = 0;
  size_t i;
  int ok;

  for (i = 0; i < interp->require_stack_len; i++) {
    if (strcmp(interp->require_stack[i].key, key) == 0) {
      start = i;
      break;
    }
  }

  ok = require_str_append(&msg, &len, &cap, "require: dependency cycle: ");

  for (i = start; ok && i < interp->require_stack_len; i++) {
    ok = require_str_append(&msg, &len, &cap, interp->require_stack[i].path);
    ok = ok && require_str_append(&msg, &len, &cap, " -> ");
  }

  ok = ok && require_str_append(&msg, &len, &cap, path);

  if (ok) {
    LCL_ERR_MSG_DUP(interp, msg);
    free(msg);
  } else {
    LCL_ERR_MSG(interp, "require: dependency cycle detected");
  }
}

/* require <path>
 *
 * Scoped load: evaluate <path> in a fresh frame parented to global,
 * collect every top-level binding whose value is a namespace, and lift
 * just those into the caller's scope. Loose let/var/proc bindings are
 * discarded. Subsequent calls with the same resolved absolute path are
 * served from cache without re-evaluating the file.
 *
 * Path resolution (see require_resolve): "/rooted" as-is, "./" and
 * "../" relative to the requiring file's lexical directory, bare
 * names via the registered search roots (or CWD when none are
 * registered). The cleaned lexical path is the module's identity —
 * the cache and cycle key — unless the host installed a stronger
 * identity via lcl_set_module_key_fn (the key never affects
 * resolution or diagnostics, only identity). A file that requires
 * (directly or transitively) a module already being evaluated by
 * require is a dependency-cycle error.
 *
 * Differs from `load` in that:
 *   - `load` evaluates inline at the call site (textual include);
 *     `require` evaluates in an isolated frame (module import).
 *   - `load` runs the file every call; `require` caches by abs path.
 *   - `load` exposes every top-level binding; `require` exposes only
 *     namespaces (the explicit "module surface").
 *   - `load` stays CWD/caller-relative; only `require` implements the
 *     module-loader resolution contract. */
static int s_require(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_value *path_v = NULL;
  const char *path;
  char *mod_path = NULL;
  char *mod_key = NULL;
  lcl_value *cached_dict = NULL;
  char *src = NULL;
  lcl_program *prog = NULL;
  lcl_frame *overlay = NULL;
  lcl_frame *saved_frame = NULL;
  lcl_frame *global_frame = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int i;
  int saved_tail_position;
  const char *saved_cur_file;
  int saved_cur_line;
  hash_iter it = {0};
  const char *key;
  lcl_value *value;

  if (!chk_argc(interp, "require", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, path_v, &path) != LCL_OK) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  mod_path = require_resolve(interp, path);
  lcl_ref_dec(path_v);

  if (!mod_path) {
    return LCL_RC_ERR;
  }

  if (interp->module_key_fn) {
    mod_key = interp->module_key_fn(mod_path, interp->module_key_ud);
  }

  if (!mod_key) {
    mod_key = strdup(mod_path);
  }

  if (!mod_key) {
    free(mod_path);
    LCL_ERR_MSG(interp, "require: out of memory");
    return LCL_RC_ERR;
  }

  /* Cache lookup. A hit means we already lifted this module's
   * namespaces once; just re-lift them at the new call site. */
  if (interp->require_cache &&
      lcl_dict_get(interp->require_cache, mod_key, &cached_dict) == LCL_OK) {
    lcl_result lift_rc = lift_namespaces_to_caller(interp, cached_dict);
    lcl_ref_dec(cached_dict);
    free(mod_path);
    free(mod_key);

    if (lift_rc != LCL_OK) {
      LCL_ERR_MSG(interp, "require: failed to bind cached namespace");
      return LCL_RC_ERR;
    }

    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  /* Cycle guard: a cache miss on a module that is already mid-require
   * means the dependency graph loops back on itself. Without this the
   * recursion would only stop at the interpreter depth limit. */
  if (require_stack_contains(interp, mod_key)) {
    require_cycle_error(interp, mod_key, mod_path);
    free(mod_path);
    free(mod_key);
    return LCL_RC_ERR;
  }

  /* Cache miss: read, compile, evaluate in an isolated overlay frame. */
  src = read_file(mod_path, NULL);

  if (!src) {
    char msg[192];

    snprintf(msg, sizeof(msg), "require: could not read file \"%.128s\"",
             mod_path);
    LCL_ERR_MSG_DUP(interp, msg);
    free(mod_path);
    free(mod_key);
    return LCL_RC_ERR;
  }

  prog = lcl_compile_report(interp, src, mod_path);
  free(src);

  if (!prog) {
    free(mod_path);
    free(mod_key);
    return LCL_RC_ERR;
  }

  global_frame = find_global_frame(interp->env.frame);
  overlay = lcl_frame_new(global_frame);

  if (!overlay) {
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: out of memory");
    return LCL_RC_ERR;
  }

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: max recursion depth exceeded");
    return LCL_RC_ERR;
  }

  if (!require_stack_push(interp, mod_key, mod_path)) {
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: out of memory");
    return LCL_RC_ERR;
  }

  saved_frame = interp->env.frame;
  saved_tail_position = interp->in_tail_position;
  saved_cur_file = interp->cur_file;
  saved_cur_line = interp->cur_line;
  interp->env.frame = overlay;
  interp->depth++;

  /* The body of a require runs for side effects + collected
   * namespaces; suppress tail-position propagation so a self-recursive
   * call inside cannot escape via LCL_RC_TAILCALL. */
  interp->in_tail_position = 0;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];

    /* Track the module file as the current source file so that a
     * nested `require ./x` inside it resolves relative to this file,
     * and error context points into it. Restored after the loop. */
    interp->cur_file = prog->file;
    interp->cur_line = cmd->line;

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc != LCL_RC_OK) {
      if (rc != LCL_RC_RETURN) {
        interp->err_line = cmd->line;

        if (interp->err_file_owned && interp->err_file) {
          free((void *)interp->err_file);
        }

        interp->err_file = prog->file ? strdup(prog->file) : NULL;
        interp->err_file_owned = prog->file ? 1 : 0;
      }

      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->depth--;
  interp->env.frame = saved_frame;
  interp->cur_file = saved_cur_file;
  interp->cur_line = saved_cur_line;
  require_stack_pop(interp);

  if (last) {
    lcl_ref_dec(last);
    last = NULL;
  }

  if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
    lcl_frame_clear(overlay);
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    return rc;
  }

  /* Collect namespaces from the overlay's locals. Procs defined
   * inside namespaces in the file still close over the overlay frame,
   * so we cannot free the overlay outright -- its refcount will drop
   * when nothing references it. */
  cached_dict = lcl_dict_new();

  if (!cached_dict) {
    lcl_frame_clear(overlay);
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: out of memory creating cache entry");
    return LCL_RC_ERR;
  }

  while (hash_table_iterate(overlay->locals, &it, &key, &value)) {
    if (value->type == LCL_NAMESPACE) {
      if (lcl_dict_put(&cached_dict, key, value) != LCL_OK) {
        lcl_ref_dec(value);
        lcl_ref_dec(cached_dict);
        lcl_frame_clear(overlay);
        lcl_frame_ref_dec(overlay);
        lcl_program_free(prog);
        free(mod_path);
        free(mod_key);
        LCL_ERR_MSG(interp, "require: out of memory caching namespace");
        return LCL_RC_ERR;
      }
    }

    lcl_ref_dec(value);
  }

  /* Drop our reference to the overlay; closures inside lifted
   * namespaces hold their own refs. */
  lcl_frame_ref_dec(overlay);
  lcl_program_free(prog);

  /* Install in the require cache. */
  if (!interp->require_cache) {
    interp->require_cache = lcl_dict_new();

    if (!interp->require_cache) {
      lcl_ref_dec(cached_dict);
      free(mod_path);
      free(mod_key);
      LCL_ERR_MSG(interp, "require: out of memory creating cache");
      return LCL_RC_ERR;
    }
  }

  if (lcl_dict_put(&interp->require_cache, mod_key, cached_dict) != LCL_OK) {
    lcl_ref_dec(cached_dict);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: failed to cache require result");
    return LCL_RC_ERR;
  }

  free(mod_path);
  free(mod_key);
  mod_path = NULL;
  mod_key = NULL;

  /* Lift the just-collected namespaces into the caller. */
  if (lift_namespaces_to_caller(interp, cached_dict) != LCL_OK) {
    lcl_ref_dec(cached_dict);
    LCL_ERR_MSG(interp, "require: failed to bind namespace into caller");
    return LCL_RC_ERR;
  }

  lcl_ref_dec(cached_dict);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* find the end of a callable expression starting with [ */
static const char *find_bracket_end(const char *s) {
  int depth = 1;
  s++;

  while (*s && depth > 0) {
    if (*s == '[') {
      depth++;
    } else if (*s == ']') {
      depth--;
    }
    s++;
  }
  return s;
}

/* ============================================================================
 * Thread-first operator: -> initial {form1} {form2} ...
 * Threads the value through each form as the first argument.
 * Example: -> $d {get b} becomes: get $d b
 *          -> $d {put c 3} {del a} becomes: del [put $d c 3] a
 *          -> 10 {$f} becomes: [$f 10] (call lambda in variable)
 *          -> 10 {[lambda {x} ...]} becomes: [[lambda {x} ...] 10]
 *
 * Implementation: Uses a temporary variable $_thread_ to hold the current
 * value, allowing it to preserve type information (dict, list, etc.)
 * ============================================================================
 */
static int s_thread_first(lcl_interp *interp, int argc, const lcl_word **args,
                          lcl_value **out) {
  lcl_value *current = NULL;
  lcl_value *form_v = NULL;
  int i;
  int rc;

  if (argc < 1) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  rc = lcl_eval_word(interp, args[0], &current);

  if (rc != LCL_RC_OK) {
    return rc;
  }

  for (i = 1; i < argc; i++) {
    const char *form;
    const char *cmd_end;
    const char *rest;
    char *threaded = NULL;
    size_t cmd_len;
    size_t total;
    lcl_value *result = NULL;

    lcl_env_let(&interp->env, "_thread_", current);
    rc = lcl_eval_word_to_str(interp, args[i], &form_v);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(current);
      return rc;
    }

    if (lcl_value_to_cstring(interp, form_v, &form) != LCL_OK) {
      lcl_ref_dec(form_v);
      lcl_ref_dec(current);
      return LCL_RC_ERR;
    }

    if (form[0] == '[') {
      cmd_end = find_bracket_end(form);
      cmd_len = (size_t)(cmd_end - form);
      rest = cmd_end;

      while (*rest == ' ' || *rest == '\t') {
        rest++;
      }

      total = 1 + cmd_len + 11 + strlen(rest) + 2;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (*rest) {
        sprintf(threaded, "[%.*s $_thread_ %s]", (int)cmd_len, form, rest);
      } else {
        sprintf(threaded, "[%.*s $_thread_]", (int)cmd_len, form);
      }
    } else if (form[0] == '$') {
      cmd_end = form + 1;

      while (*cmd_end && *cmd_end != ' ' && *cmd_end != '\t' &&
             *cmd_end != '\n') {
        cmd_end++;
      }

      cmd_len = (size_t)(cmd_end - form);
      rest = cmd_end;

      while (*rest == ' ' || *rest == '\t') {
        rest++;
      }

      total = 1 + cmd_len + 11 + strlen(rest) + 2;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (*rest) {
        sprintf(threaded, "[%.*s $_thread_ %s]", (int)cmd_len, form, rest);
      } else {
        sprintf(threaded, "[%.*s $_thread_]", (int)cmd_len, form);
      }
    } else {
      cmd_end = form;

      while (*cmd_end && *cmd_end != ' ' && *cmd_end != '\t' &&
             *cmd_end != '\n') {
        cmd_end++;
      }

      cmd_len = (size_t)(cmd_end - form);
      rest = cmd_end;

      while (*rest == ' ' || *rest == '\t') {
        rest++;
      }

      total = cmd_len + 12 + strlen(rest) + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (*rest) {
        sprintf(threaded, "%.*s $_thread_ %s", (int)cmd_len, form, rest);
      } else {
        sprintf(threaded, "%.*s $_thread_", (int)cmd_len, form);
      }
    }

    lcl_ref_dec(form_v);
    form_v = NULL;

    rc = lcl_eval_string(interp, threaded, &result);
    free(threaded);

    if (rc != LCL_RC_OK) {
      /* Fuzz bugfix: RC_RETURN delivers its value via result; pass it
       * up like any special's body would (cf. s_if). */
      lcl_ref_dec(current);
      *out = result;

      return rc;
    }

    lcl_ref_dec(current);
    current = result;
  }

  *out = current;
  return LCL_RC_OK;
}

/* Thread-last operator: ->> initial {form1} {form2} ...
 * Threads the value through each form as the last argument.
 * Example: ->> $d {cmd a b} becomes: cmd a b $d
 *          ->> 10 {$f a} becomes: [$f a 10]
 *          ->> 10 {[lambda {x} ...]} becomes: [[lambda {x} ...] 10]
 */
static int s_thread_last(lcl_interp *interp, int argc, const lcl_word **args,
                         lcl_value **out) {
  lcl_value *current = NULL;
  lcl_value *form_v = NULL;
  int i;
  int rc;

  if (argc < 1) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  rc = lcl_eval_word(interp, args[0], &current);

  if (rc != LCL_RC_OK) {
    return rc;
  }

  for (i = 1; i < argc; i++) {
    const char *form;
    char *threaded = NULL;
    size_t total;
    lcl_value *result = NULL;

    lcl_env_let(&interp->env, "_thread_", current);
    rc = lcl_eval_word_to_str(interp, args[i], &form_v);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(current);

      return rc;
    }

    if (lcl_value_to_cstring(interp, form_v, &form) != LCL_OK) {
      lcl_ref_dec(form_v);
      lcl_ref_dec(current);
      return LCL_RC_ERR;
    }

    if (form[0] == '[' || form[0] == '$') {
      total = 1 + strlen(form) + 11 + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }
      sprintf(threaded, "[%s $_thread_]", form);
    } else {
      total = strlen(form) + 11 + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      sprintf(threaded, "%s $_thread_", form);
    }

    lcl_ref_dec(form_v);
    form_v = NULL;

    rc = lcl_eval_string(interp, threaded, &result);
    free(threaded);

    if (rc != LCL_RC_OK) {
      /* Fuzz bugfix: RC_RETURN delivers its value via result; pass it
       * up like any special's body would (cf. s_if). */
      lcl_ref_dec(current);
      *out = result;

      return rc;
    }

    lcl_ref_dec(current);
    current = result;
  }

  *out = current;
  return LCL_RC_OK;
}

static int s_proc(lcl_interp *interp, int argc, const lcl_word **args,
                  lcl_value **out) {
  /* proc name {params} {body}
   * Desugars to: let name [lambda name {params} {body}] */
  lcl_value *name_v = NULL;
  lcl_value *lam = NULL;
  const char *name_str;
  int rc;

  if (!chk_argc(interp, "proc", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (strstr(name_str, "::") && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "proc: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::proc'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  rc = make_lambda(interp, name_str, args[1], args[2], &lam);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return rc;
  }

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_bind(interp, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "proc: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "proc: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(lam);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static int s_macro(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  /* macro name {params} {body}
   * Like proc, but the return value is compiled and evaluated
   * in the caller's frame at dispatch time. */
  lcl_value *name_v = NULL;
  lcl_value *lam = NULL;
  const char *name_str;
  int rc;

  if (!chk_argc(interp, "macro", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (strstr(name_str, "::") && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "macro: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::proc'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  rc = make_lambda(interp, name_str, args[1], args[2], &lam);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return rc;
  }

  ((lcl_proc *)lam->as.procedure.proc)->is_macro = 1;

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_bind(interp, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "macro: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "macro: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(lam);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* macroexpand name arg1 arg2 ...
 * Calls the macro's proc body and returns the template string
 * WITHOUT compiling/evaluating it. This is the explicit mechanism
 * for using macro expansions as values:
 *   eval [macroexpand my_macro args]
 */
static int s_macroexpand(lcl_interp *interp, int argc, const lcl_word **args,
                         lcl_value **out) {
  lcl_value *callee = NULL;
  lcl_value **argv = NULL;
  lcl_proc *p;
  int nargs;
  int i;
  int rc;

  if (!chk_argc(interp, "macroexpand", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  rc = lcl_eval_word(interp, args[0], &callee);

  if (rc != LCL_RC_OK) {
    return rc;
  }

  if (callee->type == LCL_STRING) {
    lcl_value *name = callee;
    const char *name_str;
    callee = NULL;

    if (lcl_value_to_cstring(interp, name, &name_str) != LCL_OK) {
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_command(interp, name_str, &callee) != LCL_OK) {
      LCL_ERR_MSG(interp, "macroexpand: unknown command");
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(name);
  }

  if (callee->type != LCL_PROC) {
    lcl_ref_dec(callee);
    LCL_ERR_MSG(interp, "macroexpand: not a procedure");
    return LCL_RC_ERR;
  }

  p = (lcl_proc *)callee->as.procedure.proc;

  if (!p->is_macro) {
    lcl_ref_dec(callee);
    LCL_ERR_MSG(interp, "macroexpand: not a macro");
    return LCL_RC_ERR;
  }

  nargs = argc - 1;

  if (nargs > 0) {
    argv = malloc(sizeof(lcl_value *) * (size_t)nargs);

    if (!argv) {
      LCL_ERR_MSG(interp, "macroexpand: out of memory");
      lcl_ref_dec(callee);
      return LCL_RC_ERR;
    }

    for (i = 0; i < nargs; i++) {
      rc = lcl_eval_word(interp, args[i + 1], &argv[i]);

      if (rc != LCL_RC_OK) {
        while (--i >= 0) {
          lcl_ref_dec(argv[i]);
        }

        free(argv);
        lcl_ref_dec(callee);
        return rc;
      }
    }
  }

  rc = lcl_call_user_proc(interp, callee, p, NULL, nargs, argv, out);

  for (i = 0; i < nargs; i++) {
    lcl_ref_dec(argv[i]);
  }

  free(argv);
  lcl_ref_dec(callee);
  return rc;
}

/*
 * List Commands
 */

/* list ?value ...? - construct a list from arguments */
static int c_list(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  lcl_value *list;
  int i;

  list = lcl_list_new();

  if (!list) {
    LCL_ERR_MSG(interp, "list: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc; i++) {
    if (lcl_list_push(&list, argv[i]) != LCL_OK) {
      LCL_ERR_MSG(interp, "list: out of memory");
      lcl_ref_dec(list);
      return LCL_RC_ERR;
    }
  }

  *out = list;
  return LCL_RC_OK;
}

/* lindex list ?index ...? - get element(s) from list by index */
static int c_lindex(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  lcl_value *list;
  long idx;
  (void)interp;

  if (!chk_argc(interp, "List::index", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (argc == 1) {
    *out = lcl_ref_inc(list);

    return LCL_RC_OK;
  }

  if (argc == 2) {
    if (list->type != LCL_LIST) {
      if (!arg_int(interp, "List::index", argv[1], &idx)) {
        return LCL_RC_ERR;
      }

      if (idx == 0) {
        *out = lcl_ref_inc(list);

        return LCL_RC_OK;
      }

      *out = lcl_string_new("");

      return LCL_RC_OK;
    }

    if (!arg_int(interp, "List::index", argv[1], &idx)) {
      return LCL_RC_ERR;
    }

    if (idx < 0) {
      *out = lcl_string_new("");

      return LCL_RC_OK;
    }

    if (lcl_list_get(list, (size_t)idx, out) != LCL_OK) {
      *out = lcl_string_new("");
    }

    return LCL_RC_OK;
  }

  {
    lcl_value *current = lcl_ref_inc(list);
    int i;

    for (i = 1; i < argc; i++) {
      lcl_value *next = NULL;

      if (current->type != LCL_LIST) {
        if (!arg_int(interp, "List::index", argv[i], &idx)) {
          lcl_ref_dec(current);

          return LCL_RC_ERR;
        }

        if (idx == 0) {
          continue;
        }

        lcl_ref_dec(current);
        *out = lcl_string_new("");

        return LCL_RC_OK;
      }

      if (!arg_int(interp, "List::index", argv[i], &idx)) {
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (idx < 0 || lcl_list_get(current, (size_t)idx, &next) != LCL_OK) {
        lcl_ref_dec(current);
        *out = lcl_string_new("");

        return LCL_RC_OK;
      }

      lcl_ref_dec(current);
      current = next;
    }

    *out = current;
    return LCL_RC_OK;
  }
}

/* List::range start end ?step? - generate numeric range [start, end) */
static int c_lrange(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  lcl_value *result;
  lcl_value *num;
  long start;
  long end;
  long step;
  long i;
  (void)interp;

  if (!chk_argc(interp, "List::range", argc, 2, 3)) {
    return LCL_RC_ERR;
  }

  if (!arg_int(interp, "List::range", argv[0], &start)) {
    return LCL_RC_ERR;
  }

  if (!arg_int(interp, "List::range", argv[1], &end)) {
    return LCL_RC_ERR;
  }

  step = 1;

  if (argc == 3) {
    if (!arg_int(interp, "List::range", argv[2], &step)) {
      return LCL_RC_ERR;
    }

    if (step == 0) {
      LCL_ERR_MSG(interp, "List::range: step must not be zero");
      return LCL_RC_ERR;
    }
  }

  result = lcl_list_new();

  if (!result) {
    LCL_ERR_MSG(interp, "List::range: out of memory");
    return LCL_RC_ERR;
  }

  if (step > 0) {
    i = start;

    while (i < end) {
      long next;
      num = lcl_int_new(i);

      if (!num || lcl_list_push(&result, num) != LCL_OK) {
        LCL_ERR_MSG(interp, "List::range: out of memory");
        if (num) {
          lcl_ref_dec(num);
        }

        lcl_ref_dec(result);

        return LCL_RC_ERR;
      }

      lcl_ref_dec(num);

      /* Bugfix: stop on overflow rather than wrapping around -- the
       * wrap would produce a value < end again, looping forever. */
      if (!safe_add_long(i, step, &next)) {
        break;
      }

      i = next;
    }
  } else {
    i = start;

    while (i > end) {
      long next;
      num = lcl_int_new(i);

      if (!num || lcl_list_push(&result, num) != LCL_OK) {
        LCL_ERR_MSG(interp, "List::range: out of memory");
        if (num) {
          lcl_ref_dec(num);
        }

        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }

      lcl_ref_dec(num);

      /* Bugfix: stop on overflow (step is negative here). */
      if (!safe_add_long(i, step, &next)) {
        break;
      }

      i = next;
    }
  }

  *out = result;

  return LCL_RC_OK;
}

/* join list ?separator? - join list elements with separator (default space) */
static int c_join(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  lcl_value *list;
  const char *sep = " ";
  size_t sep_len;
  size_t len;
  size_t i;
  char *buf = NULL;
  size_t buf_len = 0;
  size_t buf_cap = 0;

  if (!chk_argc(interp, "String::join", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (argc == 2) {
    if (!arg_str(interp, "String::join", argv[1], &sep)) {
      return LCL_RC_ERR;
    }
  }

  sep_len = strlen(sep);

  if (list->type != LCL_LIST) {
    const char *list_str;
    if (lcl_value_to_cstring(interp, list, &list_str) != LCL_OK) {
      return LCL_RC_ERR;
    }
    *out = lcl_string_new(list_str);

    return LCL_RC_OK;
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *elem_str;
    size_t elem_len;

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      continue;
    }

    if (lcl_value_to_cstring(interp, elem, &elem_str) != LCL_OK) {
      lcl_ref_dec(elem);
      free(buf);
      return LCL_RC_ERR;
    }
    elem_len = strlen(elem_str);

    if (i > 0 && sep_len > 0) {
      if (!buf_append(&buf, &buf_len, &buf_cap, sep, sep_len)) {
        LCL_ERR_MSG(interp, "String::join: out of memory");
        lcl_ref_dec(elem);
        free(buf);

        return LCL_RC_ERR;
      }
    }

    if (!buf_append(&buf, &buf_len, &buf_cap, elem_str, elem_len)) {
      LCL_ERR_MSG(interp, "String::join: out of memory");
      lcl_ref_dec(elem);
      free(buf);

      return LCL_RC_ERR;
    }

    lcl_ref_dec(elem);
  }

  *out = lcl_string_new(buf ? buf : "");
  free(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* split string ?splitChars? - split string into list (default split on each
 * char) */
static int c_split(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  const char *str;
  const char *split_chars = NULL;
  lcl_value *result;

  if (!chk_argc(interp, "String::split", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::split", argv[0], &str)) {
    return LCL_RC_ERR;
  }

  if (argc == 2) {
    if (!arg_str(interp, "String::split", argv[1], &split_chars)) {
      return LCL_RC_ERR;
    }
  }

  result = lcl_list_new();

  if (!result) {
    LCL_ERR_MSG(interp, "String::split: out of memory");
    return LCL_RC_ERR;
  }

  if (!split_chars || *split_chars == '\0') {
    const char *p = str;

    while (*p) {
      /* C89: aggregate initializers must be constant expressions. */
      char c[2];
      lcl_value *elem;

      c[0] = *p;
      c[1] = '\0';
      elem = lcl_string_new(c);

      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "String::split: out of memory");
        if (elem) {
          lcl_ref_dec(elem);
        }

        lcl_ref_dec(result);

        return LCL_RC_ERR;
      }

      lcl_ref_dec(elem);
      p++;
    }
  } else {
    const char *p = str;
    const char *start = str;

    while (*p) {
      if (strchr(split_chars, *p)) {
        size_t len = (size_t)(p - start);
        char *word = (char *)malloc(len + 1);
        lcl_value *elem;

        if (!word) {
          LCL_ERR_MSG(interp, "String::split: out of memory");
          lcl_ref_dec(result);
          return LCL_RC_ERR;
        }

        memcpy(word, start, len);
        word[len] = '\0';
        elem = lcl_string_new(word);
        free(word);

        if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
          LCL_ERR_MSG(interp, "String::split: out of memory");
          if (elem) {
            lcl_ref_dec(elem);
          }
          lcl_ref_dec(result);

          return LCL_RC_ERR;
        }

        lcl_ref_dec(elem);
        start = p + 1;
      }

      p++;
    }

    {
      lcl_value *elem = lcl_string_new(start);
      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "String::split: out of memory");
        if (elem) {
          lcl_ref_dec(elem);
        }
        lcl_ref_dec(result);

        return LCL_RC_ERR;
      }

      lcl_ref_dec(elem);
    }
  }

  *out = result;
  return LCL_RC_OK;
}

/*
 * Generic Type-Directed Operations
 */

/* len x - returns length of list, dict, or string */
static int c_len(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  if (!chk_argc(interp, "len", argc, 1, 1)) {
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
    return err_expected_got(interp, "len", "list, dict, string, or namespace",
                            argv[0]);
  }
}

/* empty? x - returns 1 if container is empty */
static int c_empty(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  if (!chk_argc(interp, "empty?", argc, 1, 1)) {
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
    return err_expected_got(interp, "empty?", "list, dict, or string", argv[0]);
  }
}

/* get x k [default] - get element by key/index */
static int c_generic_get(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  if (!chk_argc(interp, "get", argc, 2, 3)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;

    if (!arg_int(interp, "get", argv[1], &idx)) {
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

    if (!arg_int(interp, "get", argv[1], &idx)) {
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
    return err_expected_got(interp, "get", "list, dict, string, or namespace",
                            argv[0]);
  }
}

/* put x k v - return new container with element added/replaced */
static int c_put(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  if (!chk_argc(interp, "put", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;
    lcl_value *copy;

    if (!arg_int(interp, "put", argv[1], &idx)) {
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

  default: return err_expected_got(interp, "put", "list or dict", argv[0]);
  }
}

/* del x k - return new container without element */
static int c_del(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  if (!chk_argc(interp, "del", argc, 2, 2)) {
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

  default:
    /* TODO(bjh) del on list not implemented for MVP - could remove by index */
    return err_expected_got(interp, "del", "dict", argv[0]);
  }
}

static int c_ns_has(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out);

/* has? x k - membership test: list element, dict key, substring, or
 * namespace binding */
static int c_has(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  if (!chk_argc(interp, "has?", argc, 2, 2)) {
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

  case LCL_NAMESPACE: return c_ns_has(interp, argc, argv, out);

  default:
    return err_expected_got(interp, "has?", "list, dict, string, or namespace",
                            argv[0]);
  }
}

/* ============================================================================
 * Type Predicates
 * ============================================================================
 */

static int c_is_list(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "list?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_LIST ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_dict(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "dict?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_DICT ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_string(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "string?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_STRING ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_opaque(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "opaque?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_OPAQUE ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_number(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "number?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_INT || argv[0]->type == LCL_FLOAT) {
    *out = lcl_int_new(1);
  } else if (argv[0]->type == LCL_STRING) {
    const char *s = lcl_value_to_string(argv[0]);

    *out = lcl_int_new(
        s && lcl_num_text_classify(s, strlen(s)) != LCL_NUM_NONE ? 1 : 0);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

static int c_is_int(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "int?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_INT ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_float(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "float?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_FLOAT ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_proc(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "proc?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      argv[0]->type == LCL_PROC || argv[0]->type == LCL_CPROC ? 1 : 0);

  return LCL_RC_OK;
}

/* arity fn - return (min max) where max is -1 for unbounded */
static int c_arity(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  lcl_value *func;
  lcl_value *result;
  lcl_value *min_val;
  lcl_value *max_val;
  int min_args;
  int max_args;

  if (!chk_argc(interp, "arity", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  func = argv[0];

  if (func->type == LCL_PROC) {
    lcl_proc *p = func->as.procedure.proc;
    min_args = p->pspec.n_required;
    max_args =
        p->pspec.rest_name ? -1 : (p->pspec.n_required + p->pspec.n_optional);
  } else if (func->type == LCL_CPROC) {
    /* C procs don't have structured arity - return (0 -1) for variadic */
    min_args = 0;
    max_args = -1;
  } else {
    return err_expected_got(interp, "arity", "proc", func);
  }

  result = lcl_list_new();
  min_val = lcl_int_new(min_args);
  max_val = lcl_int_new(max_args);

  lcl_list_push(&result, min_val);
  lcl_list_push(&result, max_val);

  lcl_ref_dec(min_val);
  lcl_ref_dec(max_val);

  *out = result;
  return LCL_RC_OK;
}

/* int x - convert value to integer */
static int c_to_int(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  long val;
  double fval;

  if (!chk_argc(interp, "int", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_INT) {
    *out = lcl_ref_inc(argv[0]);
    return LCL_RC_OK;
  }

  /* Bugfix: lcl_value_to_int handles LCL_FLOAT and LCL_STRING safely
   * (rejects NaN/Inf, out-of-range floats, and ERANGE strtol
   * results). */
  if (lcl_value_to_int(argv[0], &val) == LCL_OK) {
    *out = lcl_int_new(val);
    return LCL_RC_OK;
  }

  /* String fallback: parse as float, then range-check before casting. */
  if (lcl_value_to_float(argv[0], &fval) == LCL_OK) {
    if (lcl_double_to_long(fval, &val) != LCL_OK) {
      LCL_ERR_MSG(interp, "int: value out of range");
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(val);
    return LCL_RC_OK;
  }

  return err_expected_got(interp, "int", "number", argv[0]);
}

/* float x - convert value to float */
static int c_to_float(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  double val;

  if (!chk_argc(interp, "float", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_FLOAT) {
    *out = lcl_ref_inc(argv[0]);
    return LCL_RC_OK;
  }

  if (argv[0]->type == LCL_INT) {
    *out = lcl_float_new((double)argv[0]->as.i);
    return LCL_RC_OK;
  }

  if (lcl_value_to_float(argv[0], &val) != LCL_OK) {
    return err_expected_got(interp, "float", "number", argv[0]);
  }

  *out = lcl_float_new((double)val);
  return LCL_RC_OK;
}

/* ============================================================================
 * Namespaced List Operations
 * ============================================================================
 */

/* list::push x v - return new list with v appended */
int c_list_push(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  lcl_value *copy;

  if (!chk_argc(interp, "List::push", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::push", "list", argv[0]);
  }

  copy = lcl_ref_inc(argv[0]);

  if (lcl_list_push(&copy, argv[1]) != LCL_OK) {
    LCL_ERR_MSG(interp, "List::push: out of memory");
    lcl_ref_dec(copy);

    return LCL_RC_ERR;
  }

  *out = copy;

  return LCL_RC_OK;
}

/* list::pop x - return new list without last element */
int c_list_pop(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  lcl_value *copy;
  size_t len;
  size_t i;

  if (!chk_argc(interp, "List::pop", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::pop", "list", argv[0]);
  }

  len = lcl_list_len(argv[0]);

  if (len == 0) {
    LCL_ERR_MSG(interp, "List::pop: list is empty");
    return LCL_RC_ERR;
  }

  copy = lcl_list_new();

  for (i = 0; i < len - 1; i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::pop: internal error reading list");
      lcl_ref_dec(copy);
      return LCL_RC_ERR;
    }

    lcl_list_push(&copy, elem);
    lcl_ref_dec(elem);
  }

  *out = copy;

  return LCL_RC_OK;
}

/* list::slice x start [end] - return sublist */
static int c_list_slice(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  long start;
  long end;
  size_t len;
  size_t i;
  lcl_value *result;
  (void)interp;

  if (!chk_argc(interp, "List::slice", argc, 2, 3)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::slice", "list", argv[0]);
  }

  len = lcl_list_len(argv[0]);

  if (!arg_int(interp, "List::slice", argv[1], &start)) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    if (!arg_int(interp, "List::slice", argv[2], &end)) {
      return LCL_RC_ERR;
    }
  } else {
    end = (long)len;
  }

  if (start < 0) {
    start = (long)len + start;
  }

  if (end < 0) {
    end = (long)len + end;
  }

  if (start < 0) {
    start = 0;
  }

  if (end > (long)len) {
    end = (long)len;
  }

  if (start > end) {
    start = end;
  }

  result = lcl_list_new();

  for (i = (size_t)start; i < (size_t)end; i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::slice: internal error reading list");
      lcl_ref_dec(result);

      return LCL_RC_ERR;
    }

    lcl_list_push(&result, elem);
    lcl_ref_dec(elem);
  }

  *out = result;
  return LCL_RC_OK;
}

/* list::concat a b - return new list with elements from both */
static int c_list_concat(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *result;
  size_t i;
  (void)interp;

  if (!chk_argc(interp, "List::concat", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST || argv[1]->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  for (i = 0; i < lcl_list_len(argv[0]); i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::concat: internal error reading list");
      lcl_ref_dec(result);

      return LCL_RC_ERR;
    }

    lcl_list_push(&result, elem);
    lcl_ref_dec(elem);
  }

  for (i = 0; i < lcl_list_len(argv[1]); i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[1], i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::concat: internal error reading list");
      lcl_ref_dec(result);

      return LCL_RC_ERR;
    }

    lcl_list_push(&result, elem);
    lcl_ref_dec(elem);
  }

  *out = result;

  return LCL_RC_OK;
}

/* list::reverse x - return reversed list */
static int c_list_reverse(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lcl_value *result;
  size_t len;
  size_t i;
  (void)interp;

  if (!chk_argc(interp, "List::reverse", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::reverse", "list", argv[0]);
  }

  len = lcl_list_len(argv[0]);
  result = lcl_list_new();

  for (i = len; i > 0; i--) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i - 1, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::reverse: internal error reading list");
      lcl_ref_dec(result);

      return LCL_RC_ERR;
    }

    lcl_list_push(&result, elem);
    lcl_ref_dec(elem);
  }

  *out = result;

  return LCL_RC_OK;
}

/* ============================================================================
 * Functional List Operations (map, filter, reduce)
 * ============================================================================
 */

/* List::map list f - apply f to each element, return new list */
static int c_list_map(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  int rc;

  if (!chk_argc(interp, "List::map", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::map", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "List::map", "callable", func);
  }

  len = lcl_list_len(list);
  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *mapped = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::map: internal error reading list");
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    call_args[0] = elem;
    rc = lcl_call_proc(interp, func, 1, call_args, &mapped);
    lcl_ref_dec(elem);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(result);
      return rc;
    }

    if (lcl_list_push(&result, mapped) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::map: out of memory");
      lcl_ref_dec(mapped);
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(mapped);
  }

  *out = result;
  return LCL_RC_OK;
}

/* List::filter list f - keep elements where f returns true */
static int c_list_filter(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  int rc;

  if (!chk_argc(interp, "List::filter", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];


  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::filter", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "List::filter", "callable", func);
  }

  len = lcl_list_len(list);
  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::filter: internal error reading list");
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    call_args[0] = elem;
    rc = lcl_call_proc(interp, func, 1, call_args, &pred_result);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(elem);
      lcl_ref_dec(result);
      return rc;
    }

    if (lcl_value_is_true(pred_result)) {
      if (lcl_list_push(&result, elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "List::filter: out of memory");
        lcl_ref_dec(pred_result);
        lcl_ref_dec(elem);
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }
    }

    lcl_ref_dec(pred_result);
    lcl_ref_dec(elem);
  }

  *out = result;
  return LCL_RC_OK;
}

/* List::reduce list init f - fold list with f(acc, elem) */
static int c_list_reduce(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *init;
  lcl_value *func;
  lcl_value *list;
  lcl_value *acc;
  size_t i;
  size_t len;
  int rc;

  if (!chk_argc(interp, "List::reduce", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  init = argv[1];
  func = argv[2];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::reduce", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "List::reduce", "callable", func);
  }

  len = lcl_list_len(list);
  acc = lcl_ref_inc(init);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *new_acc = NULL;
    lcl_value *call_args[2];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::reduce: internal error reading list");
      lcl_ref_dec(acc);
      return LCL_RC_ERR;
    }

    call_args[0] = acc;
    call_args[1] = elem;
    rc = lcl_call_proc(interp, func, 2, call_args, &new_acc);
    lcl_ref_dec(elem);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(acc);
      return rc;
    }

    lcl_ref_dec(acc);
    acc = new_acc;
  }

  *out = acc;
  return LCL_RC_OK;
}

/* ---- List sort machinery ----------------------------------------
 *
 * All three sorts are stable merge sorts over (key, value) pairs.
 * List::sort and List::sort_by order by the shared total ordering
 * below; List::sort_with delegates ordering to a user comparator.
 */

/* Total ordering shared by List::sort and List::sort_by keys:
 * numeric when both operands are numeric (same coercion as the
 * comparison operators, with the integral fast path preserved),
 * bytewise string comparison otherwise. */
static int lcl_order_cmp(lcl_value *a, lcl_value *b) {
  lcl_value *pair[2];
  double fa;
  double fb;
  const char *sa;
  const char *sb;

  pair[0] = a;
  pair[1] = b;

  if (all_args_integral(2, pair)) {
    long ia;
    long ib;

    if (lcl_value_to_int(a, &ia) == LCL_OK &&
        lcl_value_to_int(b, &ib) == LCL_OK) {
      return ia < ib ? -1 : ia > ib ? 1 : 0;
    }
  } else if (lcl_value_to_float(a, &fa) == LCL_OK &&
             lcl_value_to_float(b, &fb) == LCL_OK) {
    return fa < fb ? -1 : fa > fb ? 1 : 0;
  }

  sa = lcl_value_to_string(a);
  sb = lcl_value_to_string(b);

  return strcmp(sa ? sa : "", sb ? sb : "");
}

typedef struct {
  lcl_value *key; /* owned; NULL unless sorting by key function */
  lcl_value *val; /* owned */
} lcl_sort_pair;

/* Writes <0/0/>0 to *ord; returns nonzero to abort the sort (with
 * the interpreter error state already set). */
typedef int (*lcl_sort_cmp_fn)(void *ctx, const lcl_sort_pair *a,
                               const lcl_sort_pair *b, int *ord);

static int cmp_pair_val(void *ctx, const lcl_sort_pair *a,
                        const lcl_sort_pair *b, int *ord) {
  (void)ctx;
  *ord = lcl_order_cmp(a->val, b->val);
  return 0;
}

static int cmp_pair_key(void *ctx, const lcl_sort_pair *a,
                        const lcl_sort_pair *b, int *ord) {
  (void)ctx;
  *ord = lcl_order_cmp(a->key, b->key);
  return 0;
}

typedef struct {
  lcl_interp *interp;
  lcl_value *func;
} lcl_user_cmp_ctx;

static int cmp_pair_user(void *ctx, const lcl_sort_pair *a,
                         const lcl_sort_pair *b, int *ord) {
  lcl_user_cmp_ctx *u = (lcl_user_cmp_ctx *)ctx;
  lcl_value *cmp_args[2];
  lcl_value *res = NULL;
  long v;

  cmp_args[0] = a->val;
  cmp_args[1] = b->val;

  if (lcl_call_proc(u->interp, u->func, 2, cmp_args, &res) != LCL_RC_OK) {
    return -1;
  }

  if (lcl_value_to_int(res, &v) != LCL_OK) {
    lcl_ref_dec(res);
    LCL_ERR_MSG(u->interp,
                "List::sort_with: comparator must return an integer");
    return -1;
  }

  lcl_ref_dec(res);
  *ord = v < 0 ? -1 : v > 0 ? 1 : 0;

  return 0;
}

/* Stable merge sort: ties keep the left run's element first. */
static int merge_sort_pairs(lcl_sort_pair *items, lcl_sort_pair *tmp, size_t lo,
                            size_t hi, lcl_sort_cmp_fn cmp, void *ctx) {
  size_t mid;
  size_t i;
  size_t j;
  size_t k;

  if (hi - lo < 2) {
    return 0;
  }

  mid = lo + (hi - lo) / 2;

  if (merge_sort_pairs(items, tmp, lo, mid, cmp, ctx) != 0 ||
      merge_sort_pairs(items, tmp, mid, hi, cmp, ctx) != 0) {
    return -1;
  }

  for (i = lo; i < hi; i++) {
    tmp[i] = items[i];
  }

  i = lo;
  j = mid;
  k = lo;

  while (i < mid && j < hi) {
    int ord;

    if (cmp(ctx, &tmp[i], &tmp[j], &ord) != 0) {
      return -1;
    }

    if (ord <= 0) {
      items[k++] = tmp[i++];
    } else {
      items[k++] = tmp[j++];
    }
  }

  while (i < mid) {
    items[k++] = tmp[i++];
  }

  while (j < hi) {
    items[k++] = tmp[j++];
  }

  return 0;
}

/* Shared driver: pull elements (and, for sort_by, keys computed
 * exactly once per element) into pairs, sort, rebuild a value list. */
static int list_sort_common(lcl_interp *interp, lcl_value *list,
                            lcl_value *keyfn, lcl_sort_cmp_fn cmp, void *ctx,
                            lcl_value **out) {
  size_t len = lcl_list_len(list);
  size_t i;
  lcl_sort_pair *pairs;
  lcl_sort_pair *tmp;
  int failed = 0;

  if (len == 0) {
    *out = lcl_list_new();
    return LCL_RC_OK;
  }

  pairs = malloc(len * sizeof(*pairs));
  tmp = malloc(len * sizeof(*tmp));

  if (!pairs || !tmp) {
    LCL_ERR_MSG(interp, "out of memory");
    free(pairs);
    free(tmp);
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    pairs[i].key = NULL;
    pairs[i].val = NULL;
  }

  for (i = 0; i < len; i++) {
    if (lcl_list_get(list, i, &pairs[i].val) != LCL_OK) {
      failed = 1;
      break;
    }

    if (keyfn) {
      lcl_value *kargs[1];

      kargs[0] = pairs[i].val;

      if (lcl_call_proc(interp, keyfn, 1, kargs, &pairs[i].key) != LCL_RC_OK) {
        failed = 1;
        break;
      }
    }
  }

  if (!failed && merge_sort_pairs(pairs, tmp, 0, len, cmp, ctx) != 0) {
    failed = 1;
  }

  if (!failed) {
    lcl_value *result = lcl_list_new();

    for (i = 0; i < len; i++) {
      lcl_list_push(&result, pairs[i].val);
    }

    *out = result;
  }

  for (i = 0; i < len; i++) {
    lcl_ref_dec(pairs[i].val);
    lcl_ref_dec(pairs[i].key);
  }

  free(pairs);
  free(tmp);

  return failed ? LCL_RC_ERR : LCL_RC_OK;
}

/* List::sort list - stable sort by the ordinary total ordering */
static int c_list_sort(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  if (!chk_argc(interp, "List::sort", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::sort", "list", argv[0]);
  }

  return list_sort_common(interp, argv[0], NULL, cmp_pair_val, NULL, out);
}

/* List::sort_by list f - stable sort by key function f(elem) -> key.
 * f runs exactly once per element (decorate-sort-undecorate); keys
 * are compared with the ordinary total ordering. */
static int c_list_sort_by(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  if (!chk_argc(interp, "List::sort_by", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    return err_expected_got(interp, "List::sort_by", "callable", argv[1]);
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::sort_by", "list", argv[0]);
  }

  return list_sort_common(interp, argv[0], argv[1], cmp_pair_key, NULL, out);
}

/* List::sort_with list f - stable sort with comparator f(a, b) -> int */
static int c_list_sort_with(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  lcl_user_cmp_ctx ctx;

  if (!chk_argc(interp, "List::sort_with", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    return err_expected_got(interp, "List::sort_with", "callable", argv[0]);
  }

  if (argv[0]->type != LCL_LIST) {
    return err_expected_got(interp, "List::sort_with", "list", argv[1]);
  }

  ctx.interp = interp;
  ctx.func = argv[1];

  return list_sort_common(interp, argv[0], NULL, cmp_pair_user, &ctx, out);
}

/* List::find list pred - find first element where pred returns true */
static int c_list_find(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  int rc;

  if (!chk_argc(interp, "List::find", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::find", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "List::find", "callable", func);
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::find: internal error reading list");
      return LCL_RC_ERR;
    }

    call_args[0] = elem;
    rc = lcl_call_proc(interp, func, 1, call_args, &pred_result);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(elem);
      return rc;
    }

    if (lcl_value_is_true(pred_result)) {
      lcl_ref_dec(pred_result);
      *out = elem;
      return LCL_RC_OK;
    }

    lcl_ref_dec(pred_result);
    lcl_ref_dec(elem);
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* List::any list pred - return 1 if any element satisfies pred */
static int c_list_any(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  int rc;

  if (!chk_argc(interp, "List::any?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::any?", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "List::any?", "callable", func);
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::any?: internal error reading list");
      return LCL_RC_ERR;
    }

    call_args[0] = elem;
    rc = lcl_call_proc(interp, func, 1, call_args, &pred_result);
    lcl_ref_dec(elem);

    if (rc != LCL_RC_OK) {
      return rc;
    }

    if (lcl_value_is_true(pred_result)) {
      lcl_ref_dec(pred_result);
      *out = lcl_int_new(1);
      return LCL_RC_OK;
    }

    lcl_ref_dec(pred_result);
  }

  *out = lcl_int_new(0);

  return LCL_RC_OK;
}

/* List::all list pred - return 1 if all elements satisfy pred */
static int c_list_all(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  int rc;

  if (!chk_argc(interp, "List::all?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::all?", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "List::all?", "callable", func);
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::all?: internal error reading list");
      return LCL_RC_ERR;
    }

    call_args[0] = elem;
    rc = lcl_call_proc(interp, func, 1, call_args, &pred_result);
    lcl_ref_dec(elem);

    if (rc != LCL_RC_OK) {
      return rc;
    }

    if (!lcl_value_is_true(pred_result)) {
      lcl_ref_dec(pred_result);
      *out = lcl_int_new(0);
      return LCL_RC_OK;
    }

    lcl_ref_dec(pred_result);
  }

  *out = lcl_int_new(1);
  return LCL_RC_OK;
}

/* List::unique list - return list with duplicates removed */
static int c_list_unique(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  hash_table *seen;
  size_t i;
  size_t len;

  if (!chk_argc(interp, "List::unique", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::unique", "list", list);
  }

  len = lcl_list_len(list);
  result = lcl_list_new();
  seen = hash_table_new();

  if (!seen) {
    LCL_ERR_MSG(interp, "List::unique: out of memory");
    lcl_ref_dec(result);
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *str;
    lcl_value *dummy;

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::unique: internal error reading list");
      hash_table_free(seen);
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, elem, &str) != LCL_OK) {
      lcl_ref_dec(elem);
      hash_table_free(seen);
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    if (!hash_table_get(seen, str, &dummy)) {
      lcl_value *marker = lcl_int_new(1);
      lcl_list_push(&result, elem);
      hash_table_put(seen, str, marker);
      lcl_ref_dec(marker); /* hash_table_put increments refcount */
    } else {
      lcl_ref_dec(dummy); /* hash_table_get increments refcount */
    }

    lcl_ref_dec(elem);
  }

  hash_table_free(seen);
  *out = result;

  return LCL_RC_OK;
}

struct flatten_frame {
  lcl_value *list; /* owned (+1) */
  size_t idx;
};

static int c_list_flatten(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  struct flatten_frame *stack;
  size_t depth;
  size_t cap;
  long limit = -1;

  if (!chk_argc(interp, "List::flatten", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (list->type != LCL_LIST) {
    return err_expected_got(interp, "List::flatten", "list", list);
  }

  if (argc == 2 && !arg_int(interp, "List::flatten", argv[1], &limit)) {
    return LCL_RC_ERR;
  }

  cap = 8;
  stack = (struct flatten_frame *)malloc(cap * sizeof(*stack));

  if (!stack) {
    LCL_ERR_MSG(interp, "out of memory");
    return LCL_RC_ERR;
  }

  result = lcl_list_new();
  lcl_ref_inc(list);
  stack[0].list = list;
  stack[0].idx = 0;
  depth = 1;

  while (depth > 0) {
    struct flatten_frame *top = &stack[depth - 1];
    lcl_value *elem = NULL;

    if (top->idx >= lcl_list_len(top->list)) {
      lcl_ref_dec(top->list);
      depth--;
      continue;
    }

    if (lcl_list_get(top->list, top->idx, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "List::flatten: internal error reading list");
      goto fail;
    }

    top->idx++;

    if (elem->type == LCL_LIST && (limit < 0 || depth <= (size_t)limit)) {
      if (depth == cap) {
        struct flatten_frame *grown;
        cap *= 2;
        grown = (struct flatten_frame *)realloc(stack, cap * sizeof(*stack));

        if (!grown) {
          LCL_ERR_MSG(interp, "out of memory");
          lcl_ref_dec(elem);
          goto fail;
        }

        stack = grown;
      }

      stack[depth].list = elem;
      stack[depth].idx = 0;
      depth++;
    } else {
      lcl_list_push(&result, elem);
      lcl_ref_dec(elem);
    }
  }

  free(stack);
  *out = result;

  return LCL_RC_OK;

fail:
  while (depth > 0) {
    lcl_ref_dec(stack[depth - 1].list);
    depth--;
  }

  free(stack);
  lcl_ref_dec(result);

  return LCL_RC_ERR;
}

/* ============================================================================
 * Namespaced Dict Operations
 * ============================================================================
 */

/* dict::keys d - return list of keys */
static int c_dict_keys(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!chk_argc(interp, "Dict::keys", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return err_expected_got(interp, "Dict::keys", "dict", argv[0]);
  }

  result = lcl_list_new();
  while (hash_table_iterate(argv[0]->as.dict.dictionary, &it, &key, &val)) {
    lcl_value *key_v = lcl_string_new(key);
    lcl_list_push(&result, key_v);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);
  }

  *out = result;

  return LCL_RC_OK;
}

/* dict::values d - return list of values */
static int c_dict_values(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!chk_argc(interp, "Dict::values", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return err_expected_got(interp, "Dict::values", "dict", argv[0]);
  }

  result = lcl_list_new();

  while (hash_table_iterate(argv[0]->as.dict.dictionary, &it, &key, &val)) {
    lcl_list_push(&result, val);
    lcl_ref_dec(val);
  }

  *out = result;

  return LCL_RC_OK;
}

/* dict::items d - return list of {key value} pairs */
static int c_dict_items(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!chk_argc(interp, "Dict::items", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return err_expected_got(interp, "Dict::items", "dict", argv[0]);
  }

  result = lcl_list_new();

  while (hash_table_iterate(argv[0]->as.dict.dictionary, &it, &key, &val)) {
    lcl_value *pair = lcl_list_new();
    lcl_value *key_v = lcl_string_new(key);
    lcl_list_push(&pair, key_v);
    lcl_list_push(&pair, val);
    lcl_list_push(&result, pair);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);
    lcl_ref_dec(pair);
  }

  *out = result;

  return LCL_RC_OK;
}

/* ============================================================================
 * Namespaced Ns (Namespace introspection) Operations
 * ============================================================================
 */

/* Ns::keys ns - return list of binding names in a namespace */
static int c_ns_keys(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!chk_argc(interp, "Ns::keys", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return err_expected_got(interp, "Ns::keys", "namespace", argv[0]);
  }

  result = lcl_list_new();
  while (hash_table_iterate(argv[0]->as.namespace.namespace, &it, &key, &val)) {
    lcl_value *key_v = lcl_string_new(key);
    lcl_list_push(&result, key_v);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);
  }

  *out = result;
  return LCL_RC_OK;
}

/* Ns::name ns - return the qualified name of a namespace */
static int c_ns_name(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (!chk_argc(interp, "Ns::name", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return err_expected_got(interp, "Ns::name", "namespace", argv[0]);
  }

  *out = lcl_string_new(argv[0]->as.namespace.qname);
  return LCL_RC_OK;
}

/* Ns::set ns name value - bind name to value in namespace
 *
 * Programmatic write into a namespace, without going through the
 * syntactic `namespace foo { ... }` builder. Mutates the namespace's
 * underlying hash table in place, so all references to the namespace
 * value observe the new binding. Returns the bound value. */
static int c_ns_set(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  const char *name;

  if (!chk_argc(interp, "Ns::set", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return err_expected_got(interp, "Ns::set", "namespace", argv[0]);
  }

  if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!hash_table_put(argv[0]->as.namespace.namespace, name, argv[2])) {
    LCL_ERR_MSG(interp, "Ns::set: failed to bind in namespace");
    return LCL_RC_ERR;
  }

  *out = lcl_ref_inc(argv[2]);
  return LCL_RC_OK;
}

/* Ns::has? ns name - check if binding exists in namespace */
static int c_ns_has(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  lcl_value *found = NULL;
  const char *name;

  if (!chk_argc(interp, "Ns::has?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return err_expected_got(interp, "Ns::has?", "namespace", argv[0]);
  }

  if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_ns_get(argv[0], name, &found) == LCL_OK) {
    lcl_ref_dec(found);
    *out = lcl_int_new(1);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

/* Ns::del ns name - remove a binding from a namespace
 *
 * Mutates the namespace's hash table in place, so all references to
 * the namespace value observe the removal. Cells captured elsewhere
 * (closures, imports) keep their own references and stay usable.
 * Returns 1 if the binding was removed, 0 if it was absent. */
static int c_ns_del(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  const char *name;

  if (!chk_argc(interp, "Ns::del", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return err_expected_got(interp, "Ns::del", "namespace", argv[0]);
  }

  if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      hash_table_delete(argv[0]->as.namespace.namespace, name) ? 1 : 0);

  return LCL_RC_OK;
}

/* Ns::clear ns - remove all bindings from a namespace
 *
 * Empties the namespace's hash table in place; the value's identity
 * is preserved, so all live references see the emptied namespace
 * immediately. Cells captured elsewhere keep their own references. */
static int c_ns_clear(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  if (!chk_argc(interp, "Ns::clear", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return err_expected_got(interp, "Ns::clear", "namespace", argv[0]);
  }

  hash_table_clear(argv[0]->as.namespace.namespace);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* dict (constructor) - create dict from key-value pairs */
static int c_dict_create_proc(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lcl_value *dict;
  int i;

  if (argc % 2 != 0) {
    char msg[96];

    snprintf(msg, sizeof(msg),
             "dict: expected an even number of arguments, got %d", argc);
    LCL_ERR_MSG_DUP(interp, msg);
    return LCL_RC_ERR;
  }

  dict = lcl_dict_new();
  if (!dict) {
    LCL_ERR_MSG(interp, "dict: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc; i += 2) {
    const char *key;

    if (lcl_value_to_cstring(interp, argv[i], &key) != LCL_OK) {
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    if (lcl_dict_put(&dict, key, argv[i + 1]) != LCL_OK) {
      LCL_ERR_MSG(interp, "dict: out of memory");
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }
  }

  *out = dict;

  return LCL_RC_OK;
}

/* dict::merge a b - return new dict with entries from both (b overwrites a) */
static int c_dict_merge(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!chk_argc(interp, "Dict::merge", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT || argv[1]->type != LCL_DICT) {
    return LCL_RC_ERR;
  }

  result = lcl_ref_inc(argv[0]);

  while (hash_table_iterate(argv[1]->as.dict.dictionary, &it, &key, &val)) {
    lcl_dict_put(&result, key, val);
    lcl_ref_dec(val);
  }

  *out = result;

  return LCL_RC_OK;
}

/* Dict::map d f - apply f to each key-value pair, f receives key and value,
 * returns new value */
static int c_dict_map(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *dict;
  lcl_value *result;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  int rc;

  if (!chk_argc(interp, "Dict::map", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  dict = argv[0];
  func = argv[1];

  if (dict->type != LCL_DICT) {
    return err_expected_got(interp, "Dict::map", "dict", dict);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "Dict::map", "callable", func);
  }

  result = lcl_dict_new();

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &key, &val)) {
    lcl_value *mapped = NULL;
    lcl_value *key_v = lcl_string_new(key);
    lcl_value *call_args[2];

    call_args[0] = key_v;
    call_args[1] = val;
    rc = lcl_call_proc(interp, func, 2, call_args, &mapped);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(result);
      return rc;
    }

    lcl_dict_put(&result, key, mapped);
    lcl_ref_dec(mapped);
  }

  *out = result;
  return LCL_RC_OK;
}

/* Dict::filter d f - keep entries where f(key, value) returns true */
static int c_dict_filter(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *func;
  lcl_value *dict;
  lcl_value *result;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  int rc;

  if (!chk_argc(interp, "Dict::filter", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  dict = argv[0];
  func = argv[1];

  if (dict->type != LCL_DICT) {
    return err_expected_got(interp, "Dict::filter", "dict", dict);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "Dict::filter", "callable", func);
  }

  result = lcl_dict_new();

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &key, &val)) {
    lcl_value *pred_result = NULL;
    lcl_value *key_v = lcl_string_new(key);
    lcl_value *call_args[2];

    call_args[0] = key_v;
    call_args[1] = val;
    rc = lcl_call_proc(interp, func, 2, call_args, &pred_result);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(key_v);
      lcl_ref_dec(val);
      lcl_ref_dec(result);
      return rc;
    }

    if (lcl_value_is_true(pred_result)) {
      lcl_dict_put(&result, key, val);
    }

    lcl_ref_dec(pred_result);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);
  }

  *out = result;
  return LCL_RC_OK;
}

/* Dict::reduce d init f - fold dict with f(acc, key, value) */
static int c_dict_reduce(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *init;
  lcl_value *func;
  lcl_value *dict;
  lcl_value *acc;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  int rc;

  if (!chk_argc(interp, "Dict::reduce", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  dict = argv[0];
  init = argv[1];
  func = argv[2];

  if (dict->type != LCL_DICT) {
    return err_expected_got(interp, "Dict::reduce", "dict", dict);
  }

  if (!lcl_is_callable(func)) {
    return err_expected_got(interp, "Dict::reduce", "callable", func);
  }

  acc = lcl_ref_inc(init);

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &key, &val)) {
    lcl_value *new_acc = NULL;
    lcl_value *key_v = lcl_string_new(key);
    lcl_value *call_args[3];

    call_args[0] = acc;
    call_args[1] = key_v;
    call_args[2] = val;
    rc = lcl_call_proc(interp, func, 3, call_args, &new_acc);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(acc);
      return rc;
    }

    lcl_ref_dec(acc);
    acc = new_acc;
  }

  *out = acc;
  return LCL_RC_OK;
}

/* ============================================================================
 * Namespaced String Operations
 * ============================================================================
 */

/* string::upper s - return uppercase string */
static int c_string_upper(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  const char *src;
  char *result;
  size_t i;
  size_t len;

  if (!chk_argc(interp, "String::upper", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::upper", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);
  result = malloc(len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::upper: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    char c = src[i];

    if (c >= 'a' && c <= 'z') {
      result[i] = (char)(c - 32);
    } else {
      result[i] = c;
    }
  }

  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* string::lower s - return lowercase string */
static int c_string_lower(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  const char *src;
  char *result;
  size_t i;
  size_t len;

  if (!chk_argc(interp, "String::lower", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::lower", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);
  result = malloc(len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::lower: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    char c = src[i];

    if (c >= 'A' && c <= 'Z') {
      result[i] = (char)(c + 32);
    } else {
      result[i] = c;
    }
  }

  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* string::find s sub - return index of first occurrence or -1 */
static int c_string_find(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  const char *haystack;
  const char *needle;
  const char *found;

  if (!chk_argc(interp, "String::find", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::find", argv[0], &haystack)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::find", argv[1], &needle)) {
    return LCL_RC_ERR;
  }

  found = strstr(haystack, needle);

  if (found) {
    *out = lcl_int_new((long)(found - haystack));
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* string::replace s old new - return string with replacements */
static int c_string_replace(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  const char *src;
  const char *old_str;
  const char *new_str;
  const char *p;
  const char *found;
  size_t old_len;
  size_t new_len;
  size_t result_len;
  char *result;
  char *dst;
  int count = 0;

  if (!chk_argc(interp, "String::replace", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::replace", argv[0], &src)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::replace", argv[1], &old_str)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::replace", argv[2], &new_str)) {
    return LCL_RC_ERR;
  }

  old_len = strlen(old_str);
  new_len = strlen(new_str);

  if (old_len == 0) {
    *out = lcl_ref_inc(argv[0]);
    return LCL_RC_OK;
  }

  /* Count occurrences */
  p = src;

  while ((found = strstr(p, old_str)) != NULL) {
    count++;
    p = found + old_len;
  }

  if (count == 0) {
    *out = lcl_ref_inc(argv[0]);

    return LCL_RC_OK;
  }

  /* Bugfix: compute the result length with overflow detection. The
   * original form `strlen(src) + count*(new_len-old_len)` had two
   * problems: signed-style `new_len - old_len` underflows as `size_t`
   * when shrinking (saved only by lucky modular cancellation), and
   * `count*new_len` can genuinely overflow on adversarial inputs,
   * producing an undersized buffer for the rewrite loop. */
  {
    size_t src_len = strlen(src);
    size_t total_new;
    size_t total_old = (size_t)count * old_len; /* <= src_len by construction */

    if (new_len > 0 && (size_t)count > (size_t)-1 / new_len) {
      LCL_ERR_MSG(interp, "String::replace: result too large");
      return LCL_RC_ERR;
    }

    total_new = (size_t)count * new_len;

    if (total_new > (size_t)-1 - (src_len - total_old)) {
      LCL_ERR_MSG(interp, "String::replace: result too large");
      return LCL_RC_ERR;
    }

    result_len = (src_len - total_old) + total_new;

    if (result_len == (size_t)-1) {
      /* malloc(result_len + 1) below would overflow */
      LCL_ERR_MSG(interp, "String::replace: result too large");
      return LCL_RC_ERR;
    }
  }

  result = malloc(result_len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::replace: out of memory");
    return LCL_RC_ERR;
  }

  dst = result;
  p = src;

  while ((found = strstr(p, old_str)) != NULL) {
    size_t prefix_len = (size_t)(found - p);
    memcpy(dst, p, prefix_len);
    dst += prefix_len;
    memcpy(dst, new_str, new_len);
    dst += new_len;
    p = found + old_len;
  }

  strcpy(dst, p);

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* String::length s - return length of string */
static int c_string_length(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  const char *src;

  if (!chk_argc(interp, "String::length", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::length", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  *out = lcl_int_new((long)strlen(src));

  return LCL_RC_OK;
}

/* String::index s i - return character at index i as a string */
static int c_string_index(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  const char *src;
  long idx;
  size_t len;
  char buf[2];

  if (!chk_argc(interp, "String::index", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::index", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);

  if (!arg_int(interp, "String::index", argv[1], &idx)) {
    return LCL_RC_ERR;
  }

  if (idx < 0) {
    idx = (long)len + idx;
  }

  if (idx < 0 || (size_t)idx >= len) {
    LCL_ERR_MSG(interp, "string index out of range");
    return LCL_RC_ERR;
  }

  buf[0] = src[idx];
  buf[1] = '\0';
  *out = lcl_string_new(buf);

  return LCL_RC_OK;
}

/* String::range s start end - return substring from start to end (exclusive) */
static int c_string_range(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  const char *src;
  long start;
  long end;
  size_t len;
  size_t sub_len;
  char *result;

  if (!chk_argc(interp, "String::range", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (!arg_str(interp, "String::range", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);

  if (!arg_int(interp, "String::range", argv[1], &start) ||
      !arg_int(interp, "String::range", argv[2], &end)) {
    return LCL_RC_ERR;
  }

  if (start < 0) {
    start = (long)len + start;
  }

  if (end < 0) {
    end = (long)len + end;
  }

  if (start < 0) {
    start = 0;
  }

  if (end < 0) {
    end = 0;
  }

  if ((size_t)start > len) {
    start = (long)len;
  }

  if ((size_t)end > len) {
    end = (long)len;
  }

  if (start >= end) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  sub_len = (size_t)(end - start);
  result = malloc(sub_len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::range: out of memory");
    return LCL_RC_ERR;
  }

  memcpy(result, src + start, sub_len);
  result[sub_len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* String::trim s - remove leading and trailing whitespace */
static int c_string_trim(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  const char *src;
  const char *start;
  const char *end;
  size_t len;
  char *result;

  if (!chk_argc(interp, "String::trim", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &src) != LCL_OK) {
    return LCL_RC_ERR;
  }
  start = src;

  while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
    start++;
  }

  if (*start == '\0') {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  end = start + strlen(start) - 1;

  while (end > start &&
         (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
    end--;
  }

  len = (size_t)(end - start + 1);
  result = malloc(len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::trim: out of memory");
    return LCL_RC_ERR;
  }

  memcpy(result, start, len);
  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* ============================================================================
 * Namespaced Lex (reader reflection) Operations
 * ============================================================================
 */

/* Build the record for one scanned word.  A word is `dynamic` when
 * evaluation would compute part of it: a $var or [subcommand] piece,
 * including the () and #{} literals, which compile to subcommands.
 * For non-dynamic words `text` is the exact value evaluation would
 * produce -- the concatenation of the scanner's literal pieces, with
 * escapes already decoded and quotes/braces already stripped.
 * Dynamic words get `text` "" plus a `pieces` list describing the
 * structure, so callers can explain WHY a word is dynamic without
 * re-lexing.  Every record carries `span`, the word's half-open
 * (start end) byte range in the scanned text, so callers can slice
 * the original bytes -- quoting, escapes, and @ intact -- instead of
 * re-quoting decoded text.  Returns NULL on out-of-memory. */
static lcl_value *lex_word_record(const lcl_word *w) {
  lcl_value *rec;
  lcl_value *v;
  int i;
  int dynamic = 0;
  size_t text_len = 0;

  for (i = 0; i < w->np; i++) {
    if (w->wp[i].kind == LCL_WP_LIT) {
      text_len += w->wp[i].as.lit.n;
    } else {
      dynamic = 1;
    }
  }

  rec = lcl_dict_new();

  if (!rec) {
    return NULL;
  }

  if (dynamic) {
    v = lcl_string_new("");
  } else {
    char *buf = (char *)malloc(text_len + 1);
    size_t off = 0;

    if (!buf) {
      lcl_ref_dec(rec);
      return NULL;
    }

    for (i = 0; i < w->np; i++) {
      memcpy(buf + off, w->wp[i].as.lit.s, w->wp[i].as.lit.n);
      off += w->wp[i].as.lit.n;
    }

    buf[text_len] = '\0';
    v = lcl_string_new(buf);
    free(buf);
  }

  if (!v) {
    lcl_ref_dec(rec);
    return NULL;
  }

  lcl_dict_put(&rec, "text", v);
  lcl_ref_dec(v);

  v = lcl_int_new(dynamic);
  lcl_dict_put(&rec, "dynamic", v);
  lcl_ref_dec(v);

  v = lcl_int_new(w->quoted ? 1 : 0);
  lcl_dict_put(&rec, "quoted", v);
  lcl_ref_dec(v);

  v = lcl_int_new(w->braced ? 1 : 0);
  lcl_dict_put(&rec, "braced", v);
  lcl_ref_dec(v);

  v = lcl_int_new(w->expand ? 1 : 0);
  lcl_dict_put(&rec, "expand", v);
  lcl_ref_dec(v);

  {
    lcl_value *span = lcl_list_new();

    if (!span) {
      lcl_ref_dec(rec);
      return NULL;
    }

    v = lcl_int_new(w->src_start);
    lcl_list_push(&span, v);
    lcl_ref_dec(v);

    v = lcl_int_new(w->src_end);
    lcl_list_push(&span, v);
    lcl_ref_dec(v);

    lcl_dict_put(&rec, "span", span);
    lcl_ref_dec(span);
  }

  if (dynamic) {
    lcl_value *pieces = lcl_list_new();

    if (!pieces) {
      lcl_ref_dec(rec);
      return NULL;
    }

    for (i = 0; i < w->np; i++) {
      lcl_value *pd = lcl_dict_new();

      if (!pd) {
        lcl_ref_dec(pieces);
        lcl_ref_dec(rec);
        return NULL;
      }

      switch (w->wp[i].kind) {
      case LCL_WP_LIT:
        v = lcl_string_new("lit");
        lcl_dict_put(&pd, "kind", v);
        lcl_ref_dec(v);
        v = lcl_string_new(w->wp[i].as.lit.s);
        lcl_dict_put(&pd, "text", v);
        lcl_ref_dec(v);
        break;

      case LCL_WP_VAR:
        v = lcl_string_new("var");
        lcl_dict_put(&pd, "kind", v);
        lcl_ref_dec(v);
        v = lcl_string_new(w->wp[i].as.var.name);
        lcl_dict_put(&pd, "name", v);
        lcl_ref_dec(v);
        break;

      case LCL_WP_SUBCMD:
        v = lcl_string_new("sub");
        lcl_dict_put(&pd, "kind", v);
        lcl_ref_dec(v);
        break;
      }

      lcl_list_push(&pieces, pd);
      lcl_ref_dec(pd);
    }

    lcl_dict_put(&rec, "pieces", pieces);
    lcl_ref_dec(pieces);
  }

  return rec;
}

/* Lex::commands text - read `text` with the language's own reader,
 * WITHOUT evaluating any of it, and return the lexical structure: a
 * list of command records
 *   #{line N words (<word record> ...)}
 * one per statement.  This exists so embedders (weft's command
 * resolver) can decide what a piece of interactive text IS -- lcl
 * call, external command, shell handoff -- from the real grammar
 * instead of a lookalike parser.  Malformed input errors with the
 * compiler's message and line; comment-only or empty text is an
 * empty list. */
static int c_lex_commands(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  const char *src;
  lcl_program *prog;
  lcl_compile_err cerr;
  lcl_value *result;
  int ci;
  int wi;

  if (!chk_argc(interp, "Lex::commands", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &src) != LCL_OK) {
    return LCL_RC_ERR;
  }

  cerr.msg = NULL;
  cerr.line = 0;
  prog = lcl_program_compile_ex(src, NULL, &cerr);

  if (!prog) {
    char msg[192];

    snprintf(msg, sizeof(msg), "Lex::commands: %s (line %ld)",
             cerr.msg ? cerr.msg : "parse error", cerr.line);
    LCL_ERR_MSG_DUP(interp, msg);
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  if (!result) {
    lcl_program_free(prog);
    LCL_ERR_MSG(interp, "Lex::commands: out of memory");
    return LCL_RC_ERR;
  }

  for (ci = 0; ci < prog->ncmd; ci++) {
    const lcl_command *cmd = &prog->cmd[ci];
    lcl_value *cmd_rec = lcl_dict_new();
    lcl_value *words;
    lcl_value *v;

    if (!cmd_rec) {
      goto oom;
    }

    v = lcl_int_new(cmd->line);
    lcl_dict_put(&cmd_rec, "line", v);
    lcl_ref_dec(v);

    words = lcl_list_new();

    if (!words) {
      lcl_ref_dec(cmd_rec);
      goto oom;
    }

    for (wi = 0; wi < cmd->argc; wi++) {
      lcl_value *wrec = lex_word_record(&cmd->w[wi]);

      if (!wrec) {
        lcl_ref_dec(words);
        lcl_ref_dec(cmd_rec);
        goto oom;
      }

      lcl_list_push(&words, wrec);
      lcl_ref_dec(wrec);
    }

    lcl_dict_put(&cmd_rec, "words", words);
    lcl_ref_dec(words);
    lcl_list_push(&result, cmd_rec);
    lcl_ref_dec(cmd_rec);
  }

  lcl_program_free(prog);
  *out = result;

  return LCL_RC_OK;

oom:
  lcl_ref_dec(result);
  lcl_program_free(prog);
  LCL_ERR_MSG(interp, "Lex::commands: out of memory");
  return LCL_RC_ERR;
}

void lcl_register_core(lcl_interp *interp) {
  lcl_value *list_ns;
  lcl_value *dict_ns;
  lcl_value *string_ns;
  lcl_value *ns_ns;
  lcl_value *lex_ns;

  lcl_register_proc(interp, "assert", c_assert);
  lcl_register_proc(interp, "assert_eq", c_assert_eq);
  lcl_register_proc(interp, "assert_neq", c_assert_neq);

  lcl_register_proc(interp, "puts", c_puts);

  lcl_register_spec(interp, "and", s_and);
  lcl_register_spec(interp, "or", s_or);
  lcl_register_proc(interp, "not", c_not);

  lcl_register_proc(interp, "+", c_add);
  lcl_register_proc(interp, "-", c_sub);
  lcl_register_proc(interp, "*", c_mult);
  lcl_register_proc(interp, "/", c_div);
  lcl_register_proc(interp, "%", c_mod);
  lcl_register_proc(interp, "<", c_lt);
  lcl_register_proc(interp, "<=", c_lte);
  lcl_register_proc(interp, ">", c_gt);
  lcl_register_proc(interp, ">=", c_gte);

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

  lcl_register_proc(interp, "list?", c_is_list);
  lcl_register_proc(interp, "dict?", c_is_dict);
  lcl_register_proc(interp, "string?", c_is_string);
  lcl_register_proc(interp, "opaque?", c_is_opaque);
  lcl_register_proc(interp, "number?", c_is_number);
  lcl_register_proc(interp, "int?", c_is_int);
  lcl_register_proc(interp, "float?", c_is_float);
  lcl_register_proc(interp, "cell?", c_is_cell);
  lcl_register_proc(interp, "proc?", c_is_proc);

  lcl_register_proc(interp, "arity", c_arity);

  lcl_register_proc(interp, "int", c_to_int);
  lcl_register_proc(interp, "float", c_to_float);
  lcl_register_proc(interp, "type", c_type);
  lcl_register_proc(interp, "repr", c_repr);

  lcl_register_proc(interp, "let", c_let);
  lcl_register_proc(interp, "ref", c_ref);
  lcl_register_proc(interp, "gensym", c_gensym);
  lcl_register_proc(interp, "getvar", c_get);
  lcl_register_spec(interp, "var", s_var);
  lcl_register_spec(interp, "set!", s_set_bang);
  lcl_register_spec(interp, "binding-cell", s_binding_cell);
  lcl_register_spec(interp, "same-binding?", s_same_binding);

  lcl_register_spec(interp, "return", s_return);
  lcl_register_spec(interp, "lambda", s_lambda);
  lcl_register_spec(interp, "proc", s_proc);
  lcl_register_spec(interp, "macro", s_macro);
  lcl_register_spec(interp, "macroexpand", s_macroexpand);
  lcl_register_spec(interp, "eval", s_eval);
  lcl_register_proc(interp, "apply", c_apply);
  lcl_register_spec(interp, "load", s_load);
  lcl_register_spec(interp, "require", s_require);
  lcl_register_spec(interp, "subst", s_subst);
  lcl_register_spec(interp, "quasiquote", s_quasiquote);
  lcl_register_spec(interp, "namespace", s_namespace);
  lcl_register_spec(interp, "isolate", s_isolate);
  lcl_register_spec(interp, "import", s_import);
  lcl_register_spec(interp, "->", s_thread_first);
  lcl_register_spec(interp, "->>", s_thread_last);

  lcl_register_spec(interp, "if", s_if);
  lcl_register_spec(interp, "cond", s_cond);
  lcl_register_spec(interp, "case", s_case);
  lcl_register_spec(interp, "while", s_while);
  lcl_register_spec(interp, "for", s_for);
  lcl_register_spec(interp, "foreach", s_foreach);
  lcl_register_spec(interp, "break", s_break);
  lcl_register_spec(interp, "continue", s_continue);

  lcl_register_proc(interp, "error", c_error);
  lcl_register_spec(interp, "catch", c_catch);

  lcl_register_proc(interp, "list", c_list);
  lcl_register_proc(interp, "dict", c_dict_create_proc);

  list_ns = lcl_ns_new("List");
  lcl_define_take(interp, "List", list_ns);

  lcl_ns_def(list_ns, "new", lcl_c_proc_new("List::new", c_list));
  lcl_ns_def(list_ns, "push", lcl_c_proc_new("List::push", c_list_push));
  lcl_ns_def(list_ns, "pop", lcl_c_proc_new("List::pop", c_list_pop));
  lcl_ns_def(list_ns, "slice", lcl_c_proc_new("List::slice", c_list_slice));
  lcl_ns_def(list_ns, "concat", lcl_c_proc_new("List::concat", c_list_concat));
  lcl_ns_def(list_ns, "reverse",
             lcl_c_proc_new("List::reverse", c_list_reverse));
  lcl_ns_def(list_ns, "index", lcl_c_proc_new("List::index", c_lindex));
  lcl_ns_def(list_ns, "range", lcl_c_proc_new("List::range", c_lrange));
  lcl_ns_def(list_ns, "map", lcl_c_proc_new("List::map", c_list_map));
  lcl_ns_def(list_ns, "filter", lcl_c_proc_new("List::filter", c_list_filter));
  lcl_ns_def(list_ns, "reduce", lcl_c_proc_new("List::reduce", c_list_reduce));
  lcl_ns_def(list_ns, "sort", lcl_c_proc_new("List::sort", c_list_sort));
  lcl_ns_def(list_ns, "sort_by",
             lcl_c_proc_new("List::sort_by", c_list_sort_by));
  lcl_ns_def(list_ns, "sort_with",
             lcl_c_proc_new("List::sort_with", c_list_sort_with));
  lcl_ns_def(list_ns, "find", lcl_c_proc_new("List::find", c_list_find));
  lcl_ns_def(list_ns, "any?", lcl_c_proc_new("List::any?", c_list_any));
  lcl_ns_def(list_ns, "all?", lcl_c_proc_new("List::all?", c_list_all));
  lcl_ns_def(list_ns, "unique", lcl_c_proc_new("List::unique", c_list_unique));
  lcl_ns_def(list_ns, "flatten",
             lcl_c_proc_new("List::flatten", c_list_flatten));

  dict_ns = lcl_ns_new("Dict");
  lcl_define_take(interp, "Dict", dict_ns);

  lcl_ns_def(dict_ns, "new", lcl_c_proc_new("Dict::new", c_dict_create_proc));
  lcl_ns_def(dict_ns, "keys", lcl_c_proc_new("Dict::keys", c_dict_keys));
  lcl_ns_def(dict_ns, "values", lcl_c_proc_new("Dict::values", c_dict_values));
  lcl_ns_def(dict_ns, "items", lcl_c_proc_new("Dict::items", c_dict_items));
  lcl_ns_def(dict_ns, "merge", lcl_c_proc_new("Dict::merge", c_dict_merge));
  lcl_ns_def(dict_ns, "map", lcl_c_proc_new("Dict::map", c_dict_map));
  lcl_ns_def(dict_ns, "filter", lcl_c_proc_new("Dict::filter", c_dict_filter));
  lcl_ns_def(dict_ns, "reduce", lcl_c_proc_new("Dict::reduce", c_dict_reduce));

  string_ns = lcl_ns_new("String");
  lcl_define_take(interp, "String", string_ns);

  lcl_ns_def(string_ns, "from", lcl_c_proc_new("String::from", c_string_from));
  lcl_ns_def(string_ns, "upper",
             lcl_c_proc_new("String::upper", c_string_upper));
  lcl_ns_def(string_ns, "lower",
             lcl_c_proc_new("String::lower", c_string_lower));
  lcl_ns_def(string_ns, "find", lcl_c_proc_new("String::find", c_string_find));
  lcl_ns_def(string_ns, "replace",
             lcl_c_proc_new("String::replace", c_string_replace));
  lcl_ns_def(string_ns, "split", lcl_c_proc_new("String::split", c_split));
  lcl_ns_def(string_ns, "join", lcl_c_proc_new("String::join", c_join));
  lcl_ns_def(string_ns, "length",
             lcl_c_proc_new("String::length", c_string_length));
  lcl_ns_def(string_ns, "index",
             lcl_c_proc_new("String::index", c_string_index));
  lcl_ns_def(string_ns, "range",
             lcl_c_proc_new("String::range", c_string_range));
  lcl_ns_def(string_ns, "trim", lcl_c_proc_new("String::trim", c_string_trim));

  ns_ns = lcl_ns_new("Ns");
  lcl_define_take(interp, "Ns", ns_ns);

  lcl_ns_def(ns_ns, "keys", lcl_c_proc_new("Ns::keys", c_ns_keys));
  lcl_ns_def(ns_ns, "name", lcl_c_proc_new("Ns::name", c_ns_name));
  lcl_ns_def(ns_ns, "has?", lcl_c_proc_new("Ns::has?", c_ns_has));
  lcl_ns_def(ns_ns, "set", lcl_c_proc_new("Ns::set", c_ns_set));
  lcl_ns_def(ns_ns, "del", lcl_c_proc_new("Ns::del", c_ns_del));
  lcl_ns_def(ns_ns, "clear", lcl_c_proc_new("Ns::clear", c_ns_clear));

  lex_ns = lcl_ns_new("Lex");
  lcl_define_take(interp, "Lex", lex_ns);

  lcl_ns_def(lex_ns, "commands",
             lcl_c_proc_new("Lex::commands", c_lex_commands));
}
