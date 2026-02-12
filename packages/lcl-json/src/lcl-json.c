#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include <lcl.h>

#define JSON_NS "json"

#define JSON_BOOL_TYPE "json_bool"
#define JSON_NULL_TYPE "json_null"

struct json_bool_data {
  int value;
};

static cJSON *lcl_to_cjson(lcl_value *value);
static lcl_value *cjson_to_lcl(cJSON *json);

static lcl_value *json_bool_new(int value) {
  struct json_bool_data *data = malloc(sizeof(struct json_bool_data));

  if (!data) {
    return NULL;
  }

  data->value = value ? 1 : 0;

  return lcl_opaque_new(data, JSON_BOOL_TYPE, free);
}

static lcl_value *json_null_new(void) {
  return lcl_opaque_new(NULL, JSON_NULL_TYPE, NULL);
}

static int json_bool_get(lcl_value *value, int *out) {
  struct json_bool_data *data;

  if (lcl_opaque_get(value, JSON_BOOL_TYPE, (void **)&data) != LCL_OK) {
    return 0;
  }

  if (out) {
    *out = data->value;
  }

  return 1;
}

static int json_is_null(lcl_value *value) {
  void *data;
  return lcl_opaque_get(value, JSON_NULL_TYPE, &data) == LCL_OK;
}

static cJSON *lcl_to_cjson(lcl_value *value) {
  lcl_type type;

  if (!value) {
    return cJSON_CreateNull();
  }

  type = lcl_value_type_of(value);

  switch (type) {
  case LCL_STRING: return cJSON_CreateString(lcl_value_to_string(value));

  case LCL_INT: {
    long n;
    if (lcl_value_to_int(value, &n) != LCL_OK) {
      return NULL;
    }
    return cJSON_CreateNumber((double)n);
  }

  case LCL_FLOAT: {
    double f;
    if (lcl_value_to_float(value, &f) != LCL_OK) {
      return NULL;
    }
    return cJSON_CreateNumber((double)f);
  }

  case LCL_LIST: {
    cJSON *arr = cJSON_CreateArray();
    size_t len = lcl_list_len(value);
    size_t i;

    if (!arr) {
      return NULL;
    }

    for (i = 0; i < len; i++) {
      lcl_value *elem = NULL;
      cJSON *json_elem;

      if (lcl_list_get(value, i, &elem) != LCL_OK) {
        cJSON_Delete(arr);
        return NULL;
      }

      json_elem = lcl_to_cjson(elem);
      lcl_ref_dec(elem);

      if (!json_elem) {
        cJSON_Delete(arr);
        return NULL;
      }

      cJSON_AddItemToArray(arr, json_elem);
    }

    return arr;
  }

  case LCL_DICT: {
    cJSON *obj = cJSON_CreateObject();
    lcl_value *keys = NULL;
    size_t len, i;

    if (!obj) {
      return NULL;
    }

    if (lcl_dict_keys(value, &keys) != LCL_OK) {
      cJSON_Delete(obj);
      return NULL;
    }

    len = lcl_list_len(keys);

    for (i = 0; i < len; i++) {
      lcl_value *key = NULL;
      lcl_value *val = NULL;
      cJSON *json_val;
      const char *key_str;

      if (lcl_list_get(keys, i, &key) != LCL_OK) {
        lcl_ref_dec(keys);
        cJSON_Delete(obj);
        return NULL;
      }

      key_str = lcl_value_to_string(key);

      if (lcl_dict_get(value, key_str, &val) != LCL_OK) {
        lcl_ref_dec(key);
        lcl_ref_dec(keys);
        cJSON_Delete(obj);
        return NULL;
      }

      json_val = lcl_to_cjson(val);
      lcl_ref_dec(val);

      if (!json_val) {
        lcl_ref_dec(key);
        lcl_ref_dec(keys);
        cJSON_Delete(obj);
        return NULL;
      }

      cJSON_AddItemToObject(obj, key_str, json_val);
      lcl_ref_dec(key);
    }

    lcl_ref_dec(keys);
    return obj;
  }

  case LCL_CELL: {
    lcl_value *contents = NULL;
    cJSON *result;

    if (lcl_cell_get(value, &contents) != LCL_OK) {
      return NULL;
    }

    result = lcl_to_cjson(contents);
    lcl_ref_dec(contents);
    return result;
  }

  case LCL_PROC:
  case LCL_CPROC:
  case LCL_NAMESPACE: return NULL;

  case LCL_OPAQUE: {
    int bool_val;
    if (json_bool_get(value, &bool_val)) {
      return cJSON_CreateBool(bool_val);
    }
    if (json_is_null(value)) {
      return cJSON_CreateNull();
    }

    return NULL;
  }
  }

  return NULL;
}

