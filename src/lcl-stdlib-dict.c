#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "lcl-stdlib-internal.h"

static lcl_return_code c_is_dict(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "dict?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_DICT ? 1 : 0);

  return LCL_RC_OK;
}

/* dict::keys d - return list of keys */
static lcl_return_code c_dict_keys(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "Dict::keys", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return lcl_std_err_expected_got(interp, "Dict::keys", "dict", argv[0]);
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
static lcl_return_code c_dict_values(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "Dict::values", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return lcl_std_err_expected_got(interp, "Dict::values", "dict", argv[0]);
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
static lcl_return_code c_dict_items(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "Dict::items", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_DICT) {
    return lcl_std_err_expected_got(interp, "Dict::items", "dict", argv[0]);
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
static lcl_return_code c_dict_create_proc(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
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
static lcl_return_code c_dict_merge(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "Dict::merge", argc, 2, 2)) {
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
static lcl_return_code c_dict_map(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *dict;
  lcl_value *result;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "Dict::map", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  dict = argv[0];
  func = argv[1];

  if (dict->type != LCL_DICT) {
    return lcl_std_err_expected_got(interp, "Dict::map", "dict", dict);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "Dict::map", "callable", func);
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
static lcl_return_code c_dict_filter(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *func;
  lcl_value *dict;
  lcl_value *result;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "Dict::filter", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  dict = argv[0];
  func = argv[1];

  if (dict->type != LCL_DICT) {
    return lcl_std_err_expected_got(interp, "Dict::filter", "dict", dict);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "Dict::filter", "callable", func);
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
static lcl_return_code c_dict_reduce(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *init;
  lcl_value *func;
  lcl_value *dict;
  lcl_value *acc;
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "Dict::reduce", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  dict = argv[0];
  init = argv[1];
  func = argv[2];

  if (dict->type != LCL_DICT) {
    return lcl_std_err_expected_got(interp, "Dict::reduce", "dict", dict);
  }

  if (!lcl_is_callable(func)) {
    return lcl_std_err_expected_got(interp, "Dict::reduce", "callable", func);
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

void lcl_std_register_dict(lcl_interp *interp) {
  lcl_value *dict_ns;

  lcl_register_proc(interp, "dict?", c_is_dict);
  lcl_register_proc(interp, "dict", c_dict_create_proc);
  dict_ns = lcl_ns_new("Dict");
  lcl_define_take(interp, "Dict", dict_ns);
  lcl_ns_def_take(dict_ns, "new",
                  lcl_c_proc_new("Dict::new", c_dict_create_proc));
  lcl_ns_def_take(dict_ns, "keys", lcl_c_proc_new("Dict::keys", c_dict_keys));
  lcl_ns_def_take(dict_ns, "values",
                  lcl_c_proc_new("Dict::values", c_dict_values));
  lcl_ns_def_take(dict_ns, "items",
                  lcl_c_proc_new("Dict::items", c_dict_items));
  lcl_ns_def_take(dict_ns, "merge",
                  lcl_c_proc_new("Dict::merge", c_dict_merge));
  lcl_ns_def_take(dict_ns, "map", lcl_c_proc_new("Dict::map", c_dict_map));
  lcl_ns_def_take(dict_ns, "filter",
                  lcl_c_proc_new("Dict::filter", c_dict_filter));
  lcl_ns_def_take(dict_ns, "reduce",
                  lcl_c_proc_new("Dict::reduce", c_dict_reduce));
}
