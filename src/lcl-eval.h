#ifndef LCL_EVAL_H
#define LCL_EVAL_H

#include "lcl-compile.h"

lcl_return_code lcl_call_user_proc(lcl_interp *interp, lcl_value *proc_val,
                                   lcl_proc *p, const char *invoked_name,
                                   int argc, lcl_value **argv, lcl_value **out);

lcl_return_code lcl_call_from_words(lcl_interp *interp, const lcl_command *cmd,
                                    lcl_value **out);

lcl_return_code lcl_eval_string(lcl_interp *interp, const char *src,
                                lcl_value **out);
lcl_return_code lcl_eval_string_file(lcl_interp *interp, const char *src,
                                     const char *file, lcl_value **out);
lcl_return_code lcl_eval_bytes(lcl_interp *interp, const char *src, size_t len,
                               lcl_value **out);
lcl_return_code lcl_eval_bytes_file(lcl_interp *interp, const char *src,
                                    size_t len, const char *file,
                                    lcl_value **out);

lcl_return_code lcl_eval_program(lcl_interp *interp, const lcl_program *pr,
                                 lcl_value **out);

lcl_return_code lcl_step_tick(lcl_interp *interp);

lcl_program *lcl_compile_report(lcl_interp *interp, const char *src,
                                const char *file);
lcl_program *lcl_compile_report_at(lcl_interp *interp, const char *src,
                                   const char *file, long start_line);
const char *lcl_dyn_source_name(lcl_interp *interp, const char *tag, char *buf,
                                size_t n);

/* Operand evaluation for special forms.
 *
 * The return code and `*out` form one result and travel together:
 *
 *   LCL_RC_OK       *out is the value (+1).
 *   LCL_RC_ERR      the interpreter error state is set; *out is NULL.
 *   BREAK/CONTINUE  *out is the control payload (+1, always "").
 *   RETURN          *out is the proc's return value (+1).
 *
 * A control code is not a failure. A caller that does not own the
 * transition -- and no operand position owns one; only a loop owns
 * BREAK/CONTINUE from its *body* and only proc activation owns RETURN
 * -- must release its own locals and return the code with the payload
 * in its own `*out`:
 *
 *   rc = lcl_eval_word(interp, args[i], &v);
 *   if (rc != LCL_RC_OK) { ...release locals...; *out = v; return rc; }
 *
 */
lcl_return_code lcl_eval_word_to_str(lcl_interp *interp, const lcl_word *w,
                                     lcl_value **out);

lcl_return_code lcl_eval_word(lcl_interp *interp, const lcl_word *w,
                              lcl_value **out);

lcl_return_code lcl_call(lcl_interp *interp, const lcl_command *command,
                         lcl_value **out);

#endif
