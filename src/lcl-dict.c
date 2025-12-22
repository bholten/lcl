#include "lcl-values.h"

lcl_value *lcl_dict_new(void) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

  v->type = LCL_DICT;
  v->refc = 1;
  v->as.dict.dictionary = hash_table_new();

  if (!v->as.dict.dictionary) {
    free(v);
    return NULL;
  }

  return v;
}

size_t lcl_dict_len(const lcl_value *dict) {
  if (dict->type != LCL_DICT) return 0;

  return dict->as.dict.dictionary->len;
}

lcl_result lcl_dict_get(const lcl_value *dict, const char *key,
                        lcl_value **out) {
  if (dict->type != LCL_DICT) return LCL_ERROR;

  if (!hash_table_get(dict->as.dict.dictionary, key, out)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

static lcl_value *lcl_dict_clone_shallow(lcl_value *dict) {
  hash_iter it = {0};
  const char *k;
  lcl_value *value;
  lcl_value *new_dict = lcl_dict_new();

  if (dict->type != LCL_DICT) return NULL;
  if (!new_dict) return NULL;

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &k, &value)) {
    hash_table_put(new_dict->as.dict.dictionary, k, value);
    lcl_ref_dec(value);
  }

  return new_dict;
}

lcl_result lcl_dict_put(lcl_value **dict_io, const char *key,
                        lcl_value *value) {
  lcl_value *dict = *dict_io;

  if (dict->type != LCL_DICT) return LCL_ERROR;

  if (dict->refc > 1) {
    lcl_value *new_dict = lcl_dict_clone_shallow(dict);
    lcl_ref_dec(dict);
    *dict_io = dict = new_dict;
  }

  free(dict->str_repr);
  dict->str_repr = NULL;

  if (!hash_table_put(dict->as.dict.dictionary, key, value)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

lcl_result lcl_dict_del(lcl_value **dict_io, const char *key) {
  lcl_value *dict = *dict_io;

  if (dict->type != LCL_DICT) return LCL_ERROR;

  if (dict->refc > 1) {
    lcl_value *new_dict = lcl_dict_clone_shallow(dict);
    lcl_ref_dec(dict);
    *dict_io = dict = new_dict;
  }

  free(dict->str_repr);
  dict->str_repr = NULL;


  if (!hash_table_delete(dict->as.dict.dictionary, key)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

lcl_result lcl_dict_iter(const lcl_value **dict_io, lcl_dict_it *it, const char **key,
                  lcl_value **value) {
  const lcl_value *dict = *dict_io;
  hash_iter hit;

  if (dict->type != LCL_DICT) return LCL_ERROR;

  hit.i = it->i;

  return hash_table_iterate(dict->as.dict.dictionary, &hit, key, value);
}