static lcl_value *cjson_to_lcl(cJSON *json) {
  if (!json) {
    return json_null_new();
  }

  if (cJSON_IsNull(json)) {
    return json_null_new();
  }

  if (cJSON_IsBool(json)) {
    return json_bool_new(cJSON_IsTrue(json));
  }

  if (cJSON_IsNumber(json)) {
    double val = cJSON_GetNumberValue(json);

    if (val == (double)(long)val && val >= (double)LONG_MIN &&
        val <= (double)LONG_MAX) {
      return lcl_int_new((long)val);
    }

    return lcl_float_new(val);
  }

  if (cJSON_IsString(json)) {
    return lcl_string_new(cJSON_GetStringValue(json));
  }

  if (cJSON_IsArray(json)) {
    lcl_value *list = lcl_list_new();
    cJSON *elem;

    if (!list) {
      return NULL;
    }

    cJSON_ArrayForEach(elem, json) {
      lcl_value *lcl_elem = cjson_to_lcl(elem);

      if (!lcl_elem) {
        lcl_ref_dec(list);
        return NULL;
      }

      if (lcl_list_push(&list, lcl_elem) != LCL_OK) {
        lcl_ref_dec(lcl_elem);
        lcl_ref_dec(list);
        return NULL;
      }

      lcl_ref_dec(lcl_elem);
    }

    return list;
  }

  if (cJSON_IsObject(json)) {
    lcl_value *dict = lcl_dict_new();
    cJSON *item;

    if (!dict) {
      return NULL;
    }

    cJSON_ArrayForEach(item, json) {
      lcl_value *lcl_val = cjson_to_lcl(item);

      if (!lcl_val) {
        lcl_ref_dec(dict);
        return NULL;
      }

      if (lcl_dict_put(&dict, item->string, lcl_val) != LCL_OK) {
        lcl_ref_dec(lcl_val);
        lcl_ref_dec(dict);
        return NULL;
      }

      lcl_ref_dec(lcl_val);
    }

    return dict;
  }

  return lcl_string_new("");
}

static int c_json_encode(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  cJSON *json;
  char *str;
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  json = lcl_to_cjson(argv[0]);

  if (!json) {
    return LCL_RC_ERR;
  }

  str = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  if (!str) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(str);
  free(str);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_json_decode(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  const char *json_str;
  cJSON *json;
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  json_str = lcl_value_to_string(argv[0]);
  json = cJSON_Parse(json_str);

  if (!json) {
    return LCL_RC_ERR;
  }

  *out = cjson_to_lcl(json);
  cJSON_Delete(json);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_json_true(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;
  (void)argv;

  if (argc != 0) {
    return LCL_RC_ERR;
  }

  *out = json_bool_new(1);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_json_false(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  (void)interp;
  (void)argv;

  if (argc != 0) {
    return LCL_RC_ERR;
  }

  *out = json_bool_new(0);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_json_null(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;
  (void)argv;

  if (argc != 0) {
    return LCL_RC_ERR;
  }

  *out = json_null_new();
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_json_is_bool(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(json_bool_get(argv[0], NULL) ? 1 : 0);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_json_is_null(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(json_is_null(argv[0]) ? 1 : 0);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

void lcl_register_json(lcl_interp *interp) {
  lcl_value *json_ns = lcl_ns_new(JSON_NS);
  lcl_define_take(interp, JSON_NS, json_ns);

  lcl_ns_def(json_ns, "encode", lcl_c_proc_new("json::encode", c_json_encode));
  lcl_ns_def(json_ns, "decode", lcl_c_proc_new("json::decode", c_json_decode));
  lcl_ns_def(json_ns, "true", lcl_c_proc_new("json::true", c_json_true));
  lcl_ns_def(json_ns, "false", lcl_c_proc_new("json::false", c_json_false));
  lcl_ns_def(json_ns, "null", lcl_c_proc_new("json::null", c_json_null));
  lcl_ns_def(json_ns, "bool?", lcl_c_proc_new("json::bool?", c_json_is_bool));
  lcl_ns_def(json_ns, "null?", lcl_c_proc_new("json::null?", c_json_is_null));
}
