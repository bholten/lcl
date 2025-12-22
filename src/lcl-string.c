#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "lcl-values.h"

lcl_value *lcl_string_new(const char *str) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

  if (str) {
    size_t n = strlen(str);
    v->str_repr = (char *)malloc(n + 1);

    if (!v->str_repr) {
      free(v);
      return NULL;
    }

    memcpy(v->str_repr, str, n + 1);
  }

  v->refc = 1;
  v->type = LCL_STRING;

  return v;
}

static void lcl_reify_str_int(lcl_value *value) {
  char buf[32];
  /** TODO replace sprintf? **/
  int m = sprintf(buf, "%ld", value->as.i);
  value->str_repr = (char *)malloc((size_t)m + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, (size_t)m + 1);
}

static void lcl_reify_str_float(lcl_value *value) {
  char buf[32];
  int m = sprintf(buf, "%.17g", value->as.f);
  value->str_repr = (char *)malloc((size_t)m + 1);

  if (!value->str_repr) {
    return;
  }

  memcpy(value->str_repr, buf, (size_t)m + 1);
}

const char *lcl_value_to_string(lcl_value *value) {
  if (!value) return "";
  if (!value->str_repr) {
    switch (value->type) {      
    case LCL_INT:
      lcl_reify_str_int(value);
      break;
    case LCL_FLOAT:
      lcl_reify_str_float(value);
      break;
    case LCL_STRING:
      break;
    default:
      /** TODO list/dict pretty print later **/
      value->str_repr = (char *)malloc(4);

      if (!value->str_repr) return "";

      memcpy(value->str_repr, "<?>", 4);
      break;
    }
  }

  return value->str_repr ? value->str_repr : "";
}

lcl_value *lcl_value_new_string(const char *str) {
  lcl_value *value = (lcl_value *)calloc(1, sizeof(*value));

  if (!value) return NULL;

  if (str) {
    size_t n = strlen(str);
    value->str_repr = (char *)malloc(n + 1);

    if (!value->str_repr) {
      free(value);
      return NULL;
    }

    memcpy(value->str_repr, str, n + 1);
  }
  
  value->type = LCL_STRING;
  value->refc = 1;

  return value;  
}
