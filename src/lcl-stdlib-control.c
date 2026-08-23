#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "lcl-stdlib-internal.h"

/* and / or - short-circuit logical special forms.
 *
 * Operands are evaluated left to right through lcl_eval_word -- the
 * same per-word machinery ordinary argument evaluation uses -- but
 * conditionally: evaluation stops at the deciding operand, so later
 * operands can rely on guards (`and [in-bounds ...] [index ...]`).
 *
 * Value-returning: `and` yields the first falsy operand (else the
 * final operand); `or` yields the first truthy operand (else the
 * final operand). Identities: [and] -> 1, [or] -> 0.
 *
 * `@` spread is rejected up front: spread operands are already
 * evaluated, so laziness buys nothing -- List::all?/List::any? cover
 * evaluated collections. */
static lcl_return_code and_or_impl(lcl_interp *interp, int argc,
                                   const lcl_word **args, lcl_value **out,
                                   int stop_when_truthy, long identity,
                                   const char *name) {
  lcl_value *val = NULL;
  int i;

  for (i = 0; i < argc; i++) {
    if (args[i]->expand) {
      char msg[96];
      snprintf(msg, sizeof(msg), "%s: @ spread is not supported; use List::%s?",
               name, stop_when_truthy ? "any" : "all");
      LCL_ERR_MSG_DUP(interp, msg);
      return LCL_RC_ERR;
    }
  }

  if (argc == 0) {
    *out = lcl_int_new(identity);
    return LCL_RC_OK;
  }

  for (i = 0; i < argc; i++) {
    lcl_ref_dec(val);
    val = NULL;

    if (lcl_eval_word(interp, args[i], &val) != LCL_RC_OK) {
      lcl_ref_dec(val);
      return LCL_RC_ERR;
    }

    if (lcl_value_is_true(val) == stop_when_truthy) {
      break;
    }
  }

  *out = val;
  return LCL_RC_OK;
}

static lcl_return_code s_and(lcl_interp *interp, int argc,
                             const lcl_word **args, lcl_value **out) {
  return and_or_impl(interp, argc, args, out, 0, 1, "and");
}

static lcl_return_code s_or(lcl_interp *interp, int argc, const lcl_word **args,
                            lcl_value **out) {
  return and_or_impl(interp, argc, args, out, 1, 0, "or");
}

