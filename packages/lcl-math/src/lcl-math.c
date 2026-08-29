#define _XOPEN_SOURCE 600

#include <math.h>

#include <lcl.h>

#define MATH_NS "Math"

#define MATH_SINGLE_ARG_FN(fn_name, fn)                                        \
  static lcl_return_code fn_name(lcl_interp *interp, int argc,                 \
                                 lcl_value **argv, lcl_value **out) {          \
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
  static lcl_return_code fn_name(lcl_interp *interp, int argc,                 \
                                 lcl_value **argv, lcl_value **out) {          \
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

/* Math::pi -> constant pi */
static lcl_return_code c_pi(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  *out = lcl_float_new(3.14159265358979323846);
  return LCL_RC_OK;
}

/* Math::e -> constant e */
static lcl_return_code c_e(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  *out = lcl_float_new(2.71828182845904523536);
  return LCL_RC_OK;
}

/* Math::isnan x -> 1 if x is NaN, 0 otherwise */
static lcl_return_code c_isnan(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Math::isinf x -> 1 if x is infinite, 0 otherwise */
static lcl_return_code c_isinf(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Math::min a b -> minimum of a and b */
static lcl_return_code c_min(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Math::max a b -> maximum of a and b */
static lcl_return_code c_max(lcl_interp *interp, int argc, lcl_value **argv,
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
  lcl_ns_def_take(math_ns, "pi", lcl_c_proc_new("Math::pi", c_pi));
  lcl_ns_def_take(math_ns, "e", lcl_c_proc_new("Math::e", c_e));

  /* Trigonometric functions */
  lcl_ns_def_take(math_ns, "sin", lcl_c_proc_new("Math::sin", c_sin));
  lcl_ns_def_take(math_ns, "cos", lcl_c_proc_new("Math::cos", c_cos));
  lcl_ns_def_take(math_ns, "tan", lcl_c_proc_new("Math::tan", c_tan));
  lcl_ns_def_take(math_ns, "asin", lcl_c_proc_new("Math::asin", c_asin));
  lcl_ns_def_take(math_ns, "acos", lcl_c_proc_new("Math::acos", c_acos));
  lcl_ns_def_take(math_ns, "atan", lcl_c_proc_new("Math::atan", c_atan));
  lcl_ns_def_take(math_ns, "atan2", lcl_c_proc_new("Math::atan2", c_atan2));

  /* Hyperbolic functions */
  lcl_ns_def_take(math_ns, "sinh", lcl_c_proc_new("Math::sinh", c_sinh));
  lcl_ns_def_take(math_ns, "cosh", lcl_c_proc_new("Math::cosh", c_cosh));
  lcl_ns_def_take(math_ns, "tanh", lcl_c_proc_new("Math::tanh", c_tanh));
  lcl_ns_def_take(math_ns, "asinh", lcl_c_proc_new("Math::asinh", c_asinh));
  lcl_ns_def_take(math_ns, "acosh", lcl_c_proc_new("Math::acosh", c_acosh));
  lcl_ns_def_take(math_ns, "atanh", lcl_c_proc_new("Math::atanh", c_atanh));

  /* Exponential and logarithmic functions */
  lcl_ns_def_take(math_ns, "exp", lcl_c_proc_new("Math::exp", c_exp));
  lcl_ns_def_take(math_ns, "exp2", lcl_c_proc_new("Math::exp2", c_exp2));
  lcl_ns_def_take(math_ns, "log", lcl_c_proc_new("Math::log", c_log));
  lcl_ns_def_take(math_ns, "log2", lcl_c_proc_new("Math::log2", c_log2));
  lcl_ns_def_take(math_ns, "log10", lcl_c_proc_new("Math::log10", c_log10));

  /* Power functions */
  lcl_ns_def_take(math_ns, "pow", lcl_c_proc_new("Math::pow", c_pow));
  lcl_ns_def_take(math_ns, "sqrt", lcl_c_proc_new("Math::sqrt", c_sqrt));
  lcl_ns_def_take(math_ns, "cbrt", lcl_c_proc_new("Math::cbrt", c_cbrt));
  lcl_ns_def_take(math_ns, "hypot", lcl_c_proc_new("Math::hypot", c_hypot));

  /* Rounding and remainder functions */
  lcl_ns_def_take(math_ns, "ceil", lcl_c_proc_new("Math::ceil", c_ceil));
  lcl_ns_def_take(math_ns, "floor", lcl_c_proc_new("Math::floor", c_floor));
  lcl_ns_def_take(math_ns, "trunc", lcl_c_proc_new("Math::trunc", c_trunc));
  lcl_ns_def_take(math_ns, "round", lcl_c_proc_new("Math::round", c_round));
  lcl_ns_def_take(math_ns, "fmod", lcl_c_proc_new("Math::fmod", c_fmod));

  /* Absolute value */
  lcl_ns_def_take(math_ns, "abs", lcl_c_proc_new("Math::abs", c_fabs));

  /* Error and gamma functions */
  lcl_ns_def_take(math_ns, "erf", lcl_c_proc_new("Math::erf", c_erf));
  lcl_ns_def_take(math_ns, "erfc", lcl_c_proc_new("Math::erfc", c_erfc));
  lcl_ns_def_take(math_ns, "tgamma", lcl_c_proc_new("Math::tgamma", c_tgamma));
  lcl_ns_def_take(math_ns, "lgamma", lcl_c_proc_new("Math::lgamma", c_lgamma));

  /* Classification functions */
  lcl_ns_def_take(math_ns, "isnan", lcl_c_proc_new("Math::isnan", c_isnan));
  lcl_ns_def_take(math_ns, "isinf", lcl_c_proc_new("Math::isinf", c_isinf));

  /* Comparison functions */
  lcl_ns_def_take(math_ns, "min", lcl_c_proc_new("Math::min", c_min));
  lcl_ns_def_take(math_ns, "max", lcl_c_proc_new("Math::max", c_max));
}
