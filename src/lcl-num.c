#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "lcl-values.h"

lcl_value *lcl_int_new(const long n) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    return NULL;
  }

  v->type = LCL_INT;
  v->refc = 1;
  v->as.i = n;

  lcl_value_to_string(v);

  return v;
}

lcl_value *lcl_float_new(const double f) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    return NULL;
  }

  v->type = LCL_FLOAT;
  v->refc = 1;
  v->as.f = f;

  lcl_value_to_string(v);

  return v;
}

/* Bugfix: Convert a double to long, rejecting NaN, Inf, and
 * out-of-range inputs.
 *
 * The classic safe range is [LONG_MIN, -(double)LONG_MIN): LONG_MIN
 * is exactly representable as a double, and -(double)LONG_MIN equals
 * 2^N (one past LONG_MAX) and is also exactly representable.
 *
 * Casting (long)f is defined for any finite f in this half-open
 * range. */
lcl_result lcl_double_to_long(double f, long *out) {
  if (isnan(f) || isinf(f)) {
    return LCL_ERROR;
  }

  if (f < (double)LONG_MIN || f >= -(double)LONG_MIN) {
    return LCL_ERROR;
  }

  *out = (long)f;

  return LCL_OK;
}

lcl_result lcl_value_to_int(lcl_value *value, long *out) {
  if (!value || !out) {
    return LCL_ERROR;
  }

  switch (value->type) {
  case LCL_INT: *out = value->as.i; return LCL_OK;

  case LCL_FLOAT: return lcl_double_to_long(value->as.f, out);

  case LCL_STRING: {
    char *endptr;
    const char *str = lcl_value_to_string(value);
    long val;

    errno = 0;
    val = strtol(str, &endptr, 10);

    if (endptr != str && *endptr == '\0' && errno != ERANGE) {
      *out = val;

      return LCL_OK;
    }

    break;
  }

  default: break;
  }

  return LCL_ERROR;
}

lcl_result lcl_value_to_float(lcl_value *value, double *out) {
  if (!value || !out) {
    return LCL_ERROR;
  }

  switch (value->type) {
  case LCL_INT: *out = (double)value->as.i; return LCL_OK;

  case LCL_FLOAT: *out = value->as.f; return LCL_OK;

  case LCL_STRING: {
    const char *str = lcl_value_to_string(value);
    double val;

    if (sscanf(str, "%lf", &val) == 1) {
      *out = val;

      return LCL_OK;
    }

    break;
  }

  default: break;
  }

  return LCL_ERROR;
}