static lcl_return_code c_not(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  int b;

  if (!lcl_std_chk_argc(interp, "not", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  b = !lcl_value_is_true(argv[0]);

  *out = lcl_int_new(b);
  return LCL_RC_OK;
}

static lcl_return_code s_return(lcl_interp *interp, int argc,
                                const lcl_word **args, lcl_value **out) {
  lcl_return_code rc;

  if (argc == 0) {
    *out = lcl_string_new("");
    return LCL_RC_RETURN;
  }

  rc = lcl_eval_word(interp, args[0], out);

  if (rc == LCL_RC_TAILCALL) {
    return LCL_RC_TAILCALL;
  }

  if (rc == LCL_RC_OK) {
    return LCL_RC_RETURN;
  }

  return LCL_RC_ERR;
}

/* break - exit from innermost loop */
static lcl_return_code s_break(lcl_interp *interp, int argc,
                               const lcl_word **args, lcl_value **out) {
  (void)interp;
  (void)args;

  if (!lcl_std_chk_argc(interp, "break", argc, 0, 0)) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_BREAK;
}

/* continue - skip to next iteration of innermost loop */
static lcl_return_code s_continue(lcl_interp *interp, int argc,
                                  const lcl_word **args, lcl_value **out) {
  (void)interp;
  (void)args;

  if (!lcl_std_chk_argc(interp, "continue", argc, 0, 0)) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_CONTINUE;
}

/* error - throw an error with the given message */
static lcl_return_code c_error(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *msg;

  (void)out;

  if (!lcl_std_chk_argc(interp, "error", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  msg = lcl_value_to_string(argv[0]);
  LCL_ERR_MSG_DUP(interp, msg ? msg : "error");

  return LCL_RC_ERR;
}

/* catch - execute script and catch errors
 * Usage: catch script ?resultVar? ?errorVar?
 * Returns: 0 if script succeeded, 1 if error occurred
 */
static lcl_return_code c_catch(lcl_interp *interp, int argc,
                               const lcl_word **args, lcl_value **out) {
  lcl_program *prog = NULL;
  int prog_owned = 0;
  lcl_value *result = NULL;
  char *result_var = NULL;
  char *error_var = NULL;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "catch", argc, 1, 3)) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    lcl_value *rv = NULL;
    const char *rv_s;

    if (lcl_eval_word_to_str(interp, args[1], &rv) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, rv, &rv_s) != LCL_OK) {
      lcl_ref_dec(rv);
      return LCL_RC_ERR;
    }

    result_var = strdup(rv_s);
    lcl_ref_dec(rv);

    if (!result_var) {
      LCL_ERR_MSG(interp, "catch: out of memory");
      return LCL_RC_ERR;
    }
  }

  if (argc >= 3) {
    lcl_value *ev = NULL;
    const char *ev_s;

    if (lcl_eval_word_to_str(interp, args[2], &ev) != LCL_RC_OK) {
      free(result_var);
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, ev, &ev_s) != LCL_OK) {
      free(result_var);
      lcl_ref_dec(ev);
      return LCL_RC_ERR;
    }

    error_var = strdup(ev_s);
    lcl_ref_dec(ev);

    if (!error_var) {
      free(result_var);
      LCL_ERR_MSG(interp, "catch: out of memory");
      return LCL_RC_ERR;
    }
  }

  if (args[0]->compiled) {
    prog = args[0]->compiled;
    prog_owned = 0;
  } else {
    lcl_value *body_v = NULL;
    const char *body_src;

    if (lcl_eval_word_to_str(interp, args[0], &body_v) != LCL_RC_OK) {
      free(result_var);
      free(error_var);
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, body_v, &body_src) != LCL_OK) {
      lcl_ref_dec(body_v);
      free(result_var);
      free(error_var);
      return LCL_RC_ERR;
    }

    {
      char name[256];
      prog = lcl_compile_report(
          interp, body_src,
          lcl_dyn_source_name(interp, "catch", name, sizeof(name)));
    }
    lcl_ref_dec(body_v);

    if (prog) {
      prog_owned = 1;
    }
  }

  if (!prog) {
    rc = LCL_RC_ERR;
  } else {
    rc = lcl_eval_program(interp, prog, &result);
    lcl_std_free_if_owned(prog, prog_owned);
  }

  if (rc == LCL_RC_ERR && interp->interrupted) {
    lcl_ref_dec(result);
    free(result_var);
    free(error_var);
    return LCL_RC_ERR;
  }

  if (rc == LCL_RC_ERR) {
    if (error_var) {
      const char *err_msg = interp->err_msg ? interp->err_msg : "unknown error";
      lcl_value *err_v = lcl_string_new(err_msg);
      lcl_define(interp, error_var, err_v);
      lcl_ref_dec(err_v);
    }

    if (result_var) {
      lcl_value *empty = lcl_string_new("");
      lcl_define(interp, result_var, empty);
      lcl_ref_dec(empty);
    }

    LCL_ERR_CLEAR(interp);

    if (result) {
      lcl_ref_dec(result);
    }

    free(result_var);
    free(error_var);

    *out = lcl_int_new(1);
    return LCL_RC_OK;
  } else if (rc == LCL_RC_OK) {
    if (result_var && result) {
      lcl_define(interp, result_var, result);
    }

    if (error_var) {
      lcl_value *empty = lcl_string_new("");
      lcl_define(interp, error_var, empty);
      lcl_ref_dec(empty);
    }

    if (result) {
      lcl_ref_dec(result);
    }

    free(result_var);
    free(error_var);

    *out = lcl_int_new(0);
    return LCL_RC_OK;
  } else {
    free(result_var);
    free(error_var);
    *out = result;
    return rc;
  }
}

