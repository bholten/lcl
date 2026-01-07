#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"

#include "lcl-stdlib.h"

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

static int c_assert(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  const char *expr;
  lcl_value *result = NULL;
  (void)out;

  if (argc < 1) {
    LCL_ERR_MSG(interp, "assert requires an expression");
    return LCL_RC_ERR;
  }

  expr = lcl_value_to_string(argv[0]);

  if (lcl_eval_string(interp, expr, &result) != LCL_OK) {
    if (result) {
      lcl_ref_dec(result);
    }
    return LCL_RC_ERR;
  }

  if (!lcl_value_is_true(result)) {
    lcl_ref_dec(result);
    if (argc >= 2) {
      LCL_ERR_MSG_DUP(interp, lcl_value_to_string(argv[1]));
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
  (void)interp;

  for (i = 0; i < argc; i++) {
    const char *str = lcl_value_to_string(argv[i]);
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

static int c_and(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  int b;
  int i;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc; i++) {
    b = lcl_value_is_true(argv[i]);

    if (!b) {
      goto ret;
    }
  }

ret:
  *out = lcl_int_new(b);
  return LCL_RC_OK;
}

static int c_or(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  int b;
  int i;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  b = lcl_value_is_true(argv[0]);

  for (i = 1; i < argc; i++) {
    b = b || lcl_value_is_true(argv[i]);
  }

  *out = lcl_int_new(b);
  return LCL_RC_OK;
}

static int c_not(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  int b;
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  b = !lcl_value_is_true(argv[0]);

  *out = lcl_int_new(b);
  return LCL_RC_OK;
}

static int c_add(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  double sum = 0.0f;
  int i;
  double v;
  (void)interp;

  for (i = 0; i < argc; i++) {
    if (lcl_value_to_float(argv[i], &v) != LCL_OK) {
      return LCL_RC_ERR;
    }

    sum += v;
  }

  *out = lcl_float_new(sum);

  return LCL_RC_OK;
}

static int c_sub(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  double result;
  int i;
  double v;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &result) != LCL_OK) {
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc; i++) {
    if (lcl_value_to_float(argv[i], &v) != LCL_OK) {
      return LCL_RC_ERR;
    }

    result -= v;
  }

  *out = lcl_float_new(result);

  return LCL_RC_OK;
}

static int c_mult(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  double product = 1.0f;
  int i;
  double v;
  (void)interp;

  for (i = 0; i < argc; i++) {
    if (lcl_value_to_float(argv[i], &v) != LCL_OK) {
      return LCL_RC_ERR;
    }

    product *= v;
  }

  *out = lcl_float_new(product);

  return LCL_RC_OK;
}

static int c_div(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;
  double result;
  double numerator;
  double divisor;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &numerator) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &divisor) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (divisor == 0.0f) {
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
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &dividend) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &divisor) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = dividend % divisor;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
}

static int c_lt(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  long result;
  double left;
  double right;
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &left) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &right) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = left < right;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
}

static int c_lte(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  long result;
  double left;
  double right;
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &left) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &right) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = left <= right;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
}

static int c_gt(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  long result;
  double left;
  double right;
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &left) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &right) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = left > right;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
}

static int c_gte(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  long result;
  double left;
  double right;
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &left) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &right) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = left >= right;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
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

/* check if a value can be interpreted as a number and get its double value */
static int value_to_double(lcl_value *v, double *out) {
  if (v->type == LCL_INT) {
    *out = (double)v->as.i;
    return 1;
  }

  if (v->type == LCL_FLOAT) {
    *out = (double)v->as.f;
    return 1;
  }

  if (v->type == LCL_STRING) {
    const char *s = lcl_value_to_string(v);
    char *endptr;
    double d;

    if (!s || *s == '\0') {
      return 0;
    }

    d = strtod(s, &endptr);

    if (*endptr == '\0') {
      *out = d;
      return 1;
    }
  }

  return 0;
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
  case LCL_STRING:
    return strcmp(lcl_value_to_string(a), lcl_value_to_string(b)) == 0;

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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lcl_value_equal_deep(argv[0], argv[1], &guard) ? 1 : 0);
  return LCL_RC_OK;
}

static int c_ne(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lcl_value_equal_deep(argv[0], argv[1], &guard) ? 0 : 1);
  return LCL_RC_OK;
}

/* same? : identity equality (no deref) */
static int c_same(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0] == argv[1] ? 1 : 0);
  return LCL_RC_OK;
}

/* not-same? : identity inequality */
static int c_not_same(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0] != argv[1] ? 1 : 0);
  return LCL_RC_OK;
}

