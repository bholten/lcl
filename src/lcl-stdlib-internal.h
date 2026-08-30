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

int lcl_std_safe_add_int(lcl_int a, lcl_int b, lcl_int *out);
int lcl_std_safe_sub_int(lcl_int a, lcl_int b, lcl_int *out);
int lcl_std_safe_mul_int(lcl_int a, lcl_int b, lcl_int *out);
int lcl_std_safe_add_size(size_t a, size_t b, size_t *out);

int lcl_std_chk_argc(lcl_interp *interp, const char *name, int argc, int min,
                     int max);
lcl_return_code lcl_std_err_expected_got(lcl_interp *interp, const char *name,
                                         const char *expected, lcl_value *got);
lcl_return_code lcl_std_err_undefined(lcl_interp *interp, const char *name,
                                      const char *var);

int lcl_std_arg_int(lcl_interp *interp, const char *name, lcl_value *v,
                    lcl_int *out);

/* The lcl_int <-> size_t boundary. An Lcl integer is 64-bit on every
 * host; a container index or count is whatever size_t is (32 bits on
 * wasm32), so the conversion is checked, never a cast.
 * lcl_std_index: 1 when 0 <= idx < len, writing the index to *out
 * (NULL allowed). lcl_std_int_to_size: 1 when n is a non-negative
 * value the host can hold. lcl_std_err_index records the standard
 * "<cmd>: index N out of range" error. */
int lcl_std_index(lcl_int idx, size_t len, size_t *out);
int lcl_std_int_to_size(lcl_int n, size_t *out);
void lcl_std_err_index(lcl_interp *interp, const char *cmd, lcl_int idx);
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

/* In-place mutation forms (`List::push!`, `put!`, ...). */
lcl_return_code lcl_std_mut_cell(lcl_interp *interp, const char *cmd,
                                 const lcl_word *name_w, lcl_value **cell_out,
                                 lcl_value **name_out);
void lcl_std_mut_begin(lcl_value *cell, lcl_value **work, int *owned);
void lcl_std_mut_commit(lcl_value *cell, lcl_value *work, int owned);
void lcl_std_mut_abort(lcl_value *work, int owned);
int lcl_std_mut_check_cycle(lcl_interp *interp, const char *cmd,
                            const char *name, lcl_value *cell,
                            lcl_value *value);
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