/* if condition {then-body} [else] [{else-body}]
 *
 * Simple two-branch conditional:
 * - condition is evaluated (can be [expr], $var, or literal)
 * - then-body executes if condition is truthy
 * - else-body (optional) executes if condition is falsy
 * - The 'else' keyword is optional for readability
 *
 * Valid forms:
 *   if [cond] {then}                 - no else branch
 *   if [cond] {then} {else}          - positional else
 *   if [cond] {then} else {else}     - with else keyword
 */
static lcl_return_code s_if(lcl_interp *interp, int argc, const lcl_word **args,
                            lcl_value **out) {
  int saved_tail_position = interp->in_tail_position;
  lcl_value *cond_v = NULL;
  lcl_value *body_v = NULL;
  lcl_program *body_p = NULL;
  int is_true;
  lcl_return_code rc;
  int body_idx;
  int else_body_idx = -1;

  if (!lcl_std_chk_argc(interp, "if", argc, 2, 4)) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    else_body_idx = 2;
  } else if (argc == 4) {
    const lcl_word *kw = args[2];

    if (kw->np != 1 || kw->wp[0].kind != LCL_WP_LIT ||
        strcmp(kw->wp[0].as.lit.s, "else") != 0) {
      LCL_ERR_MSG(interp, "if: expected 'else' between the two branches");
      return LCL_RC_ERR;
    }

    else_body_idx = 3;
  }

  interp->in_tail_position = 0;

  if (lcl_eval_word(interp, args[0], &cond_v) != LCL_RC_OK) {
    lcl_ref_dec(cond_v);
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  is_true = lcl_value_is_true(cond_v);
  lcl_ref_dec(cond_v);

  if (is_true) {
    body_idx = 1;
  } else if (else_body_idx > 0) {
    body_idx = else_body_idx;
  } else {
    interp->in_tail_position = saved_tail_position;
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  if (args[body_idx]->compiled) {
    interp->in_tail_position = saved_tail_position;
    return lcl_eval_program(interp, args[body_idx]->compiled, out);
  }

  if (lcl_eval_word_to_str(interp, args[body_idx], &body_v) != LCL_RC_OK) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  {
    const char *body_src;

    if (lcl_value_to_cstring(interp, body_v, &body_src) != LCL_OK) {
      lcl_ref_dec(body_v);
      interp->in_tail_position = saved_tail_position;
      return LCL_RC_ERR;
    }

    {
      char name[256];
      body_p = lcl_compile_report(
          interp, body_src,
          lcl_dyn_source_name(interp, "if", name, sizeof(name)));
    }
  }

  lcl_ref_dec(body_v);

  if (!body_p) {
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  interp->in_tail_position = saved_tail_position;
  rc = lcl_eval_program(interp, body_p, out);
  lcl_program_free(body_p);

  return rc;
}

static int word_is_literal(const lcl_word *w, const char *lit) {
  if (w->np != 1) {
    return 0;
  }

  if (w->wp[0].kind != LCL_WP_LIT) {
    return 0;
  }

  return strcmp(w->wp[0].as.lit.s, lit) == 0;
}

/* cond test1 expr1 test2 expr2 ... [else exprN]
 * Multi-branch conditional with short-circuit evaluation.
 * Evaluates tests left-to-right until one is truthy, then evaluates
 * and returns that clause's expression. The 'else' keyword marks
 * the default clause (must be last). Error if no clause matches.
 */
static lcl_return_code s_cond(lcl_interp *interp, int argc,
                              const lcl_word **args, lcl_value **out) {
  int saved_tail_position = interp->in_tail_position;
  int i;
  lcl_value *test_v = NULL;
  int is_true;
  lcl_return_code rc;

  if (argc < 2 || (argc % 2) != 0) {
    LCL_ERR_MSG(interp, "cond: requires pairs of test/expr arguments");
    return LCL_RC_ERR;
  }

  for (i = 0; i < argc - 2; i += 2) {
    if (word_is_literal(args[i], "else")) {
      LCL_ERR_MSG(interp, "cond: 'else' must be the last clause");
      return LCL_RC_ERR;
    }
  }

  interp->in_tail_position = 0;

  for (i = 0; i < argc; i += 2) {
    if (word_is_literal(args[i], "else")) {
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);
      return rc;
    }

    if (lcl_eval_word(interp, args[i], &test_v) != LCL_RC_OK) {
      lcl_ref_dec(test_v);
      interp->in_tail_position = saved_tail_position;
      return LCL_RC_ERR;
    }

    is_true = lcl_value_is_true(test_v);
    lcl_ref_dec(test_v);
    test_v = NULL;

    if (is_true) {
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);
      return rc;
    }
  }

  interp->in_tail_position = saved_tail_position;
  LCL_ERR_MSG(interp, "cond: no matching clause");
  return LCL_RC_ERR;
}

