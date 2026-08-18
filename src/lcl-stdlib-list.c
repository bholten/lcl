#include "lcl-stdlib-internal.h"

/* list ?value ...? - construct a list from arguments */
static lcl_return_code c_list(lcl_interp *interp, int argc, lcl_value **argv,
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
static lcl_return_code c_lindex(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lcl_value *list;
  long idx;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "List::index", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (argc == 1) {
    *out = lcl_ref_inc(list);

    return LCL_RC_OK;
  }

  if (argc == 2) {
    if (list->type != LCL_LIST) {
      if (!lcl_std_arg_int(interp, "List::index", argv[1], &idx)) {
        return LCL_RC_ERR;
      }

      if (idx == 0) {
        *out = lcl_ref_inc(list);

        return LCL_RC_OK;
      }

      *out = lcl_string_new("");

      return LCL_RC_OK;
    }

    if (!lcl_std_arg_int(interp, "List::index", argv[1], &idx)) {
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
        if (!lcl_std_arg_int(interp, "List::index", argv[i], &idx)) {
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

      if (!lcl_std_arg_int(interp, "List::index", argv[i], &idx)) {
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
static lcl_return_code c_lrange(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lcl_value *result;
  lcl_value *num;
  long start;
  long end;
  long step;
  long i;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "List::range", argc, 2, 3)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_int(interp, "List::range", argv[0], &start)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_int(interp, "List::range", argv[1], &end)) {
    return LCL_RC_ERR;
  }

  step = 1;

  if (argc == 3) {
    if (!lcl_std_arg_int(interp, "List::range", argv[2], &step)) {
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

      if (!lcl_std_safe_add_long(i, step, &next)) {
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

      if (!lcl_std_safe_add_long(i, step, &next)) {
        break;
      }

      i = next;
    }
  }

  *out = result;

  return LCL_RC_OK;
}

static lcl_return_code c_is_list(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "list?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_LIST ? 1 : 0);

  return LCL_RC_OK;
}

/* list::push x v - return new list with v appended */
static lcl_return_code c_list_push(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  lcl_value *copy;

  if (!lcl_std_chk_argc(interp, "List::push", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::push", "list", argv[0]);
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
static lcl_return_code c_list_pop(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  lcl_value *copy;
  size_t len;
  size_t i;

  if (!lcl_std_chk_argc(interp, "List::pop", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::pop", "list", argv[0]);
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
static lcl_return_code c_list_slice(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  long start;
  long end;
  size_t len;
  size_t i;
  lcl_value *result;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "List::slice", argc, 2, 3)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::slice", "list", argv[0]);
  }

  len = lcl_list_len(argv[0]);

  if (!lcl_std_arg_int(interp, "List::slice", argv[1], &start)) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    if (!lcl_std_arg_int(interp, "List::slice", argv[2], &end)) {
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
static lcl_return_code c_list_concat(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *result;
  size_t i;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "List::concat", argc, 2, 2)) {
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
static lcl_return_code c_list_reverse(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lcl_value *result;
  size_t len;
  size_t i;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "List::reverse", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::reverse", "list", argv[0]);
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

/* List::map list f - apply f to each element, return new list */
static lcl_return_code c_list_map(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "List::map", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::map", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "List::map", "callable", func);
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
static lcl_return_code c_list_filter(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  lcl_value *result;
  size_t i;
  size_t len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "List::filter", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::filter", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "List::filter", "callable", func);
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
static lcl_return_code c_list_reduce(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *init;
  lcl_value *func;
  lcl_value *list;
  lcl_value *acc;
  size_t i;
  size_t len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "List::reduce", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  init = argv[1];
  func = argv[2];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::reduce", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "List::reduce", "callable", func);
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

/*
 * All three sorts are stable merge sorts over (key, value) pairs.
 * List::sort and List::sort_by order by the shared total ordering
 * below; List::sort_with delegates ordering to a user comparator.
 *
 * Total ordering shared by List::sort and List::sort_by keys: numeric
 * when both operands are numeric (same coercion as the comparison
 * operators, with the integral fast path preserved), bytewise string
 * comparison otherwise. */
static int lcl_order_cmp(lcl_value *a, lcl_value *b) {
  lcl_value *pair[2];
  double fa;
  double fb;
  const char *sa;
  const char *sb;

  pair[0] = a;
  pair[1] = b;

  if (lcl_std_all_args_integral(2, pair)) {
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
static lcl_return_code list_sort_common(lcl_interp *interp, lcl_value *list,
                                        lcl_value *keyfn, lcl_sort_cmp_fn cmp,
                                        void *ctx, lcl_value **out) {
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
static lcl_return_code c_list_sort(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "List::sort", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::sort", "list", argv[0]);
  }

  return list_sort_common(interp, argv[0], NULL, cmp_pair_val, NULL, out);
}

/* List::sort_by list f - stable sort by key function f(elem) -> key.
 * f runs exactly once per element (decorate-sort-undecorate); keys
 * are compared with the ordinary total ordering. */
static lcl_return_code c_list_sort_by(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "List::sort_by", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    return lcl_std_err_expected_got(interp, "List::sort_by", "callable",
                                    argv[1]);
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::sort_by", "list", argv[0]);
  }

  return list_sort_common(interp, argv[0], argv[1], cmp_pair_key, NULL, out);
}

/* List::sort_with list f - stable sort with comparator f(a, b) -> int */
static lcl_return_code c_list_sort_with(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lcl_user_cmp_ctx ctx;

  if (!lcl_std_chk_argc(interp, "List::sort_with", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    return lcl_std_err_expected_got(interp, "List::sort_with", "callable",
                                    argv[0]);
  }

  if (argv[0]->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::sort_with", "list", argv[1]);
  }

  ctx.interp = interp;
  ctx.func = argv[1];

  return list_sort_common(interp, argv[0], NULL, cmp_pair_user, &ctx, out);
}

/* List::find list pred - find first element where pred returns true */
static lcl_return_code c_list_find(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "List::find", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::find", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "List::find", "callable", func);
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
static lcl_return_code c_list_any(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "List::any?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::any?", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "List::any?", "callable", func);
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
static lcl_return_code c_list_all(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *list;
  size_t i;
  size_t len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "List::all?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];
  func = argv[1];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::all?", "list", list);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "List::all?", "callable", func);
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
static lcl_return_code c_list_unique(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  hash_table *seen;
  size_t i;
  size_t len;

  if (!lcl_std_chk_argc(interp, "List::unique", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::unique", "list", list);
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

static lcl_return_code c_list_flatten(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  struct flatten_frame *stack;
  size_t depth;
  size_t cap;
  long limit = -1;

  if (!lcl_std_chk_argc(interp, "List::flatten", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (list->type != LCL_LIST) {
    return lcl_std_err_expected_got(interp, "List::flatten", "list", list);
  }

  if (argc == 2 && !lcl_std_arg_int(interp, "List::flatten", argv[1], &limit)) {
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

void lcl_std_register_list(lcl_interp *interp) {
  lcl_value *list_ns;

  lcl_register_proc(interp, "list?", c_is_list);
  lcl_register_proc(interp, "list", c_list);
  list_ns = lcl_ns_new("List");
  lcl_define_take(interp, "List", list_ns);
  lcl_ns_def_take(list_ns, "new", lcl_c_proc_new("List::new", c_list));
  lcl_ns_def_take(list_ns, "push", lcl_c_proc_new("List::push", c_list_push));
  lcl_ns_def_take(list_ns, "pop", lcl_c_proc_new("List::pop", c_list_pop));
  lcl_ns_def_take(list_ns, "slice",
                  lcl_c_proc_new("List::slice", c_list_slice));
  lcl_ns_def_take(list_ns, "concat",
                  lcl_c_proc_new("List::concat", c_list_concat));
  lcl_ns_def_take(list_ns, "reverse",
                  lcl_c_proc_new("List::reverse", c_list_reverse));
  lcl_ns_def_take(list_ns, "index", lcl_c_proc_new("List::index", c_lindex));
  lcl_ns_def_take(list_ns, "range", lcl_c_proc_new("List::range", c_lrange));
  lcl_ns_def_take(list_ns, "map", lcl_c_proc_new("List::map", c_list_map));
  lcl_ns_def_take(list_ns, "filter",
                  lcl_c_proc_new("List::filter", c_list_filter));
  lcl_ns_def_take(list_ns, "reduce",
                  lcl_c_proc_new("List::reduce", c_list_reduce));
  lcl_ns_def_take(list_ns, "sort", lcl_c_proc_new("List::sort", c_list_sort));
  lcl_ns_def_take(list_ns, "sort_by",
                  lcl_c_proc_new("List::sort_by", c_list_sort_by));
  lcl_ns_def_take(list_ns, "sort_with",
                  lcl_c_proc_new("List::sort_with", c_list_sort_with));
  lcl_ns_def_take(list_ns, "find", lcl_c_proc_new("List::find", c_list_find));
  lcl_ns_def_take(list_ns, "any?", lcl_c_proc_new("List::any?", c_list_any));
  lcl_ns_def_take(list_ns, "all?", lcl_c_proc_new("List::all?", c_list_all));
  lcl_ns_def_take(list_ns, "unique",
                  lcl_c_proc_new("List::unique", c_list_unique));
  lcl_ns_def_take(list_ns, "flatten",
                  lcl_c_proc_new("List::flatten", c_list_flatten));
}
