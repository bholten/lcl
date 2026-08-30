#include "lcl-stdlib-internal.h"

static lcl_return_code c_add(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  int i;
  (void)interp;

  if (lcl_std_all_args_integral(argc, argv)) {
    lcl_int sum = 0;

    for (i = 0; i < argc; i++) {
      lcl_int v;
      lcl_value_to_int(argv[i], &v);

      if (!lcl_std_safe_add_int(sum, v, &sum)) {
        LCL_ERR_MSG(interp, "integer overflow in +");

        return LCL_RC_ERR;
      }
    }

    *out = lcl_int_new(sum);
  } else {
    double sum = 0.0;

    for (i = 0; i < argc; i++) {
      double v;

      if (!lcl_std_arg_float(interp, "+", argv[i], &v)) {
        return LCL_RC_ERR;
      }

      sum += v;
    }

    *out = lcl_float_new(sum);
  }

  return LCL_RC_OK;
}

static lcl_return_code c_sub(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  int i;

  if (!lcl_std_chk_argc(interp, "-", argc, 2, -1)) {
    return LCL_RC_ERR;
  }

  if (lcl_std_all_args_integral(argc, argv)) {
    lcl_int result;
    lcl_value_to_int(argv[0], &result);

    for (i = 1; i < argc; i++) {
      lcl_int v;
      lcl_value_to_int(argv[i], &v);

      if (!lcl_std_safe_sub_int(result, v, &result)) {
        LCL_ERR_MSG(interp, "integer overflow in -");

        return LCL_RC_ERR;
      }
    }

    *out = lcl_int_new(result);
  } else {
    double result;

    if (!lcl_std_arg_float(interp, "-", argv[0], &result)) {
      return LCL_RC_ERR;
    }

    for (i = 1; i < argc; i++) {
      double v;

      if (!lcl_std_arg_float(interp, "-", argv[i], &v)) {
        return LCL_RC_ERR;
      }

      result -= v;
    }

    *out = lcl_float_new(result);
  }

  return LCL_RC_OK;
}

static lcl_return_code c_mult(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  int i;
  (void)interp;

  if (lcl_std_all_args_integral(argc, argv)) {
    lcl_int product = 1;

    for (i = 0; i < argc; i++) {
      lcl_int v;
      lcl_value_to_int(argv[i], &v);

      if (!lcl_std_safe_mul_int(product, v, &product)) {
        LCL_ERR_MSG(interp, "integer overflow in *");
        return LCL_RC_ERR;
      }
    }

    *out = lcl_int_new(product);
  } else {
    double product = 1.0;

    for (i = 0; i < argc; i++) {
      double v;

      if (!lcl_std_arg_float(interp, "*", argv[i], &v)) {
        return LCL_RC_ERR;
      }

      product *= v;
    }

    *out = lcl_float_new(product);
  }

  return LCL_RC_OK;
}