/* case expr key1 expr1 key2 expr2 ... [else exprN]
 *
 * Value dispatch with equality comparison.  Evaluates the scrutinee
 * once, then compares keys using == until a match is found. The
 * 'else' keyword marks the default clause (must be last). Error if no
 * clause matches.
 */
static lcl_return_code s_case(lcl_interp *interp, int argc,
                              const lcl_word **args, lcl_value **out) {
  int saved_tail_position = interp->in_tail_position;
  struct eq_cycle_guard guard = {{0}, {0}, 0};
  lcl_value *scrutinee = NULL;
  lcl_value *key_v = NULL;
  int i;
  int is_match;
  lcl_return_code rc;

  if (argc < 3 || (argc % 2) != 1) {
    LCL_ERR_MSG(interp, "case: requires scrutinee and pairs of key/expr");
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc - 2; i += 2) {
    if (word_is_literal(args[i], "else")) {
      LCL_ERR_MSG(interp, "case: 'else' must be the last clause");
      return LCL_RC_ERR;
    }
  }

  interp->in_tail_position = 0;

  if (lcl_eval_word(interp, args[0], &scrutinee) != LCL_RC_OK) {
    lcl_ref_dec(scrutinee);
    interp->in_tail_position = saved_tail_position;
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc; i += 2) {
    if (word_is_literal(args[i], "else")) {
      lcl_ref_dec(scrutinee);
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);

      return rc;
    }

    if (lcl_eval_word(interp, args[i], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(key_v);
      lcl_ref_dec(scrutinee);
      interp->in_tail_position = saved_tail_position;
      return LCL_RC_ERR;
    }

    is_match = lcl_value_equal_deep(scrutinee, key_v, &guard);
    lcl_ref_dec(key_v);
    key_v = NULL;

    if (is_match) {
      lcl_ref_dec(scrutinee);
      interp->in_tail_position = saved_tail_position;
      rc = lcl_eval_word(interp, args[i + 1], out);
      return rc;
    }
  }

  lcl_ref_dec(scrutinee);
  interp->in_tail_position = saved_tail_position;
  LCL_ERR_MSG(interp, "case: no matching clause");

  return LCL_RC_ERR;
}

/* while test body - loop while test is true, re-evaluating test each iteration
 */
