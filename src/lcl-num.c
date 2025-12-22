#include <stdio.h>
#include <stdlib.h>

#include "lcl-values.h"

lcl_value *lcl_int_new(const long n) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

  v->type = LCL_INT;
  v->refc = 1;
  v->as.i = n;

  lcl_value_to_string(v);

  return v;
}

lcl_value *lcl_float_new(const float f) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

  v->type = LCL_FLOAT;
  v->refc = 1;
  v->as.f = f;

  lcl_value_to_string(v);  

  return v;  
}

lcl_result lcl_value_to_int(lcl_value *value, long *out) {
  switch (value->type) {
  case LCL_INT:
    *out = value->as.i;
    return LCL_OK;

  case LCL_FLOAT:
    *out = (long)value->as.f;
    return LCL_OK;

  case LCL_STRING: {
    char *endptr;
    const char *str = lcl_value_to_string(value);
    long val = strtol(str, &endptr, 10);
    if (endptr != str && *endptr == '\0') {
      *out = val;
      return LCL_OK;
    }
    break;
  }

  default:
    break;
  }

  return LCL_ERROR;
}

lcl_result lcl_value_to_float(lcl_value *value, float *out) {
  switch (value->type) {
  case LCL_INT:
    *out = (float)value->as.i;
    return LCL_OK;

  case LCL_FLOAT:
    *out = value->as.f;
    return LCL_OK;

  case LCL_STRING: {
    const char *str = lcl_value_to_string(value);
    float val;

    if (sscanf(str, "%f", &val) == 1) {
      *out = val;
      return LCL_OK;
    }
    break;
  }

  default:
    break;
  }

  return LCL_ERROR;
}