static lcl_return_code c_div(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  double result;
  double numerator;
  double divisor;

  if (!lcl_std_chk_argc(interp, "/", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_float(interp, "/", argv[0], &numerator)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_float(interp, "/", argv[1], &divisor)) {
    return LCL_RC_ERR;
  }

  if (divisor == 0.0) {
    LCL_ERR_MSG(interp, "division by zero");
    return LCL_RC_ERR;
  }

  result = numerator / divisor;

  *out = lcl_float_new(result);

  return LCL_RC_OK;
}

static lcl_return_code c_mod(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  lcl_int result;
  lcl_int dividend;
  lcl_int divisor;

  if (!lcl_std_chk_argc(interp, "%", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_int(interp, "%", argv[0], &dividend)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_int(interp, "%", argv[1], &divisor)) {
    return LCL_RC_ERR;
  }

  if (divisor == 0) {
    LCL_ERR_MSG(interp, "division by zero");
    return LCL_RC_ERR;
  }

  if (dividend == LCL_INT_MIN && divisor == -1) {
    *out = lcl_int_new(0);
    return LCL_RC_OK;
  }

  result = dividend % divisor;

  *out = lcl_int_new(result);

  return LCL_RC_OK;
}

static lcl_return_code c_compare(lcl_interp *interp, const char *name, int argc,
                                 lcl_value **argv, int op, lcl_value **out) {
  double left;
  double right;
  lcl_int result;

  if (!lcl_std_chk_argc(interp, name, argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_std_all_args_integral(argc, argv)) {
    lcl_int li;
    lcl_int ri;

    if (!lcl_std_arg_int(interp, name, argv[0], &li) ||
        !lcl_std_arg_int(interp, name, argv[1], &ri)) {
      return LCL_RC_ERR;
    }

    switch (op) {
    case 0: result = (li < ri); break;
    case 1: result = (li <= ri); break;
    case 2: result = (li > ri); break;
    case 3: result = (li >= ri); break;
    default:
      LCL_ERR_MSG(interp, "internal error: bad comparison op");
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(result);
    return LCL_RC_OK;
  }

  if (!lcl_std_arg_float(interp, name, argv[0], &left) ||
      !lcl_std_arg_float(interp, name, argv[1], &right)) {
    return LCL_RC_ERR;
  }

  switch (op) {
  case 0: result = (left < right); break;
  case 1: result = (left <= right); break;
  case 2: result = (left > right); break;
  case 3: result = (left >= right); break;
  default:
    LCL_ERR_MSG(interp, "internal error: bad comparison op");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(result);
  return LCL_RC_OK;
}

static lcl_return_code c_lt(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  return c_compare(interp, "<", argc, argv, 0, out);
}

static lcl_return_code c_lte(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  return c_compare(interp, "<=", argc, argv, 1, out);
}

static lcl_return_code c_gt(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  return c_compare(interp, ">", argc, argv, 2, out);
}

static lcl_return_code c_gte(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  return c_compare(interp, ">=", argc, argv, 3, out);
}

static lcl_return_code c_is_number(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "number?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_INT || argv[0]->type == LCL_FLOAT) {
    *out = lcl_int_new(1);
  } else if (argv[0]->type == LCL_STRING) {
    const char *s = lcl_value_to_string(argv[0]);

    *out = lcl_int_new(
        s && lcl_num_text_classify(s, strlen(s)) != LCL_NUM_NONE ? 1 : 0);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

static lcl_return_code c_is_int(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "int?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_INT ? 1 : 0);

  return LCL_RC_OK;
}

static lcl_return_code c_is_float(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "float?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_FLOAT ? 1 : 0);

  return LCL_RC_OK;
}

/* int x - convert value to integer */
static lcl_return_code c_to_int(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lcl_int val;
  double fval;

  if (!lcl_std_chk_argc(interp, "int", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type == LCL_INT) {
    *out = lcl_ref_inc(argv[0]);
    return LCL_RC_OK;
  }

  if (lcl_value_to_int(argv[0], &val) == LCL_OK) {
    *out = lcl_int_new(val);
    return LCL_RC_OK;
  }

  /* String fallback: parse as float, then range-check before casting. */
  if (lcl_value_to_float(argv[0], &fval) == LCL_OK) {
    if (lcl_double_to_int(fval, &val) != LCL_OK) {
      LCL_ERR_MSG(interp, "int: value out of range");
      return LCL_RC_ERR;
    }

    *out = lcl_int_new(val);
    return LCL_RC_OK;
  }

  return lcl_std_err_expected_got(interp, "int", "number", argv[0]);
}

/* float x - convert value to float */
static lcl_return_code c_to_float(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  double val;

  if (!lcl_std_chk_argc(interp, "float", argc, 1, 1)) {
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
    return lcl_std_err_expected_got(interp, "float", "number", argv[0]);
  }

  *out = lcl_float_new((double)val);
  return LCL_RC_OK;
}

void lcl_std_register_num(lcl_interp *interp) {
  lcl_register_proc(interp, "+", c_add);
  lcl_register_proc(interp, "-", c_sub);
  lcl_register_proc(interp, "*", c_mult);
  lcl_register_proc(interp, "/", c_div);
  lcl_register_proc(interp, "%", c_mod);
  lcl_register_proc(interp, "<", c_lt);
  lcl_register_proc(interp, "<=", c_lte);
  lcl_register_proc(interp, ">", c_gt);
  lcl_register_proc(interp, ">=", c_gte);
  lcl_register_proc(interp, "number?", c_is_number);
  lcl_register_proc(interp, "int?", c_is_int);
  lcl_register_proc(interp, "float?", c_is_float);
  lcl_register_proc(interp, "int", c_to_int);
  lcl_register_proc(interp, "float", c_to_float);
}
