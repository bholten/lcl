#include <math.h>

#include <lcl.h>

#define MATH_NS "math"

#define MATH_SINGLE_ARG_FN(fn_name, fn)                                        \
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
    double value;                                                              \
    (void)interp;                                                              \
    if (argc < 1)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_value_to_float(argv[0], &value) != LCL_OK)                         \
      return LCL_RC_ERR;                                                       \
    *out = lcl_float_new(fn(value));                                           \
    return LCL_RC_OK;                                                          \
  }

#define MATH_TWO_ARG_FN(fn_name, fn)                                           \
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
    double a;                                                                  \
    double b;                                                                  \
    (void)interp;                                                              \
    if (argc < 2)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_value_to_float(argv[0], &a) != LCL_OK)                             \
      return LCL_RC_ERR;                                                       \
    if (lcl_value_to_float(argv[1], &b) != LCL_OK)                             \
      return LCL_RC_ERR;                                                       \
    *out = lcl_float_new(fn(a, b));                                            \
    return LCL_RC_OK;                                                          \
  }

/* Trigonometric functions */
MATH_SINGLE_ARG_FN(c_sin, sin)
MATH_SINGLE_ARG_FN(c_cos, cos)
MATH_SINGLE_ARG_FN(c_tan, tan)
MATH_SINGLE_ARG_FN(c_asin, asin)
MATH_SINGLE_ARG_FN(c_acos, acos)
MATH_SINGLE_ARG_FN(c_atan, atan)
MATH_TWO_ARG_FN(c_atan2, atan2)

/* Hyperbolic functions */
MATH_SINGLE_ARG_FN(c_sinh, sinh)
MATH_SINGLE_ARG_FN(c_cosh, cosh)
MATH_SINGLE_ARG_FN(c_tanh, tanh)
MATH_SINGLE_ARG_FN(c_asinh, asinh)
MATH_SINGLE_ARG_FN(c_acosh, acosh)
MATH_SINGLE_ARG_FN(c_atanh, atanh)

/* Exponential and logarithmic functions */
MATH_SINGLE_ARG_FN(c_exp, exp)
MATH_SINGLE_ARG_FN(c_exp2, exp2)
MATH_SINGLE_ARG_FN(c_log, log)
MATH_SINGLE_ARG_FN(c_log2, log2)
MATH_SINGLE_ARG_FN(c_log10, log10)

/* Power functions */
MATH_TWO_ARG_FN(c_pow, pow)
MATH_SINGLE_ARG_FN(c_sqrt, sqrt)
MATH_SINGLE_ARG_FN(c_cbrt, cbrt)
MATH_TWO_ARG_FN(c_hypot, hypot)

/* Rounding and remainder functions */
MATH_SINGLE_ARG_FN(c_ceil, ceil)
MATH_SINGLE_ARG_FN(c_floor, floor)
MATH_SINGLE_ARG_FN(c_trunc, trunc)
MATH_SINGLE_ARG_FN(c_round, round)
MATH_TWO_ARG_FN(c_fmod, fmod)

/* Absolute value */
MATH_SINGLE_ARG_FN(c_fabs, fabs)

/* Error and gamma functions */
MATH_SINGLE_ARG_FN(c_erf, erf)
MATH_SINGLE_ARG_FN(c_erfc, erfc)
MATH_SINGLE_ARG_FN(c_tgamma, tgamma)
MATH_SINGLE_ARG_FN(c_lgamma, lgamma)

/* math::pi -> constant pi */
static int c_pi(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  *out = lcl_float_new(3.14159265358979323846);
  return LCL_RC_OK;
}

/* math::e -> constant e */
static int c_e(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  *out = lcl_float_new(2.71828182845904523536);
  return LCL_RC_OK;
}

/* math::isnan x -> 1 if x is NaN, 0 otherwise */
static int c_isnan(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  double value;
  (void)interp;
  if (argc < 1) {
    return LCL_RC_ERR;
  }
  if (lcl_value_to_float(argv[0], &value) != LCL_OK) {
    return LCL_RC_ERR;
  }
  *out = lcl_int_new(isnan(value) ? 1 : 0);
  return LCL_RC_OK;
}

/* math::isinf x -> 1 if x is infinite, 0 otherwise */
static int c_isinf(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  double value;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &value) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(isinf(value) ? 1 : 0);

  return LCL_RC_OK;
}

/* math::min a b -> minimum of a and b */
static int c_min(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  double a;
  double b;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &a) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &b) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_float_new(a < b ? a : b);

  return LCL_RC_OK;
}

/* math::max a b -> maximum of a and b */
static int c_max(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  double a;
  double b;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[0], &a) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_float(argv[1], &b) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_float_new(a > b ? a : b);

  return LCL_RC_OK;
}