/* cell? : check if value is a cell */
static int c_is_cell(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (argc != 1) {
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

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  name = lcl_value_to_string(name_v);

  if (lcl_env_get_value(&interp->env, name, &binding) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);

  if (binding->type != LCL_CELL) {
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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &name1_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }
  if (lcl_eval_word(interp, args[1], &name2_v) != LCL_RC_OK) {
    lcl_ref_dec(name1_v);
    return LCL_RC_ERR;
  }

  name1 = lcl_value_to_string(name1_v);
  name2 = lcl_value_to_string(name2_v);

  if (lcl_env_get_value(&interp->env, name1, &binding1) != LCL_OK) {
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(&interp->env, name2, &binding2) != LCL_OK) {
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    lcl_ref_dec(binding1);
    return LCL_RC_ERR;
  }

  if (binding1->type != LCL_CELL || binding2->type != LCL_CELL) {
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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  name = lcl_value_to_string(argv[0]);

  if (strstr(name, "::") && interp->def_depth == 0) {
    LCL_ERR_MSG(interp, "qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::def'");
    return LCL_RC_ERR;
  }

  if (interp->def_depth > 0) {
    if (lcl_def_target_bind(interp, name, argv[1]) != LCL_OK) {
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name, argv[1]) != LCL_OK) {
      return LCL_RC_ERR;
    }
  }

  *out = lcl_ref_inc(argv[1]);

  return LCL_RC_OK;
}

static int c_ref(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_cell_new(argv[0]);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_get(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  lcl_value *val = NULL;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(&interp->env, lcl_value_to_string(argv[0]), &val) !=
      LCL_OK) {
    return LCL_RC_ERR;
  }

  if (val->type == LCL_CELL) {
    if (lcl_cell_get(val, out) != LCL_OK) {
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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &val_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  name_str = lcl_value_to_string(name_v);

  /* Look up the cell to check for cycles before mutating.
   * If the value is a proc that captures this cell, assigning it
   * would create a reference cycle that can never be freed. */
  if (lcl_env_get_value(&interp->env, name_str, &cell) == LCL_OK) {
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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, argv[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  name_str = lcl_value_to_string(name_v);

  if (strstr(name_str, "::") && interp->def_depth == 0) {
    LCL_ERR_MSG(interp, "qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::def'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, argv[1], &init_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (interp->def_depth > 0) {
    if (lcl_def_target_var(interp, name_str, init_v) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(init_v);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_var(&interp->env, name_str, init_v) != LCL_OK) {
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

  if (argc != 0) {
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

  if (argc != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_CONTINUE;
}

/* error - throw an error with the given message */
int c_error(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  const char *msg;

  (void)out;

  if (argc < 1) {
    LCL_ERR_MSG(interp, "error: requires message argument");
    return LCL_RC_ERR;
  }

  msg = lcl_value_to_string(argv[0]);
  LCL_ERR_MSG_DUP(interp, msg);

  return LCL_RC_ERR;
}

/* catch - execute script and catch errors
 * Usage: catch script ?resultVar? ?errorVar?
 * Returns: 0 if script succeeded, 1 if error occurred
 */
static int c_catch(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  lcl_value *script_v = NULL;
  lcl_program *prog = NULL;
  lcl_value *result = NULL;
  char *result_var = NULL;
  char *error_var = NULL;
  int rc;

  if (argc < 1 || argc > 3) {
    LCL_ERR_MSG(interp, "catch: usage: catch script ?resultVar? ?errorVar?");
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    lcl_value *rv = NULL;
    if (lcl_eval_word_to_str(interp, args[1], &rv) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }
    result_var = strdup(lcl_value_to_string(rv));
    lcl_ref_dec(rv);
  }

  if (argc >= 3) {
    lcl_value *ev = NULL;
    if (lcl_eval_word_to_str(interp, args[2], &ev) != LCL_RC_OK) {
      free(result_var);
      return LCL_RC_ERR;
    }
    error_var = strdup(lcl_value_to_string(ev));
    lcl_ref_dec(ev);
  }

  if (lcl_eval_word_to_str(interp, args[0], &script_v) != LCL_RC_OK) {
    free(result_var);
    free(error_var);
    return LCL_RC_ERR;
  }

  prog = lcl_program_compile(lcl_value_to_string(script_v), "<catch>");
  lcl_ref_dec(script_v);

  if (!prog) {
    free(result_var);
    free(error_var);
    LCL_ERR_MSG(interp, "catch: failed to compile script");
    return LCL_RC_ERR;
  }

  rc = lcl_eval_program(interp, prog, &result);
  lcl_program_free(prog);

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

  n = strtol(s, &endptr, 10);

  if (*endptr == '\0') {
    return n != 0;
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

  if (argc < 2 || argc > 4) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    else_body_idx = 2;
  } else if (argc == 4) {
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

  if (lcl_eval_word_to_str(interp, args[body_idx], &body_v) != LCL_RC_OK) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<if>");
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

/* Helper to check if a word is a literal matching a specific string */
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
  lcl_value *body_v = NULL;
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  lcl_value *last = NULL;
  int test_is_braced;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[0]->braced;

  if (test_is_braced) {
    lcl_value *test_v = NULL;

    if (lcl_eval_word_to_str(interp, args[0], &test_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    test_p = lcl_program_compile(lcl_value_to_string(test_v), "<while-test>");
    lcl_ref_dec(test_v);

    if (!test_p) {
      return LCL_RC_ERR;
    }
  }

  if (lcl_eval_word_to_str(interp, args[1], &body_v) != LCL_RC_OK) {
    if (test_p) {
      lcl_program_free(test_p);
    }

    return LCL_RC_ERR;
  }

  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<while-body>");
  lcl_ref_dec(body_v);

  if (!body_p) {
    if (test_p) {
      lcl_program_free(test_p);
    }

    return LCL_RC_ERR;
  }

  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);

      if (rc != LCL_RC_OK) {
        lcl_program_free(test_p);
        lcl_program_free(body_p);

        if (last) {
          lcl_ref_dec(last);
        }

        return rc;
      }
    } else {
      if (lcl_eval_word(interp, args[0], &cond_v) != LCL_RC_OK) {
        lcl_program_free(body_p);

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
      if (test_p) {
        lcl_program_free(test_p);
      }

      lcl_program_free(body_p);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      if (test_p) {
        lcl_program_free(test_p);
      }

      lcl_program_free(body_p);
      *out = last;

      return LCL_RC_RETURN;
    }
  }

  if (test_p) {
    lcl_program_free(test_p);
  }

  lcl_program_free(body_p);

  *out = last ? last : lcl_string_new("");

  return LCL_RC_OK;
}

/* for start test next body - Tcl-style for loop */
static int s_for(lcl_interp *interp, int argc, const lcl_word **args,
                 lcl_value **out) {
  lcl_value *start_v = NULL;
  lcl_value *body_v = NULL;
  lcl_value *next_v = NULL;
  lcl_program *start_p = NULL;
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  lcl_program *next_p = NULL;
  lcl_value *last = NULL;
  lcl_value *tmp = NULL;
  int test_is_braced;
  int rc;

  if (argc != 4) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[1]->braced;

  if (lcl_eval_word_to_str(interp, args[0], &start_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  start_p = lcl_program_compile(lcl_value_to_string(start_v), "<for-start>");
  lcl_ref_dec(start_v);

  if (!start_p) {
    return LCL_RC_ERR;
  }

  if (test_is_braced) {
    lcl_value *test_v = NULL;

    if (lcl_eval_word_to_str(interp, args[1], &test_v) != LCL_RC_OK) {
      lcl_program_free(start_p);
      return LCL_RC_ERR;
    }

    test_p = lcl_program_compile(lcl_value_to_string(test_v), "<for-test>");
    lcl_ref_dec(test_v);

    if (!test_p) {
      lcl_program_free(start_p);
      return LCL_RC_ERR;
    }
  }

  if (lcl_eval_word_to_str(interp, args[2], &next_v) != LCL_RC_OK) {
    lcl_program_free(start_p);

    if (test_p) {
      lcl_program_free(test_p);
    }

    return LCL_RC_ERR;
  }

  next_p = lcl_program_compile(lcl_value_to_string(next_v), "<for-next>");
  lcl_ref_dec(next_v);

  if (!next_p) {
    lcl_program_free(start_p);

    if (test_p) {
      lcl_program_free(test_p);
    }

    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[3], &body_v) != LCL_RC_OK) {
    lcl_program_free(start_p);

    if (test_p) {
      lcl_program_free(test_p);
    }

    lcl_program_free(next_p);

    return LCL_RC_ERR;
  }

  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<for-body>");
  lcl_ref_dec(body_v);

  if (!body_p) {
    lcl_program_free(start_p);

    if (test_p) {
      lcl_program_free(test_p);
    }

    lcl_program_free(next_p);

    return LCL_RC_ERR;
  }

  rc = lcl_eval_program(interp, start_p, &tmp);
  lcl_program_free(start_p);

  if (tmp) {
    lcl_ref_dec(tmp);
  }

  if (rc != LCL_RC_OK) {
    if (test_p) {
      lcl_program_free(test_p);
    }

    lcl_program_free(body_p);
    lcl_program_free(next_p);

    return rc;
  }

  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);

      if (rc != LCL_RC_OK) {
        lcl_program_free(test_p);
        lcl_program_free(body_p);
        lcl_program_free(next_p);

        if (last) {
          lcl_ref_dec(last);
        }

        return rc;
      }
    } else {
      if (lcl_eval_word(interp, args[1], &cond_v) != LCL_RC_OK) {
        lcl_program_free(body_p);
        lcl_program_free(next_p);

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
          lcl_program_free(test_p);
        }

        lcl_program_free(body_p);
        lcl_program_free(next_p);

        if (last) {
          lcl_ref_dec(last);
        }

        return rc;
      }

      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      if (test_p) {
        lcl_program_free(test_p);
      }

      lcl_program_free(body_p);
      lcl_program_free(next_p);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {

      if (test_p) {
        lcl_program_free(test_p);
      }

      lcl_program_free(body_p);
      lcl_program_free(next_p);

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
        lcl_program_free(test_p);
      }

      lcl_program_free(body_p);
      lcl_program_free(next_p);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }
  }

  if (test_p) {
    lcl_program_free(test_p);
  }

  lcl_program_free(body_p);
  lcl_program_free(next_p);

  *out = last ? last : lcl_string_new("");

  return LCL_RC_OK;
}

/* foreach varname list body - iterate over list elements */
static int s_foreach(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_value *varname_v = NULL;
  lcl_value *list_v = NULL;
  lcl_value *body_v = NULL;
  lcl_program *body_p = NULL;
  lcl_value *last = NULL;
  const char *varname;
  int i;
  int list_len;
  int rc;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &varname_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  varname = lcl_value_to_string(varname_v);

  if (lcl_eval_word(interp, args[1], &list_v) != LCL_RC_OK) {
    lcl_ref_dec(varname_v);

    return LCL_RC_ERR;
  }

  if (list_v->type != LCL_LIST) {
    lcl_value *parsed = lcl_list_new_from_cwords(lcl_value_to_string(list_v));
    lcl_ref_dec(list_v);

    if (!parsed) {
      lcl_ref_dec(varname_v);

      return LCL_RC_ERR;
    }

    list_v = parsed;
  }

  if (lcl_eval_word_to_str(interp, args[2], &body_v) != LCL_RC_OK) {
    lcl_ref_dec(varname_v);
    lcl_ref_dec(list_v);

    return LCL_RC_ERR;
  }

  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<foreach>");
  lcl_ref_dec(body_v);

  if (!body_p) {
    lcl_ref_dec(varname_v);
    lcl_ref_dec(list_v);

    return LCL_RC_ERR;
  }

  list_len = (int)lcl_list_len(list_v);

  for (i = 0; i < list_len; i++) {
    lcl_value *elem = NULL;

    if (lcl_list_get(list_v, (size_t)i, &elem) != LCL_OK) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    if (lcl_env_let(&interp->env, varname, elem) != LCL_OK) {
      lcl_ref_dec(elem);
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);

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
      lcl_program_free(body_p);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);

      *out = last;

      return LCL_RC_RETURN;
    }
  }

  lcl_ref_dec(varname_v);
  lcl_ref_dec(list_v);
  lcl_program_free(body_p);

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
  lcl_value *params_list = NULL;
  lcl_upvalue *upvals = NULL;
  int nupvals = 0;

  if (lcl_eval_word_to_str(interp, params_word, &params_s) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, body_word, &body_s) != LCL_RC_OK) {
    lcl_ref_dec(params_s);
    return LCL_RC_ERR;
  }

  /* TODO: proper Tcl list parser; MVP split on spaces */
  params_list = lcl_list_new_from_cwords(lcl_value_to_string(params_s));
  lcl_ref_dec(params_s);

  body_p = lcl_program_compile(lcl_value_to_string(body_s), "<lambda>");
  lcl_ref_dec(body_s);

  if (!body_p) {
    lcl_ref_dec(params_list);
    return LCL_RC_ERR;
  }

  /* Build upvalues (flat closure) from variables referenced in body.
   * self_name is excluded from capture - it will be injected at runtime.
   * This also enforces forward reference check - errors if undefined var. */
  if (lcl_build_upvalues(interp, body_p, params_list, self_name, &upvals,
                         &nupvals) != LCL_RC_OK) {
    lcl_ref_dec(params_list);
    lcl_program_free(body_p);
    return LCL_RC_ERR;
  }

  /* upvals can be NULL if no captures needed - that's okay */
  /* lcl_proc_new takes ownership of body_p and upvals */
  *out = lcl_proc_new(self_name, upvals, nupvals, params_list, body_p);
  lcl_ref_dec(params_list);

  if (!*out) {
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

    self_name = lcl_value_to_string(name_v);
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

/* resolve or create a namespace path like "a::b::c".
 * Creates intermediate namespaces as needed.
 * Returns the final namespace with +1 refcount, or NULL on error. */
static lcl_value *resolve_or_create_ns_path(lcl_interp *interp,
                                            const char *path) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  if (!lcl_ns_split(path, first, sizeof(first), &rest)) {
    lcl_value *ns = NULL;

    if (lcl_env_get_value(&interp->env, path, &ns) == LCL_OK) {
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

    if (lcl_env_let(&interp->env, path, ns) != LCL_OK) {
      lcl_ref_dec(ns);
      return NULL;
    }

    return ns;
  }

  if (lcl_env_get_value(&interp->env, first, &current) != LCL_OK) {
    current = lcl_ns_new(first);

    if (!current) {
      return NULL;
    }

    if (lcl_env_let(&interp->env, first, current) != LCL_OK) {
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

/* Note: `namespace eval` simplified to `namespace`. */
static int s_namespace(lcl_interp *interp, int argc, const lcl_word **args,
                       lcl_value **out) {
  /* namespace { body }      - anonymous, returns ns value
   * namespace name { body } - named, auto-attaches to registry */
  int named;
  const lcl_word *body_word;
  lcl_value *name_v = NULL;
  char *ns_name = NULL;
  lcl_value *body_v = NULL;
  lcl_program *prog = NULL;
  lcl_frame *old_frame = NULL;
  lcl_def_target *target;
  lcl_return_code rc;
  int i;
  lcl_value *last = NULL;
  lcl_value *exports = NULL;
  lcl_value *ns = NULL;

  if (argc < 1 || argc > 2) {
    LCL_ERR_MSG(interp, "namespace: expected 1 or 2 arguments");
    return LCL_RC_ERR;
  }

  named = (argc == 2);
  body_word = named ? args[1] : args[0];

  if (named) {
    if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }
    ns_name = strdup(lcl_value_to_string(name_v));
    lcl_ref_dec(name_v);
    if (!ns_name) {
      return LCL_RC_ERR;
    }
  }

  if (lcl_eval_word_to_str(interp, body_word, &body_v) != LCL_RC_OK) {
    free(ns_name);
    return LCL_RC_ERR;
  }

  prog = lcl_program_compile(lcl_value_to_string(body_v), "<namespace>");
  lcl_ref_dec(body_v);

  if (!prog) {
    free(ns_name);
    return LCL_RC_ERR;
  }

  if (lcl_def_target_push(interp, interp->env.frame) != LCL_OK) {
    lcl_program_free(prog);
    free(ns_name);
    return LCL_RC_ERR;
  }

  target = &interp->def_stack[interp->def_depth - 1];
  old_frame = interp->env.frame;

  /* If re-entering an existing namespace, pre-populate overlay with its
   * bindings. This handles both top-level re-entry and nested re-entry within a
   * builder. */
  if (ns_name && !strchr(ns_name, ':')) {
    lcl_value *existing_ns = NULL;
    /* Look up the namespace - either from environment (top-level) or from
     * parent builder's overlay (nested). lcl_env_get_value will check
     * the current frame chain which includes the parent overlay. */
    if (lcl_env_get_value(&interp->env, ns_name, &existing_ns) == LCL_OK) {
      if (existing_ns->type == LCL_NAMESPACE) {
        hash_iter it = {0};
        const char *key;
        lcl_value *value;
        while (hash_table_iterate(existing_ns->as.namespace.namespace, &it,
                                  &key, &value)) {
          hash_table_put(target->overlay->locals, key, value);
          lcl_dict_put(&target->exports, key, value);
          lcl_ref_dec(value); /* Balance iterate */
        }
      }
      lcl_ref_dec(existing_ns);
    }
  }

  interp->env.frame = target->overlay;

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    interp->env.frame = old_frame;
    lcl_def_target_pop(interp);
    lcl_program_free(prog);
    free(ns_name);
    LCL_ERR_MSG(interp, "namespace: max recursion depth exceeded");
    return LCL_RC_ERR;
  }

  interp->depth++;
  rc = LCL_RC_OK;

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

  interp->depth--;
  interp->env.frame = old_frame;
  exports = lcl_def_target_pop(interp);
  lcl_program_free(prog);

  if (last) {
    lcl_ref_dec(last);
  }

  if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
    if (exports) {
      lcl_ref_dec(exports);
    }
    free(ns_name);
    return rc;
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
      if (interp->def_depth > 0) {
        if (lcl_def_target_bind(interp, ns_name, ns) != LCL_OK) {
          lcl_ref_dec(ns);
          free(ns_name);
          LCL_ERR_MSG(interp,
                      "namespace: failed to bind in parent builder");
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

static int s_subst(lcl_interp *interp, int argc, const lcl_word **args,
                   lcl_value **out) {
  lcl_value *input_v = NULL;
  const char *src;
  size_t src_len;
  size_t i;
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(input_v);
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

          if (lcl_env_get_value(&interp->env, name, &val) != LCL_OK) {
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

          val_str = lcl_value_to_string(val);

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

          if (lcl_env_get_value(&interp->env, name, &val) != LCL_OK) {
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

          val_str = lcl_value_to_string(val);

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

        prog = lcl_program_compile(subcmd_src, "<subst>");
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
        }

        interp->depth--;
        lcl_program_free(prog);

        if (rc != LCL_RC_OK) {
          if (subcmd_result) {
            lcl_ref_dec(subcmd_result);
          }
          goto err;
        }

        result_str = subcmd_result ? lcl_value_to_string(subcmd_result) : "";

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

/* IR node types for quasiquote */
typedef enum {
  QQ_LITERAL, /* Literal text */
  QQ_EVAL,    /* ,expr - evaluate and insert */
  QQ_SPLICE   /* ,@expr - evaluate and splice list elements */
} qq_node_kind;

typedef struct qq_node {
  qq_node_kind kind;
  char *text;        /* For LITERAL: the text; for EVAL/SPLICE: word source */
  int prefix_commas; /* For EVAL: number of commas to prepend to result */
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

/* Check if string starts with "quasiquote" followed by whitespace and { */
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

  /* Must be followed by whitespace then { */
  i = pos + kw_len;
  while (i < len && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n')) {
    i++;
  }
  return (i < len && src[i] == '{');
}

/* Find matching } for quasiquote block starting at { */
static size_t find_quasiquote_end(const char *src, size_t len, size_t start) {
  return skip_braces(src, len, start + 1) + 1;
}

/* Parse a word after comma - returns end position, sets word_start/word_end */
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

  /* Bugfix: bare word (alphanumeric, underscore, hyphen, colon for namespaces)
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
        size_t inner_start, inner_end;
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
      size_t word_start, word_end;

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

/* Check if a string needs braces to be a valid unquote word.
 * Returns 1 if braces needed, 0 if bare word is fine. */
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
      lcl_program *prog;
      lcl_value *val = NULL;
      const char *saved_file;
      int saved_line;
      int eval_rc;
      int j;

      for (j = 0; j < node->prefix_commas; j++) {
        if (!buf_append_char(result, result_len, result_cap, ',')) {
          return 0;
        }
      }

      prog = lcl_program_compile(node->text, "<quasiquote>");
      if (!prog) {
        LCL_ERR_MSG(interp, "failed to compile unquote expression");
        return 0;
      }

      saved_file = interp->cur_file;
      saved_line = interp->cur_line;

      eval_rc = lcl_eval_program(interp, prog, &val);

      interp->cur_file = saved_file;
      interp->cur_line = saved_line;

      lcl_program_free(prog);

      if (eval_rc != LCL_RC_OK) {
        return 0;
      }

      if (node->kind == QQ_SPLICE) {
        lcl_value *list_val = val;

        if (val->type != LCL_LIST) {
          list_val = lcl_list_new_from_cwords(lcl_value_to_string(val));
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

            elem_str = lcl_value_to_string(elem);

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
        /* Eval: insert stringified value */
        const char *val_str = lcl_value_to_string(val);
        /* When prefix_commas > 0, wrap in braces only if needed.
         * Simple values like "42" or "hello" can be bare words.
         * Values with spaces or special chars need braces. */
        int needs_braces = node->prefix_commas > 0 && qq_needs_braces(val_str);
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

  if (argc != 1) {
    LCL_ERR_MSG(interp, "quasiquote requires exactly one argument");
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(input_v);
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

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  /* MVP: single argument */
  if (argc == 1) {
    lcl_value *script_v = NULL;

    if (lcl_eval_word_to_str(interp, args[0], &script_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    prog = lcl_program_compile(lcl_value_to_string(script_v), "<eval>");
    lcl_ref_dec(script_v);
  } else {
    size_t total_len = 0;
    lcl_value **parts = NULL;
    char *script_str = NULL;
    char *p;

    parts = malloc(sizeof(lcl_value *) * (size_t)argc);
    if (!parts) {
      return LCL_RC_ERR;
    }

    for (i = 0; i < argc; i++) {
      if (lcl_eval_word_to_str(interp, args[i], &parts[i]) != LCL_RC_OK) {
        int j;
        for (j = 0; j < i; j++) {
          lcl_ref_dec(parts[j]);
        }
        free(parts);
        return LCL_RC_ERR;
      }
      total_len += strlen(lcl_value_to_string(parts[i]));
    }

    total_len += (size_t)(argc - 1);

    script_str = malloc(total_len + 1);
    if (!script_str) {
      for (i = 0; i < argc; i++) {
        lcl_ref_dec(parts[i]);
      }
      free(parts);
      return LCL_RC_ERR;
    }

    p = script_str;
    for (i = 0; i < argc; i++) {
      const char *s = lcl_value_to_string(parts[i]);
      size_t l = strlen(s);
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

    prog = lcl_program_compile(script_str, "<eval>");
    free(script_str);
  }

  if (!prog) {
    return LCL_RC_ERR;
  }

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

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

static int s_load(lcl_interp *interp, int argc, const lcl_word **args,
                  lcl_value **out) {
  lcl_value *path_v = NULL;
  const char *path;
  char *src = NULL;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int i;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(path_v);
  src = read_file(path, NULL);

  if (!src) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  prog = lcl_program_compile(src, path);
  free(src);

  if (!prog) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(path_v); /* prog->file now owns a copy of the path string */

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

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

    form = lcl_value_to_string(form_v);

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
      lcl_ref_dec(current);

      return rc;
    }

    lcl_ref_dec(current);
    current = result;
  }

  *out = current;
  return LCL_RC_OK;
}

/* ============================================================================
 * Thread-last operator: ->> initial {form1} {form2} ...
 * Threads the value through each form as the last argument.
 * Example: ->> $d {cmd a b} becomes: cmd a b $d
 *          ->> 10 {$f a} becomes: [$f a 10]
 *          ->> 10 {[lambda {x} ...]} becomes: [[lambda {x} ...] 10]
 * ============================================================================
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

    form = lcl_value_to_string(form_v);

    if (form[0] == '[' || form[0] == '$') {
      total = 1 + strlen(form) + 11 + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }
      sprintf(threaded, "[%s $_thread_]", form);
    } else {
      total = strlen(form) + 11 + 1; /* 11 = " $_thread_" */
      threaded = (char *)malloc(total);

      if (!threaded) {
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
      lcl_ref_dec(current);

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

  if (argc != 3) {
    LCL_ERR_MSG(interp, "proc: expected 3 arguments");
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  name_str = lcl_value_to_string(name_v);

  if (strstr(name_str, "::") && interp->def_depth == 0) {
    LCL_ERR_MSG(interp, "qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::proc'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  rc = make_lambda(interp, name_str, args[1], args[2], &lam);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return rc;
  }

  if (interp->def_depth > 0) {
    if (lcl_def_target_bind(interp, name_str, lam) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name_str, lam) != LCL_OK) {
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

/* ============================================================================
 * List Commands
 * ============================================================================
 */

/* list ?value ...? - construct a list from arguments */
static int c_list(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  lcl_value *list;
  int i;
  (void)interp;

  list = lcl_list_new();
  if (!list) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc; i++) {
    if (lcl_list_push(&list, argv[i]) != LCL_OK) {
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

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (argc == 1) {
    *out = lcl_ref_inc(list);

    return LCL_RC_OK;
  }

  if (argc == 2) {
    if (list->type != LCL_LIST) {
      if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
        return LCL_RC_ERR;
      }
      if (idx == 0) {
        *out = lcl_ref_inc(list);

        return LCL_RC_OK;
      }

      *out = lcl_string_new("");

      return LCL_RC_OK;
    }

    if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
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
        if (lcl_value_to_int(argv[i], &idx) != LCL_OK) {
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

      if (lcl_value_to_int(argv[i], &idx) != LCL_OK) {
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

  if (argc < 2 || argc > 3) {
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[0], &start) != LCL_OK) {
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[1], &end) != LCL_OK) {
    return LCL_RC_ERR;
  }

  step = 1;

  if (argc == 3) {
    if (lcl_value_to_int(argv[2], &step) != LCL_OK) {
      return LCL_RC_ERR;
    }
    if (step == 0) {
      return LCL_RC_ERR;
    }
  }

  result = lcl_list_new();

  if (!result) {
    return LCL_RC_ERR;
  }

  if (step > 0) {
    for (i = start; i < end; i += step) {
      num = lcl_int_new(i);

      if (!num || lcl_list_push(&result, num) != LCL_OK) {
        if (num) {
          lcl_ref_dec(num);
        }
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }

      lcl_ref_dec(num);
    }
  } else {
    for (i = start; i > end; i += step) {
      num = lcl_int_new(i);
      if (!num || lcl_list_push(&result, num) != LCL_OK) {
        if (num) {
          lcl_ref_dec(num);
        }
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }

      lcl_ref_dec(num);
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
  (void)interp;

  if (argc < 1 || argc > 2) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  if (argc == 2) {
    sep = lcl_value_to_string(argv[1]);
  }

  sep_len = strlen(sep);

  if (list->type != LCL_LIST) {
    *out = lcl_string_new(lcl_value_to_string(list));

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

    elem_str = lcl_value_to_string(elem);
    elem_len = strlen(elem_str);

    if (i > 0 && sep_len > 0) {
      if (!buf_append(&buf, &buf_len, &buf_cap, sep, sep_len)) {
        lcl_ref_dec(elem);
        free(buf);

        return LCL_RC_ERR;
      }
    }

    if (!buf_append(&buf, &buf_len, &buf_cap, elem_str, elem_len)) {
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
  (void)interp;

  if (argc < 1 || argc > 2) {
    return LCL_RC_ERR;
  }

  str = lcl_value_to_string(argv[0]);

  if (argc == 2) {
    split_chars = lcl_value_to_string(argv[1]);
  }

  result = lcl_list_new();

  if (!result) {
    return LCL_RC_ERR;
  }

  if (!split_chars || *split_chars == '\0') {
    const char *p = str;

    while (*p) {
      char c[2] = {*p, '\0'};
      lcl_value *elem = lcl_string_new(c);

      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
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
          lcl_ref_dec(result);
          return LCL_RC_ERR;
        }

        memcpy(word, start, len);
        word[len] = '\0';
        elem = lcl_string_new(word);
        free(word);

        if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
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

/* ============================================================================
 * Generic Type-Directed Operations
 * ============================================================================
 */

/* len x - returns length of list, dict, or string */
static int c_len(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST:
    *out = lcl_int_new((long)lcl_list_len(argv[0]));
    return LCL_RC_OK;

  case LCL_DICT:
    *out = lcl_int_new((long)lcl_dict_len(argv[0]));
    return LCL_RC_OK;

  case LCL_STRING:
    *out = lcl_int_new((long)strlen(lcl_value_to_string(argv[0])));
    return LCL_RC_OK;

  default: return LCL_RC_ERR;
  }
}

/* empty? x - returns 1 if container is empty */
static int c_empty(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST:
    *out = lcl_int_new(lcl_list_len(argv[0]) == 0 ? 1 : 0);
    return LCL_RC_OK;

  case LCL_DICT:
    *out = lcl_int_new(lcl_dict_len(argv[0]) == 0 ? 1 : 0);
    return LCL_RC_OK;

  case LCL_STRING:
    *out = lcl_int_new(strlen(lcl_value_to_string(argv[0])) == 0 ? 1 : 0);
    return LCL_RC_OK;

  case LCL_OPAQUE: *out = lcl_int_new(0); return LCL_RC_OK;

  default: return LCL_RC_ERR;
  }
}

/* get x k [default] - get element by key/index */
static int c_generic_get(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  (void)interp;

  if (argc < 2 || argc > 3) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;
    if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_list_get(argv[0], (size_t)idx, out) != LCL_OK) {
      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      return LCL_RC_ERR;
    }

    return LCL_RC_OK;
  }

  case LCL_DICT: {
    const char *key = lcl_value_to_string(argv[1]);
    if (lcl_dict_get(argv[0], key, out) != LCL_OK) {
      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      return LCL_RC_ERR;
    }

    return LCL_RC_OK;
  }

  case LCL_STRING: {
    long idx;
    const char *str;
    char buf[2];
    if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
      return LCL_RC_ERR;
    }

    str = lcl_value_to_string(argv[0]);

    if (idx < 0 || (size_t)idx >= strlen(str)) {
      if (argc == 3) {
        *out = lcl_ref_inc(argv[2]);

        return LCL_RC_OK;
      }

      return LCL_RC_ERR;
    }

    buf[0] = str[idx];
    buf[1] = '\0';

    *out = lcl_string_new(buf);

    return LCL_RC_OK;
  }

  default: return LCL_RC_ERR;
  }
}

/* put x k v - return new container with element added/replaced */
static int c_put(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;
    lcl_value *copy;

    if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
      return LCL_RC_ERR;
    }

    copy = lcl_ref_inc(argv[0]);

    if (lcl_list_set(&copy, (size_t)idx, argv[2]) != LCL_OK) {
      lcl_ref_dec(copy);
      return LCL_RC_ERR;
    }

    *out = copy;

    return LCL_RC_OK;
  }

  case LCL_DICT: {
    const char *key = lcl_value_to_string(argv[1]);
    lcl_value *copy = lcl_ref_inc(argv[0]);

    if (lcl_dict_put(&copy, key, argv[2]) != LCL_OK) {
      lcl_ref_dec(copy);
      return LCL_RC_ERR;
    }

    *out = copy;

    return LCL_RC_OK;
  }

  default: return LCL_RC_ERR;
  }
}

/* del x k - return new container without element */
static int c_del(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_DICT: {
    const char *key = lcl_value_to_string(argv[1]);
    lcl_value *copy = lcl_ref_inc(argv[0]);
    lcl_dict_del(&copy, key);

    *out = copy;

    return LCL_RC_OK;
  }

  default:
    /* TODO(bjh) del on list not implemented for MVP - could remove by index */
    return LCL_RC_ERR;
  }
}

/* has? x k - check if key/index exists */
static int c_has(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  switch (argv[0]->type) {
  case LCL_LIST: {
    long idx;

    if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(idx >= 0 && (size_t)idx < lcl_list_len(argv[0]) ? 1 : 0);

    return LCL_RC_OK;
  }

  case LCL_DICT: {
    const char *key = lcl_value_to_string(argv[1]);
    lcl_value *val;

    if (lcl_dict_get(argv[0], key, &val) == LCL_OK) {
      lcl_ref_dec(val);
      *out = lcl_int_new(1);
    } else {
      *out = lcl_int_new(0);
    }

    return LCL_RC_OK;
  }

  default: return LCL_RC_ERR;
  }
}

/* ============================================================================
 * Type Predicates
 * ============================================================================
 */

static int c_is_list(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_LIST ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_dict(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_DICT ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_string(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_STRING ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_opaque(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_OPAQUE ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_number(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_INT || argv[0]->type == LCL_FLOAT) {
    *out = lcl_int_new(1);
  } else if (argv[0]->type == LCL_STRING) {
    const char *s = lcl_value_to_string(argv[0]);
    char *end;
    (void)strtol(s, &end, 10);

    if (end != s && *end == '\0') {
      *out = lcl_int_new(1);
    } else {
      (void)strtod(s, &end);
      *out = lcl_int_new(end != s && *end == '\0' ? 1 : 0);
    }
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

static int c_is_int(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_INT ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_float(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_FLOAT ? 1 : 0);

  return LCL_RC_OK;
}

static int c_is_proc(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      argv[0]->type == LCL_PROC || argv[0]->type == LCL_CPROC ? 1 : 0);

  return LCL_RC_OK;
}

/* int x - convert value to integer */
static int c_to_int(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  long val;
  double fval;

  if (argc != 1) {
    LCL_ERR_MSG(interp, "int requires exactly one argument");
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_INT) {
    *out = lcl_ref_inc(argv[0]);
    return LCL_RC_OK;
  }

  if (argv[0]->type == LCL_FLOAT) {
    *out = lcl_int_new((long)argv[0]->as.f);
    return LCL_RC_OK;
  }

  if (lcl_value_to_int(argv[0], &val) == LCL_OK) {
    *out = lcl_int_new(val);
    return LCL_RC_OK;
  }

  if (lcl_value_to_float(argv[0], &fval) == LCL_OK) {
    *out = lcl_int_new((long)fval);
    return LCL_RC_OK;
  }

  LCL_ERR_MSG(interp, "cannot convert to integer");
  return LCL_RC_ERR;
}

/* float x - convert value to float */
static int c_to_float(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  double val;

  if (argc != 1) {
    LCL_ERR_MSG(interp, "float requires exactly one argument");
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
    LCL_ERR_MSG(interp, "cannot convert to float");
    return LCL_RC_ERR;
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
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  copy = lcl_ref_inc(argv[0]);

  if (lcl_list_push(&copy, argv[1]) != LCL_OK) {
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
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(argv[0]);

  if (len == 0) {
    return LCL_RC_ERR;
  }

  copy = lcl_list_new();

  for (i = 0; i < len - 1; i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
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

  if (argc < 2 || argc > 3) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(argv[0]);

  if (lcl_value_to_int(argv[1], &start) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    if (lcl_value_to_int(argv[2], &end) != LCL_OK) {
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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST || argv[1]->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  for (i = 0; i < lcl_list_len(argv[0]); i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i, &elem) != LCL_OK) {
      lcl_ref_dec(result);

      return LCL_RC_ERR;
    }

    lcl_list_push(&result, elem);
    lcl_ref_dec(elem);
  }

  for (i = 0; i < lcl_list_len(argv[1]); i++) {
    lcl_value *elem;

    if (lcl_list_get(argv[1], i, &elem) != LCL_OK) {
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

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(argv[0]);
  result = lcl_list_new();

  for (i = len; i > 0; i--) {
    lcl_value *elem;

    if (lcl_list_get(argv[0], i - 1, &elem) != LCL_OK) {
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

/* List::map f list - apply f to each element, return new list */
static int c_list_map(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  list = argv[1];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *mapped = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
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
      lcl_ref_dec(mapped);
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(mapped);
  }

  *out = result;
  return LCL_RC_OK;
}

/* List::filter f list - keep elements where f returns true */
static int c_list_filter(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  list = argv[1];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
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

/* List::reduce init f list - fold list with f(acc, elem) */
static int c_list_reduce(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *init;
  lcl_value *func;
  lcl_value *list;
  lcl_value *acc;
  size_t i;
  size_t len;
  int rc;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  init = argv[0];
  func = argv[1];
  list = argv[2];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  acc = lcl_ref_inc(init);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *new_acc = NULL;
    lcl_value *call_args[2];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
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

/* List::sort list - sort list lexicographically by string value */
static int c_list_sort(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  lcl_value **items;
  size_t i;
  size_t j;
  size_t len;
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  if (len == 0) {
    *out = lcl_list_new();
    return LCL_RC_OK;
  }

  items = malloc(len * sizeof(lcl_value *));

  if (!items) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    if (lcl_list_get(list, i, &items[i]) != LCL_OK) {
      while (i > 0) {
        lcl_ref_dec(items[--i]);
      }

      free(items);

      return LCL_RC_ERR;
    }
  }

  for (i = 1; i < len; i++) {
    lcl_value *key = items[i];
    const char *key_str = lcl_value_to_string(key);
    j = i;

    while (j > 0 && strcmp(lcl_value_to_string(items[j - 1]), key_str) > 0) {
      items[j] = items[j - 1];
      j--;
    }

    items[j] = key;
  }

  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_list_push(&result, items[i]);
    lcl_ref_dec(items[i]);
  }

  free(items);
  *out = result;
  return LCL_RC_OK;
}

/* List::sort_by f list - sort using comparison function f(a, b) -> int */
static int c_list_sort_by(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  lcl_value **items;
  size_t i;
  size_t j;
  size_t k;
  size_t len;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  list = argv[1];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  if (len == 0) {
    *out = lcl_list_new();
    return LCL_RC_OK;
  }

  items = malloc(len * sizeof(lcl_value *));

  if (!items) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    if (lcl_list_get(list, i, &items[i]) != LCL_OK) {
      while (i > 0) {
        lcl_ref_dec(items[--i]);
      }
      free(items);
      return LCL_RC_ERR;
    }
  }

  for (i = 1; i < len; i++) {
    lcl_value *key = items[i];
    j = i;
    while (j > 0) {
      lcl_value *cmp_args[2];
      lcl_value *cmp_result = NULL;
      long cmp_val;

      cmp_args[0] = items[j - 1];
      cmp_args[1] = key;
      rc = lcl_call_proc(interp, func, 2, cmp_args, &cmp_result);

      if (rc != LCL_RC_OK) {
        for (k = 0; k < len; k++) {
          lcl_ref_dec(items[k]);
        }
        free(items);
        return rc;
      }

      if (lcl_value_to_int(cmp_result, &cmp_val) != LCL_OK) {
        lcl_ref_dec(cmp_result);
        for (k = 0; k < len; k++) {
          lcl_ref_dec(items[k]);
        }
        free(items);
        return LCL_RC_ERR;
      }

      lcl_ref_dec(cmp_result);

      if (cmp_val <= 0) {
        break;
      }

      items[j] = items[j - 1];
      j--;
    }
    items[j] = key;
  }

  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_list_push(&result, items[i]);
    lcl_ref_dec(items[i]);
  }

  free(items);
  *out = result;
  return LCL_RC_OK;
}

/* List::find pred list - find first element where pred returns true */
static int c_list_find(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  list = argv[1];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
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

/* List::any pred list - return 1 if any element satisfies pred */
static int c_list_any(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  list = argv[1];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
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

/* List::all pred list - return 1 if all elements satisfy pred */
static int c_list_all(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  list = argv[1];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    lcl_value *pred_result = NULL;
    lcl_value *call_args[1];

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
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
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  result = lcl_list_new();
  seen = hash_table_new();

  if (!seen) {
    lcl_ref_dec(result);
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *str;
    lcl_value *dummy;

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      hash_table_free(seen);
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    str = lcl_value_to_string(elem);

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

/* List::flatten list - flatten nested lists one level */
static int c_list_flatten(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (list->type != LCL_LIST) {
    return LCL_RC_ERR;
  }

  len = lcl_list_len(list);
  result = lcl_list_new();

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      lcl_ref_dec(result);
      return LCL_RC_ERR;
    }

    if (elem->type == LCL_LIST) {
      size_t j;
      size_t sublen = lcl_list_len(elem);
      for (j = 0; j < sublen; j++) {
        lcl_value *subelem = NULL;
        if (lcl_list_get(elem, j, &subelem) != LCL_OK) {
          lcl_ref_dec(elem);
          lcl_ref_dec(result);
          return LCL_RC_ERR;
        }
        lcl_list_push(&result, subelem);
        lcl_ref_dec(subelem);
      }
    } else {
      lcl_list_push(&result, elem);
    }

    lcl_ref_dec(elem);
  }

  *out = result;
  return LCL_RC_OK;
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

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return LCL_RC_ERR;
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

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return LCL_RC_ERR;
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

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return LCL_RC_ERR;
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

/* dict (constructor) - create dict from key-value pairs */
static int c_dict_create_proc(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lcl_value *dict;
  int i;
  (void)interp;

  if (argc % 2 != 0) {
    return LCL_RC_ERR;
  }

  dict = lcl_dict_new();
  if (!dict) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc; i += 2) {
    const char *key = lcl_value_to_string(argv[i]);

    if (lcl_dict_put(&dict, key, argv[i + 1]) != LCL_OK) {
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

  if (argc != 2) {
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

/* Dict::map f d - apply f to each key-value pair, f receives key and value,
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

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  dict = argv[1];

  if (dict->type != LCL_DICT) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
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

/* Dict::filter f d - keep entries where f(key, value) returns true */
static int c_dict_filter(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lcl_value *func;
  lcl_value *dict;
  lcl_value *result;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  func = argv[0];
  dict = argv[1];

  if (dict->type != LCL_DICT) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
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

/* Dict::reduce init f d - fold dict with f(acc, key, value) */
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

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  init = argv[0];
  func = argv[1];
  dict = argv[2];

  if (dict->type != LCL_DICT) {
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(func)) {
    return LCL_RC_ERR;
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
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
  len = strlen(src);
  result = malloc(len + 1);

  if (!result) {
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
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
  len = strlen(src);
  result = malloc(len + 1);

  if (!result) {
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
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  haystack = lcl_value_to_string(argv[0]);
  needle = lcl_value_to_string(argv[1]);

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
  (void)interp;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
  old_str = lcl_value_to_string(argv[1]);
  new_str = lcl_value_to_string(argv[2]);

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

  result_len = strlen(src) + (size_t)count * (new_len - old_len);
  result = malloc(result_len + 1);

  if (!result) {
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
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
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
  (void)interp;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
  len = strlen(src);

  if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
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
  (void)interp;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
  len = strlen(src);

  if (lcl_value_to_int(argv[1], &start) != LCL_OK ||
      lcl_value_to_int(argv[2], &end) != LCL_OK) {
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
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(argv[0]);
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
    return LCL_RC_ERR;
  }

  memcpy(result, start, len);
  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

void lcl_register_core(lcl_interp *interp) {
  lcl_value *list_ns;
  lcl_value *dict_ns;
  lcl_value *string_ns;

  lcl_register_proc(interp, "assert", c_assert);
  lcl_register_proc(interp, "puts", c_puts);

  lcl_register_proc(interp, "and", c_and);
  lcl_register_proc(interp, "or", c_or);
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

  lcl_register_proc(interp, "int", c_to_int);
  lcl_register_proc(interp, "float", c_to_float);

  lcl_register_proc(interp, "let", c_let);
  lcl_register_proc(interp, "ref", c_ref);
  lcl_register_proc(interp, "getvar", c_get);
  lcl_register_spec(interp, "var", s_var);
  lcl_register_spec(interp, "set!", s_set_bang);
  lcl_register_spec(interp, "binding-cell", s_binding_cell);
  lcl_register_spec(interp, "same-binding?", s_same_binding);

  lcl_register_spec(interp, "return", s_return);
  lcl_register_spec(interp, "lambda", s_lambda);
  lcl_register_spec(interp, "proc", s_proc);
  lcl_register_spec(interp, "eval", s_eval);
  lcl_register_spec(interp, "load", s_load);
  lcl_register_spec(interp, "subst", s_subst);
  lcl_register_spec(interp, "quasiquote", s_quasiquote);
  lcl_register_spec(interp, "namespace", s_namespace);
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
}
