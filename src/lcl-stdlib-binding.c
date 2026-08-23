#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "lcl-name.h"
#include "lcl-stdlib-internal.h"

/* cell? : check if value is a cell */
static lcl_return_code c_is_cell(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "cell?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_CELL ? 1 : 0);
  return LCL_RC_OK;
}

/* binding-cell name : returns the cell object for a binding (special form) */
static lcl_return_code s_binding_cell(lcl_interp *interp, int argc,
                                      const lcl_word **args, lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *binding = NULL;
  const char *name;

  if (!lcl_std_chk_argc(interp, "binding-cell", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &name_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name, &binding) != LCL_OK) {
    lcl_std_err_undefined(interp, "binding-cell", name);
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);

  if (binding->type != LCL_CELL) {
    lcl_std_err_expected_got(interp, "binding-cell", "cell binding", binding);
    lcl_ref_dec(binding);
    return LCL_RC_ERR;
  }

  *out = binding;
  return LCL_RC_OK;
}

/* same-binding? name1 name2 : check if two bindings refer to the same cell */
static lcl_return_code s_same_binding(lcl_interp *interp, int argc,
                                      const lcl_word **args, lcl_value **out) {
  lcl_value *name1_v = NULL;
  lcl_value *name2_v = NULL;
  lcl_value *binding1 = NULL;
  lcl_value *binding2 = NULL;
  const char *name1;
  const char *name2;
  int same;

  if (!lcl_std_chk_argc(interp, "same-binding?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[0], &name1_v) != LCL_RC_OK) {
    lcl_ref_dec(name1_v);
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &name2_v) != LCL_RC_OK) {
    lcl_ref_dec(name2_v);
    lcl_ref_dec(name1_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name1_v, &name1) != LCL_OK) {
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name2_v, &name2) != LCL_OK) {
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name1, &binding1) != LCL_OK) {
    lcl_std_err_undefined(interp, "same-binding?", name1);
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name2, &binding2) != LCL_OK) {
    lcl_std_err_undefined(interp, "same-binding?", name2);
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    lcl_ref_dec(binding1);
    return LCL_RC_ERR;
  }

  if (binding1->type != LCL_CELL || binding2->type != LCL_CELL) {
    lcl_std_err_expected_got(interp, "same-binding?", "cell binding",
                             binding1->type != LCL_CELL ? binding1 : binding2);
    lcl_ref_dec(name1_v);
    lcl_ref_dec(name2_v);
    lcl_ref_dec(binding1);
    lcl_ref_dec(binding2);
    return LCL_RC_ERR;
  }

  same = (binding1 == binding2) ? 1 : 0;

  lcl_ref_dec(name1_v);
  lcl_ref_dec(name2_v);
  lcl_ref_dec(binding1);
  lcl_ref_dec(binding2);

  *out = lcl_int_new(same);
  return LCL_RC_OK;
}

static lcl_return_code c_let(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  const char *name;

  if (!lcl_std_chk_argc(interp, "let", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_name_has_sep(name) && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "let: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::def'");
    return LCL_RC_ERR;
  }

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_check_kind(interp, "let", name,
                                  argv[1]->type == LCL_NAMESPACE) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_def_target_bind(interp, name, argv[1]) != LCL_OK) {
      LCL_ERR_MSG(interp, "let: out of memory");
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name, argv[1]) != LCL_OK) {
      LCL_ERR_MSG(interp, "let: out of memory");
      return LCL_RC_ERR;
    }
  }

  *out = lcl_ref_inc(argv[1]);

  return LCL_RC_OK;
}

