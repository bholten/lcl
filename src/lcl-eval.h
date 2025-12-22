#ifndef LCL_EVAL_H
#define LCL_EVAL_H

#include "lcl-compile.h"

int lcl_call_user_proc(lcl_interp *interp, lcl_proc *p,
                       int argc, lcl_value **argv, lcl_value **out);

int lcl_call_from_words(lcl_interp *interp, const lcl_command *cmd,
                        lcl_value **out);

int lcl_eval_string(lcl_interp *interp, const char *src, lcl_value **out);

int lcl_eval_program(lcl_interp *interp, const lcl_program *pr,
                     lcl_value **out);

int lcl_eval_word_to_str(lcl_interp *interp, const lcl_word *w,
                         lcl_value **out);

int lcl_eval_word(lcl_interp *interp, const lcl_word *w,
                  lcl_value **out);

lcl_return_code lcl_call(lcl_interp *interp, const lcl_command *command,
                         lcl_value **out);

#endif