static lcl_return_code s_while(lcl_interp *interp, int argc,
                               const lcl_word **args, lcl_value **out) {
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  int test_owned = 0;
  int body_owned = 0;
  lcl_value *last = NULL;
  int test_is_braced;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "while", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[0]->braced;

  if (test_is_braced) {
    if (lcl_std_get_body_program(interp, args[0], "while-test", &test_p,
                                 &test_owned) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }
  }

  if (lcl_std_get_body_program(interp, args[1], "while-body", &body_p,
                               &body_owned) != LCL_RC_OK) {
    lcl_std_free_if_owned(test_p, test_owned);
    return LCL_RC_ERR;
  }

  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    if (lcl_step_tick(interp) != LCL_RC_OK) {
      lcl_std_free_if_owned(test_p, test_owned);
      lcl_std_free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);

      if (rc != LCL_RC_OK) {
        lcl_std_free_if_owned(test_p, test_owned);
        lcl_std_free_if_owned(body_p, body_owned);
        if (last) {
          lcl_ref_dec(last);
        }

        if (cond_v) {
          *out = cond_v;
        }
        return rc;
      }
    } else {
      if (lcl_eval_word(interp, args[0], &cond_v) != LCL_RC_OK) {
        lcl_ref_dec(cond_v);
        lcl_std_free_if_owned(body_p, body_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        return LCL_RC_ERR;
      }
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (!is_true) {
      break;
    }

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      lcl_std_free_if_owned(test_p, test_owned);
      lcl_std_free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      lcl_std_free_if_owned(test_p, test_owned);
      lcl_std_free_if_owned(body_p, body_owned);
      *out = last;
      return LCL_RC_RETURN;
    }
  }

  lcl_std_free_if_owned(test_p, test_owned);
  lcl_std_free_if_owned(body_p, body_owned);
  *out = last ? last : lcl_string_new("");
  return LCL_RC_OK;
}

