#ifndef LCL_STDLIB_INTERNAL_H
#define LCL_STDLIB_INTERNAL_H

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"

#include "lcl-stdlib.h"

lcl_result lcl_register_proc(lcl_interp *interp, const char *name,
                             lcl_c_proc_fn fn);
lcl_result lcl_register_spec(lcl_interp *interp, const char *name,
                             lcl_c_spec_fn fn);
lcl_result lcl_define(lcl_interp *interp, const char *name, lcl_value *value);
lcl_result lcl_define_take(lcl_interp *interp, const char *name,
                           lcl_value *value);
int lcl_is_callable(lcl_value *value);
lcl_return_code lcl_call_proc(lcl_interp *interp, lcl_value *proc, int argc,
                              lcl_value **argv, lcl_value **out);

int lcl_std_safe_add_long(long a, long b, long *out);
int lcl_std_safe_sub_long(long a, long b, long *out);
int lcl_std_safe_mul_long(long a, long b, long *out);
int lcl_std_safe_add_size(size_t a, size_t b, size_t *out);

int lcl_std_chk_argc(lcl_interp *interp, const char *name, int argc, int min,
                     int max);
lcl_return_code lcl_std_err_expected_got(lcl_interp *interp, const char *name,
                                         const char *expected, lcl_value *got);
lcl_return_code lcl_std_err_undefined(lcl_interp *interp, const char *name,
                                      const char *var);

int lcl_std_arg_int(lcl_interp *interp, const char *name, lcl_value *v,
                    long *out);
int lcl_std_arg_float(lcl_interp *interp, const char *name, lcl_value *v,
                      double *out);
int lcl_std_arg_str(lcl_interp *interp, const char *name, lcl_value *v,
                    const char **out);

int lcl_std_buf_append(char **buf, size_t *len, size_t *cap, const char *s,
                       size_t n);
int lcl_std_buf_append_char(char **buf, size_t *len, size_t *cap, char c);

lcl_return_code lcl_std_get_body_program(lcl_interp *interp, const lcl_word *w,
                                         const char *tag,
                                         lcl_program **prog_out, int *owned);
void lcl_std_free_if_owned(lcl_program *p, int owned);

lcl_frame *lcl_std_find_global_frame(lcl_frame *f);
int lcl_std_all_args_integral(int argc, lcl_value **argv);

int lcl_std_value_to_double(lcl_value *v, double *out);

int lcl_value_is_true(lcl_value *v);

#define EQ_STACK_MAX 256

struct eq_cycle_guard {
  lcl_value *a[EQ_STACK_MAX];
  lcl_value *b[EQ_STACK_MAX];
  int depth;
};

int lcl_value_equal_deep(lcl_value *a, lcl_value *b,
                         struct eq_cycle_guard *guard);

lcl_result lcl_std_dict_items(lcl_value *dict, lcl_value **out);

lcl_return_code lcl_std_ns_has(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out);

void lcl_std_register_num(lcl_interp *interp);
void lcl_std_register_control(lcl_interp *interp);
void lcl_std_register_binding(lcl_interp *interp);
void lcl_std_register_ns(lcl_interp *interp);
void lcl_std_register_module(lcl_interp *interp);
void lcl_std_register_eval(lcl_interp *interp);
void lcl_std_register_list(lcl_interp *interp);
void lcl_std_register_dict(lcl_interp *interp);
void lcl_std_register_string(lcl_interp *interp);
void lcl_std_register_lex(lcl_interp *interp);

#endif