/* gensym ?prefix? - generate a unique symbol name */
static lcl_return_code c_gensym(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *prefix = "_G";
  char buf[128];
  int n;

  if (!lcl_std_chk_argc(interp, "gensym", argc, 0, 1)) {
    return LCL_RC_ERR;
  }

  if (argc == 1) {
    if (lcl_value_to_cstring(interp, argv[0], &prefix) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (strlen(prefix) > 96) {
      LCL_ERR_MSG(interp, "gensym: prefix too long (max 96 bytes)");
      return LCL_RC_ERR;
    }
  }

  interp->gensym_counter++;
  n = snprintf(buf, sizeof(buf), "%s%lu", prefix, interp->gensym_counter);

  if (n < 0 || (size_t)n >= sizeof(buf)) {
    LCL_ERR_MSG(interp, "gensym: name too long");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code c_ref(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "ref", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_cell_new(argv[0]);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code c_get(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  lcl_value *val = NULL;
  const char *name;

  if (!lcl_std_chk_argc(interp, "getvar", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name, &val) != LCL_OK) {
    return lcl_std_err_undefined(interp, "getvar", name);
  }

  if (val->type == LCL_CELL) {
    if (lcl_cell_get(val, out) != LCL_OK) {
      LCL_ERR_MSG(interp, "getvar: cell has been cleared");
      lcl_ref_dec(val);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(val);
  } else {
    *out = val;
  }

  return LCL_RC_OK;
}

static lcl_return_code s_set_bang(lcl_interp *interp, int argc,
                                  const lcl_word **args, lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *val_v = NULL;
  lcl_value *cell = NULL;
  const char *name_str;

  if (!lcl_std_chk_argc(interp, "set!", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, args[1], &val_v) != LCL_RC_OK) {
    lcl_ref_dec(val_v);
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_get_value(interp, name_str, &cell) == LCL_OK) {
    if (cell->type == LCL_CELL && lcl_cell_would_cycle(cell, val_v)) {
      LCL_ERR_MSG(interp, "assignment would create reference cycle "
                          "(mutual recursion not allowed)");
      lcl_ref_dec(cell);
      lcl_ref_dec(name_v);
      lcl_ref_dec(val_v);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(cell);
  }

  if (lcl_env_set_bang(&interp->env, name_str, val_v) != LCL_OK) {
    lcl_value *probe = NULL;

    if (lcl_env_get_value(interp, name_str, &probe) != LCL_OK) {
      lcl_std_err_undefined(interp, "set!", name_str);
    } else {
      lcl_std_err_expected_got(interp, "set!", "cell (declare with 'var')",
                               probe);
      lcl_ref_dec(probe);
    }

    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);

    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  *out = lcl_ref_inc(val_v);
  lcl_ref_dec(val_v);

  return LCL_RC_OK;
}

static lcl_return_code s_var(lcl_interp *interp, int argc,
                             const lcl_word **argv, lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *init_v = NULL;
  const char *name_str;

  if (!lcl_std_chk_argc(interp, "var", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, argv[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_name_has_sep(name_str) && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "var: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::def'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, argv[1], &init_v) != LCL_RC_OK) {
    lcl_ref_dec(init_v);
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_check_kind(interp, "var", name_str, 0) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(init_v);
      return LCL_RC_ERR;
    }

    if (lcl_def_target_var(interp, name_str, init_v) != LCL_OK) {
      LCL_ERR_MSG(interp, "var: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(init_v);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_var(&interp->env, name_str, init_v) != LCL_OK) {
      LCL_ERR_MSG(interp, "var: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(init_v);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(init_v);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

static lcl_return_code make_lambda(lcl_interp *interp, const char *self_name,
                                   const lcl_word *params_word,
                                   const lcl_word *body_word, lcl_value **out) {
  lcl_value *params_s = NULL;
  lcl_value *body_s = NULL;
  lcl_program *body_p = NULL;
  lcl_param_spec pspec;
  lcl_upvalue *upvals = NULL;
  int nupvals = 0;
  const char *params_str;
  const char *body_str;

  pspec.params = NULL;
  pspec.n_required = 0;
  pspec.n_optional = 0;
  pspec.rest_name = NULL;

  if (lcl_eval_word_to_str(interp, params_word, &params_s) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, body_word, &body_s) != LCL_RC_OK) {
    lcl_ref_dec(params_s);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, params_s, &params_str) != LCL_OK) {
    lcl_ref_dec(params_s);
    lcl_ref_dec(body_s);
    return LCL_RC_ERR;
  }

  if (lcl_parse_params(interp, params_str, &pspec) != LCL_RC_OK) {
    lcl_ref_dec(params_s);
    lcl_ref_dec(body_s);
    return LCL_RC_ERR;
  }
  lcl_ref_dec(params_s);

  if (lcl_value_to_cstring(interp, body_s, &body_str) != LCL_OK) {
    lcl_param_spec_free(&pspec);
    lcl_ref_dec(body_s);
    return LCL_RC_ERR;
  }

  if (body_word->braced && body_word->line > 0 && interp->cur_file) {
    body_p = lcl_compile_report_at(interp, body_str, interp->cur_file,
                                   body_word->line);
  } else {
    char name[256];
    body_p = lcl_compile_report(
        interp, body_str,
        lcl_dyn_source_name(interp, "lambda", name, sizeof(name)));
  }
  lcl_ref_dec(body_s);

  if (!body_p) {
    lcl_param_spec_free(&pspec);
    return LCL_RC_ERR;
  }

  if (lcl_build_upvalues(interp, body_p, &pspec, self_name, &upvals,
                         &nupvals) != LCL_RC_OK) {
    lcl_param_spec_free(&pspec);
    lcl_program_free(body_p);
    return LCL_RC_ERR;
  }

  *out = lcl_proc_new(self_name, upvals, nupvals, &pspec, body_p,
                      interp->cur_file, interp->cur_line);

  if (!*out) {
    LCL_ERR_MSG(interp, "lambda: out of memory");
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/*
 * Two forms:
 *   lambda {params} {body}        - anonymous (argc == 2, first arg braced)
 *   lambda name {params} {body}   - named, can self-recurse (argc == 3)
 *
 * Detection: if argc == 2 → anonymous
 *            if argc == 3 → named (first arg is the name)
 */
static lcl_return_code s_lambda(lcl_interp *interp, int argc,
                                const lcl_word **args, lcl_value **out) {
  if (argc == 2) {
    return make_lambda(interp, NULL, args[0], args[1], out);
  } else if (argc == 3) {
    lcl_value *name_v = NULL;
    const char *self_name;
    lcl_return_code rc;

    if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, name_v, &self_name) != LCL_OK) {
      lcl_ref_dec(name_v);
      return LCL_RC_ERR;
    }

    rc = make_lambda(interp, self_name, args[1], args[2], out);
    lcl_ref_dec(name_v);
    return rc;
  } else {
    LCL_ERR_MSG(interp, "lambda: expected 2 or 3 arguments");
    return LCL_RC_ERR;
  }
}

static lcl_return_code s_proc(lcl_interp *interp, int argc,
                              const lcl_word **args, lcl_value **out) {
  /* proc name {params} {body}
   * Desugars to: let name [lambda name {params} {body}] */
  lcl_value *name_v = NULL;
  lcl_value *lam = NULL;
  const char *name_str;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "proc", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_name_has_sep(name_str) && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "proc: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::proc'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  rc = make_lambda(interp, name_str, args[1], args[2], &lam);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return rc;
  }

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_check_kind(interp, "proc", name_str, 0) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }

    if (lcl_def_target_bind(interp, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "proc: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "proc: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(lam);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static lcl_return_code s_macro(lcl_interp *interp, int argc,
                               const lcl_word **args, lcl_value **out) {
  /* macro name {params} {body}
   * Like proc, but the return value is compiled and evaluated
   * in the caller's frame at dispatch time. */
  lcl_value *name_v = NULL;
  lcl_value *lam = NULL;
  const char *name_str;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "macro", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_name_has_sep(name_str) && interp->def_depth <= interp->def_floor) {
    LCL_ERR_MSG(interp, "macro: qualified name not allowed here; "
                        "define inside 'namespace' or use 'ns::proc'");
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  rc = make_lambda(interp, name_str, args[1], args[2], &lam);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return rc;
  }

  ((lcl_proc *)lam->as.procedure.proc)->is_macro = 1;

  if (interp->def_depth > interp->def_floor) {
    if (lcl_def_target_check_kind(interp, "macro", name_str, 0) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }

    if (lcl_def_target_bind(interp, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "macro: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  } else {
    if (lcl_env_let(&interp->env, name_str, lam) != LCL_OK) {
      LCL_ERR_MSG(interp, "macro: out of memory");
      lcl_ref_dec(name_v);
      lcl_ref_dec(lam);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(lam);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* macroexpand name arg1 arg2 ...
 *
 * Calls the macro's proc body and returns the template string WITHOUT
 * compiling/evaluating it. This is the explicit mechanism for using
 * macro expansions as values: eval [macroexpand my_macro args]
 */
static lcl_return_code s_macroexpand(lcl_interp *interp, int argc,
                                     const lcl_word **args, lcl_value **out) {
  lcl_value *callee = NULL;
  lcl_value **argv = NULL;
  lcl_proc *p;
  int nargs;
  int i;
  lcl_return_code rc;

  if (!lcl_std_chk_argc(interp, "macroexpand", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  rc = lcl_eval_word(interp, args[0], &callee);

  if (rc != LCL_RC_OK) {
    lcl_ref_dec(callee);
    return rc;
  }

  if (callee->type == LCL_STRING) {
    lcl_value *name = callee;
    const char *name_str;
    callee = NULL;

    if (lcl_value_to_cstring(interp, name, &name_str) != LCL_OK) {
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_command(interp, name_str, &callee) != LCL_OK) {
      LCL_ERR_MSG(interp, "macroexpand: unknown command");
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(name);
  }

  if (callee->type != LCL_PROC) {
    lcl_ref_dec(callee);
    LCL_ERR_MSG(interp, "macroexpand: not a procedure");
    return LCL_RC_ERR;
  }

  p = (lcl_proc *)callee->as.procedure.proc;

  if (!p->is_macro) {
    lcl_ref_dec(callee);
    LCL_ERR_MSG(interp, "macroexpand: not a macro");
    return LCL_RC_ERR;
  }

  nargs = argc - 1;

  if (nargs > 0) {
    argv = malloc(sizeof(lcl_value *) * (size_t)nargs);

    if (!argv) {
      LCL_ERR_MSG(interp, "macroexpand: out of memory");
      lcl_ref_dec(callee);
      return LCL_RC_ERR;
    }

    for (i = 0; i < nargs; i++) {
      lcl_value *arg_v = NULL;

      rc = lcl_eval_word(interp, args[i + 1], &arg_v);

      if (rc != LCL_RC_OK) {
        lcl_ref_dec(arg_v);

        while (--i >= 0) {
          lcl_ref_dec(argv[i]);
        }

        free(argv);
        lcl_ref_dec(callee);
        return rc;
      }

      argv[i] = arg_v;
    }
  }

  rc = lcl_call_user_proc(interp, callee, p, NULL, nargs, argv, out);

  for (i = 0; i < nargs; i++) {
    lcl_ref_dec(argv[i]);
  }

  free(argv);
  lcl_ref_dec(callee);
  return rc;
}

static lcl_return_code c_is_proc(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "proc?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      argv[0]->type == LCL_PROC || argv[0]->type == LCL_CPROC ? 1 : 0);

  return LCL_RC_OK;
}

/* Resolve a proc-valued argument. */
static lcl_value *resolve_proc_arg(lcl_interp *interp, const char *who,
                                   lcl_value *v, lcl_value **held) {
  *held = NULL;

  if (v->type == LCL_STRING) {
    const char *name;

    if (lcl_value_to_cstring(interp, v, &name) != LCL_OK) {
      return NULL;
    }

    if (lcl_env_get_command(interp, name, held) != LCL_OK) {
      lcl_std_err_expected_got(interp, who, "proc", v);
      return NULL;
    }

    v = *held;
  }

  if (v->type != LCL_PROC && v->type != LCL_CPROC) {
    lcl_std_err_expected_got(interp, who, "proc", v);
    lcl_ref_dec(*held);
    *held = NULL;
    return NULL;
  }

  return v;
}

/* Proc::name fn -- registered proc name or empty-string for lambda */
static lcl_return_code c_proc_name(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  lcl_value *held;
  lcl_value *fn;
  const char *name = "";

  if (!lcl_std_chk_argc(interp, "Proc::name", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  fn = resolve_proc_arg(interp, "Proc::name", argv[0], &held);

  if (!fn) {
    return LCL_RC_ERR;
  }

  if (fn->type == LCL_PROC) {
    lcl_proc *p = fn->as.procedure.proc;
    name = p->self_name ? p->self_name : "";
  } else if (fn->str_repr) {
    name = fn->str_repr;
  }

  *out = lcl_string_new(name);
  lcl_ref_dec(held);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* Proc::params fn - parameter names in order, as a flat list. */
static lcl_return_code c_proc_params(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *held;
  lcl_value *fn;
  lcl_value *list;

  if (!lcl_std_chk_argc(interp, "Proc::params", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  fn = resolve_proc_arg(interp, "Proc::params", argv[0], &held);

  if (!fn) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  if (!list) {
    lcl_ref_dec(held);
    LCL_ERR_MSG(interp, "Proc::params: out of memory");
    return LCL_RC_ERR;
  }

  if (fn->type == LCL_PROC) {
    const lcl_param_spec *ps = &fn->as.procedure.proc->pspec;
    int n = ps->n_required + ps->n_optional;
    int i;
    char buf[256];

    for (i = 0; i <= n; i++) {
      const char *pname;
      lcl_value *item;
      const char *mark;

      if (i < n) {
        pname = ps->params[i].name;
        mark = i < ps->n_required ? "" : "?";
      } else if (ps->rest_name) {
        pname = ps->rest_name;
        mark = "*";
      } else {
        break;
      }

      if (strlen(pname) + 2 > sizeof(buf)) {
        lcl_ref_dec(list);
        lcl_ref_dec(held);
        LCL_ERR_MSG(interp, "Proc::params: parameter name too long");
        return LCL_RC_ERR;
      }

      strcpy(buf, mark);
      strcat(buf, pname);
      item = lcl_string_new(buf);

      if (!item || lcl_list_push(&list, item) != LCL_OK) {
        lcl_ref_dec(item);
        lcl_ref_dec(list);
        lcl_ref_dec(held);
        LCL_ERR_MSG(interp, "Proc::params: out of memory");
        return LCL_RC_ERR;
      }

      lcl_ref_dec(item);
    }
  }

  lcl_ref_dec(held);
  *out = list;
  return LCL_RC_OK;
}

/* Proc::origin fn - #{file F line L}: where the proc was
   constructed. */
static lcl_return_code c_proc_origin(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *held;
  lcl_value *fn;
  lcl_value *dict;

  if (!lcl_std_chk_argc(interp, "Proc::origin", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  fn = resolve_proc_arg(interp, "Proc::origin", argv[0], &held);

  if (!fn) {
    return LCL_RC_ERR;
  }

  dict = lcl_dict_new();

  if (!dict) {
    lcl_ref_dec(held);
    LCL_ERR_MSG(interp, "Proc::origin: out of memory");
    return LCL_RC_ERR;
  }

  if (fn->type == LCL_PROC && fn->as.procedure.proc->file) {
    lcl_proc *p = fn->as.procedure.proc;
    lcl_value *file_v = lcl_string_new(p->file);
    lcl_value *line_v = lcl_int_new(p->line);
    int ok = file_v && line_v && lcl_dict_put(&dict, "file", file_v) == LCL_OK &&
             lcl_dict_put(&dict, "line", line_v) == LCL_OK;

    lcl_ref_dec(file_v);
    lcl_ref_dec(line_v);

    if (!ok) {
      lcl_ref_dec(dict);
      lcl_ref_dec(held);
      LCL_ERR_MSG(interp, "Proc::origin: out of memory");
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(held);
  *out = dict;
  return LCL_RC_OK;
}

/* arity fn - return (min max) where max is -1 for unbounded */
static lcl_return_code c_arity(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  lcl_value *func;
  lcl_value *result;
  lcl_value *min_val;
  lcl_value *max_val;
  lcl_value *resolved = NULL;
  int min_args;
  int max_args;

  if (!lcl_std_chk_argc(interp, "arity", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  func = argv[0];

  if (func->type == LCL_STRING) {
    const char *name;

    if (lcl_value_to_cstring(interp, func, &name) != LCL_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_env_get_command(interp, name, &resolved) != LCL_OK) {
      return lcl_std_err_expected_got(interp, "arity", "proc", func);
    }

    func = resolved;
  }

  if (func->type == LCL_PROC) {
    lcl_proc *p = func->as.procedure.proc;
    min_args = p->pspec.n_required;
    max_args =
        p->pspec.rest_name ? -1 : (p->pspec.n_required + p->pspec.n_optional);
  } else if (func->type == LCL_CPROC) {
    min_args = 0;
    max_args = -1;
  } else {
    lcl_ref_dec(resolved);
    return lcl_std_err_expected_got(interp, "arity", "proc", func);
  }

  lcl_ref_dec(resolved);

  result = lcl_list_new();
  min_val = lcl_int_new(min_args);
  max_val = lcl_int_new(max_args);

  lcl_list_push(&result, min_val);
  lcl_list_push(&result, max_val);

  lcl_ref_dec(min_val);
  lcl_ref_dec(max_val);

  *out = result;
  return LCL_RC_OK;
}

void lcl_std_register_binding(lcl_interp *interp) {
  lcl_register_proc(interp, "cell?", c_is_cell);
  lcl_register_proc(interp, "proc?", c_is_proc);
  lcl_register_proc(interp, "arity", c_arity);

  {
    lcl_value *proc_ns = lcl_ns_new("Proc");
    lcl_define_take(interp, "Proc", proc_ns);
    lcl_ns_def_take(proc_ns, "name", lcl_c_proc_new("Proc::name", c_proc_name));
    lcl_ns_def_take(proc_ns, "params",
                    lcl_c_proc_new("Proc::params", c_proc_params));
    lcl_ns_def_take(proc_ns, "origin",
                    lcl_c_proc_new("Proc::origin", c_proc_origin));
  }
  lcl_register_proc(interp, "let", c_let);
  lcl_register_proc(interp, "ref", c_ref);
  lcl_register_proc(interp, "gensym", c_gensym);
  lcl_register_proc(interp, "getvar", c_get);
  lcl_register_spec(interp, "var", s_var);
  lcl_register_spec(interp, "set!", s_set_bang);
  lcl_register_spec(interp, "binding-cell", s_binding_cell);
  lcl_register_spec(interp, "same-binding?", s_same_binding);
  lcl_register_spec(interp, "lambda", s_lambda);
  lcl_register_spec(interp, "proc", s_proc);
  lcl_register_spec(interp, "macro", s_macro);
  lcl_register_spec(interp, "macroexpand", s_macroexpand);
}