/* for start test next body - Tcl-style for loop */
static lcl_return_code s_for(lcl_interp *interp, int argc,
                             const lcl_word **args, lcl_value **out) {
  lcl_program *start_p = NULL;
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  lcl_program *next_p = NULL;
  int start_owned = 0;
  int test_owned = 0;
  int body_owned = 0;
  int next_owned = 0;
  lcl_value *last = NULL;
  lcl_value *tmp = NULL;
  int test_is_braced;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "for", argc, 4, 4)) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[1]->braced;

  if (lcl_std_get_body_program(interp, args[0], "for-start", &start_p,
                               &start_owned) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (test_is_braced) {
    if (lcl_std_get_body_program(interp, args[1], "for-test", &test_p,
                                 &test_owned) != LCL_RC_OK) {
      lcl_std_free_if_owned(start_p, start_owned);
      return LCL_RC_ERR;
    }
  }

  if (lcl_std_get_body_program(interp, args[2], "for-next", &next_p,
                               &next_owned) != LCL_RC_OK) {
    lcl_std_free_if_owned(start_p, start_owned);

    if (test_p) {
      lcl_std_free_if_owned(test_p, test_owned);
    }

    return LCL_RC_ERR;
  }

  if (lcl_std_get_body_program(interp, args[3], "for-body", &body_p,
                               &body_owned) != LCL_RC_OK) {
    lcl_std_free_if_owned(start_p, start_owned);

    if (test_p) {
      lcl_std_free_if_owned(test_p, test_owned);
    }

    lcl_std_free_if_owned(next_p, next_owned);

    return LCL_RC_ERR;
  }

  rc = lcl_eval_program(interp, start_p, &tmp);
  lcl_std_free_if_owned(start_p, start_owned);

  if (tmp) {
    lcl_ref_dec(tmp);
  }

  if (rc != LCL_RC_OK) {
    if (test_p) {
      lcl_std_free_if_owned(test_p, test_owned);
    }

    lcl_std_free_if_owned(body_p, body_owned);
    lcl_std_free_if_owned(next_p, next_owned);

    return rc;
  }

  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    if (lcl_step_tick(interp) != LCL_RC_OK) {
      lcl_std_free_if_owned(test_p, test_owned);
      lcl_std_free_if_owned(body_p, body_owned);
      lcl_std_free_if_owned(next_p, next_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);

      if (rc != LCL_RC_OK) {
        lcl_std_free_if_owned(test_p, test_owned);
        lcl_std_free_if_owned(body_p, body_owned);
        lcl_std_free_if_owned(next_p, next_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        if (cond_v) {
          *out = cond_v;
        }

        return rc;
      }
    } else {
      if (lcl_eval_word(interp, args[1], &cond_v) != LCL_RC_OK) {
        lcl_ref_dec(cond_v);
        lcl_std_free_if_owned(body_p, body_owned);
        lcl_std_free_if_owned(next_p, next_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        return LCL_RC_ERR;
      }
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (!is_true) {
      break;
    }

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      tmp = NULL;
      rc = lcl_eval_program(interp, next_p, &tmp);

      if (tmp) {
        lcl_ref_dec(tmp);
      }

      if (rc != LCL_RC_OK && rc != LCL_RC_CONTINUE) {
        if (test_p) {
          lcl_std_free_if_owned(test_p, test_owned);
        }

        lcl_std_free_if_owned(body_p, body_owned);
        lcl_std_free_if_owned(next_p, next_owned);

        if (last) {
          lcl_ref_dec(last);
        }

        return rc;
      }

      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      if (test_p) {
        lcl_std_free_if_owned(test_p, test_owned);
      }

      lcl_std_free_if_owned(body_p, body_owned);
      lcl_std_free_if_owned(next_p, next_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      if (test_p) {
        lcl_std_free_if_owned(test_p, test_owned);
      }

      lcl_std_free_if_owned(body_p, body_owned);
      lcl_std_free_if_owned(next_p, next_owned);

      *out = last;

      return LCL_RC_RETURN;
    }

    tmp = NULL;
    rc = lcl_eval_program(interp, next_p, &tmp);

    if (tmp) {
      lcl_ref_dec(tmp);
    }

    if (rc != LCL_RC_OK) {
      if (test_p) {
        lcl_std_free_if_owned(test_p, test_owned);
      }

      lcl_std_free_if_owned(body_p, body_owned);
      lcl_std_free_if_owned(next_p, next_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }
  }

  if (test_p) {
    lcl_std_free_if_owned(test_p, test_owned);
  }

  lcl_std_free_if_owned(body_p, body_owned);
  lcl_std_free_if_owned(next_p, next_owned);

  *out = last ? last : lcl_string_new("");

  return LCL_RC_OK;
}

/* Turn the evaluated iterable into the list foreach walks. Only
 * values with structure iterate: a LIST as-is, a DICT as (key value)
 * pairs, a STRING as one-byte strings. Text is never reparsed as a
 * list -- that is what (...) literals and String::split are for. */
static lcl_return_code foreach_source(lcl_interp *interp, lcl_value *v,
                                      lcl_value **out) {
  if (v->type == LCL_CELL) {
    v = v->as.cell.inner;
    if (!v) {
      LCL_ERR_MSG(interp, "foreach: expected list, dict, or string, got "
                          "empty cell");
      return LCL_RC_ERR;
    }
  }

  switch (v->type) {
  case LCL_LIST:
    lcl_ref_inc(v);
    *out = v;
    return LCL_RC_OK;

  case LCL_DICT:
    if (lcl_std_dict_items(v, out) != LCL_OK) {
      LCL_ERR_MSG(interp, "foreach: out of memory");
      return LCL_RC_ERR;
    }
    return LCL_RC_OK;

  case LCL_STRING: {
    const char *s = lcl_value_to_string(v);
    lcl_value *chars = lcl_list_new();
    char buf[2];

    if (!chars) {
      LCL_ERR_MSG(interp, "foreach: out of memory");
      return LCL_RC_ERR;
    }

    buf[1] = '\0';

    for (; s && *s; s++) {
      lcl_value *c;

      buf[0] = *s;
      c = lcl_string_new(buf);

      if (!c || lcl_list_push(&chars, c) != LCL_OK) {
        lcl_ref_dec(c);
        lcl_ref_dec(chars);
        LCL_ERR_MSG(interp, "foreach: out of memory");
        return LCL_RC_ERR;
      }

      lcl_ref_dec(c);
    }

    *out = chars;
    return LCL_RC_OK;
  }

  default:
    return lcl_std_err_expected_got(interp, "foreach", "list, dict, or string",
                                    v);
  }
}

/* foreach varname iterable body - iterate a list's elements, a
 * dict's (key value) pairs, or a string's bytes */
static lcl_return_code s_foreach(lcl_interp *interp, int argc,
                                 const lcl_word **args, lcl_value **out) {
  lcl_value *varname_v = NULL;
  lcl_value *iter_v = NULL;
  lcl_value *list_v = NULL;
  lcl_program *body_p = NULL;
  int body_owned = 0;
  lcl_value *last = NULL;
  const char *varname;
  size_t i;
  size_t list_len;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "foreach", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  /* A braced word here is the Tcl idiom `foreach x {a b c}`. Text no
   * longer reparses as a list, so it would silently iterate bytes;
   * refuse the spelling outright */
  if (args[1]->braced) {
    LCL_ERR_MSG(interp, "foreach: {...} is text, not a list; write (a b c) "
                        "for a list, or \"abc\" / $s to iterate a string's "
                        "characters");
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &varname_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, varname_v, &varname) != LCL_OK) {
    lcl_ref_dec(varname_v);
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &iter_v) != LCL_RC_OK) {
    lcl_ref_dec(iter_v);
    lcl_ref_dec(varname_v);

    return LCL_RC_ERR;
  }

  rc = foreach_source(interp, iter_v, &list_v);
  lcl_ref_dec(iter_v);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(varname_v);
    return rc;
  }

  if (lcl_std_get_body_program(interp, args[2], "foreach", &body_p,
                               &body_owned) != LCL_RC_OK) {
    lcl_ref_dec(varname_v);
    lcl_ref_dec(list_v);

    return LCL_RC_ERR;
  }

  list_len = lcl_list_len(list_v);

  for (i = 0; i < list_len; i++) {
    lcl_value *elem = NULL;

    if (lcl_list_get(list_v, i, &elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "foreach: internal error reading list");
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_std_free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    if (lcl_env_let(&interp->env, varname, elem) != LCL_OK) {
      LCL_ERR_MSG(interp, "foreach: out of memory");
      lcl_ref_dec(elem);
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_std_free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return LCL_RC_ERR;
    }

    lcl_ref_dec(elem);

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_std_free_if_owned(body_p, body_owned);

      if (last) {
        lcl_ref_dec(last);
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_std_free_if_owned(body_p, body_owned);

      *out = last;

      return LCL_RC_RETURN;
    }
  }

  lcl_ref_dec(varname_v);
  lcl_ref_dec(list_v);
  lcl_std_free_if_owned(body_p, body_owned);

  *out = last ? last : lcl_string_new("");

  return LCL_RC_OK;
}

void lcl_std_register_control(lcl_interp *interp) {
  lcl_register_spec(interp, "and", s_and);
  lcl_register_spec(interp, "or", s_or);
  lcl_register_proc(interp, "not", c_not);
  lcl_register_spec(interp, "return", s_return);
  lcl_register_spec(interp, "if", s_if);
  lcl_register_spec(interp, "cond", s_cond);
  lcl_register_spec(interp, "case", s_case);
  lcl_register_spec(interp, "while", s_while);
  lcl_register_spec(interp, "for", s_for);
  lcl_register_spec(interp, "foreach", s_foreach);
  lcl_register_spec(interp, "break", s_break);
  lcl_register_spec(interp, "continue", s_continue);
  lcl_register_proc(interp, "error", c_error);
  lcl_register_spec(interp, "catch", c_catch);
}