void lcl_register_math(lcl_interp *interp) {
  lcl_value *math_ns = lcl_ns_new(MATH_NS);
  lcl_define_take(interp, MATH_NS, math_ns);

  /* Constants */
  lcl_ns_def(math_ns, "pi", lcl_c_proc_new("math::pi", c_pi));
  lcl_ns_def(math_ns, "e", lcl_c_proc_new("math::e", c_e));

  /* Trigonometric functions */
  lcl_ns_def(math_ns, "sin", lcl_c_proc_new("math::sin", c_sin));
  lcl_ns_def(math_ns, "cos", lcl_c_proc_new("math::cos", c_cos));
  lcl_ns_def(math_ns, "tan", lcl_c_proc_new("math::tan", c_tan));
  lcl_ns_def(math_ns, "asin", lcl_c_proc_new("math::asin", c_asin));
  lcl_ns_def(math_ns, "acos", lcl_c_proc_new("math::acos", c_acos));
  lcl_ns_def(math_ns, "atan", lcl_c_proc_new("math::atan", c_atan));
  lcl_ns_def(math_ns, "atan2", lcl_c_proc_new("math::atan2", c_atan2));

  /* Hyperbolic functions */
  lcl_ns_def(math_ns, "sinh", lcl_c_proc_new("math::sinh", c_sinh));
  lcl_ns_def(math_ns, "cosh", lcl_c_proc_new("math::cosh", c_cosh));
  lcl_ns_def(math_ns, "tanh", lcl_c_proc_new("math::tanh", c_tanh));
  lcl_ns_def(math_ns, "asinh", lcl_c_proc_new("math::asinh", c_asinh));
  lcl_ns_def(math_ns, "acosh", lcl_c_proc_new("math::acosh", c_acosh));
  lcl_ns_def(math_ns, "atanh", lcl_c_proc_new("math::atanh", c_atanh));

  /* Exponential and logarithmic functions */
  lcl_ns_def(math_ns, "exp", lcl_c_proc_new("math::exp", c_exp));
  lcl_ns_def(math_ns, "exp2", lcl_c_proc_new("math::exp2", c_exp2));
  lcl_ns_def(math_ns, "log", lcl_c_proc_new("math::log", c_log));
  lcl_ns_def(math_ns, "log2", lcl_c_proc_new("math::log2", c_log2));
  lcl_ns_def(math_ns, "log10", lcl_c_proc_new("math::log10", c_log10));

  /* Power functions */
  lcl_ns_def(math_ns, "pow", lcl_c_proc_new("math::pow", c_pow));
  lcl_ns_def(math_ns, "sqrt", lcl_c_proc_new("math::sqrt", c_sqrt));
  lcl_ns_def(math_ns, "cbrt", lcl_c_proc_new("math::cbrt", c_cbrt));
  lcl_ns_def(math_ns, "hypot", lcl_c_proc_new("math::hypot", c_hypot));

  /* Rounding and remainder functions */
  lcl_ns_def(math_ns, "ceil", lcl_c_proc_new("math::ceil", c_ceil));
  lcl_ns_def(math_ns, "floor", lcl_c_proc_new("math::floor", c_floor));
  lcl_ns_def(math_ns, "trunc", lcl_c_proc_new("math::trunc", c_trunc));
  lcl_ns_def(math_ns, "round", lcl_c_proc_new("math::round", c_round));
  lcl_ns_def(math_ns, "fmod", lcl_c_proc_new("math::fmod", c_fmod));

  /* Absolute value */
  lcl_ns_def(math_ns, "abs", lcl_c_proc_new("math::abs", c_fabs));

  /* Error and gamma functions */
  lcl_ns_def(math_ns, "erf", lcl_c_proc_new("math::erf", c_erf));
  lcl_ns_def(math_ns, "erfc", lcl_c_proc_new("math::erfc", c_erfc));
  lcl_ns_def(math_ns, "tgamma", lcl_c_proc_new("math::tgamma", c_tgamma));
  lcl_ns_def(math_ns, "lgamma", lcl_c_proc_new("math::lgamma", c_lgamma));

  /* Classification functions */
  lcl_ns_def(math_ns, "isnan", lcl_c_proc_new("math::isnan", c_isnan));
  lcl_ns_def(math_ns, "isinf", lcl_c_proc_new("math::isinf", c_isinf));

  /* Comparison functions */
  lcl_ns_def(math_ns, "min", lcl_c_proc_new("math::min", c_min));
  lcl_ns_def(math_ns, "max", lcl_c_proc_new("math::max", c_max));
}
