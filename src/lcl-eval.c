#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-lex.h"
#include "lcl-values.h"
#include "str-compat.h"

static int path_resolves_at_root(lcl_interp *interp, const char *name) {
  char rooted[300];
  lcl_value *v = NULL;

  if (!name || name[0] == '\0' || (name[0] == ':' && name[1] == ':')) {
    return 0;
  }

  if (strlen(name) + 3 > sizeof(rooted)) {
    return 0;
  }

  sprintf(rooted, "::%s", name);

  if (lcl_env_get_value(interp, rooted, &v) != LCL_OK) {
    return 0;
  }

  lcl_ref_dec(v);
  return 1;
}

static void describe_name_failure(lcl_interp *interp, const char *name,
                                  int kind, char *msg, size_t msglen) {
  const char *sep = strstr(name, "::");
  int hint = path_resolves_at_root(interp, name);
  size_t used = 0;

  if (!sep || (name[0] == ':' && name[1] == ':')) {
    if (kind == 1) {
      used = (size_t)snprintf(msg, msglen, "unknown command: %.200s", name);
    } else {
      used =
          (size_t)snprintf(msg, msglen, "undefined variable \"%.200s\"", name);
    }
  } else {
    char root[256];
    const char *rest = NULL;
    lcl_value *current = NULL;
    size_t n = (size_t)(sep - name);

    if (n >= sizeof(root)) {
      n = sizeof(root) - 1;
    }

    memcpy(root, name, n);
    root[n] = '\0';

    if (lcl_env_get_value(interp, root, &current) != LCL_OK) {
      used = (size_t)snprintf(
          msg, msglen, "undefined name \"%.128s\" (in \"%.200s\")", root, name);
    } else {
      char sofar[300];
      char part[256];
      const char *next_rest = NULL;

      strcpy(sofar, root);
      rest = sep + 2;

      for (;;) {
        const char *part_name;
        lcl_value *next = NULL;

        if (current->type == LCL_CELL) {
          lcl_value *inner = NULL;

          if (lcl_cell_get(current, &inner) != LCL_OK) {
            break;
          }

          lcl_ref_dec(current);
          current = inner;
        }

        if (current->type != LCL_NAMESPACE) {
          used = (size_t)snprintf(
              msg, msglen,
              "\"%.128s\" is not a namespace (cannot resolve \"%.200s\")",
              sofar, name);
          lcl_ref_dec(current);
          current = NULL;
          break;
        }

        if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
          part_name = part;
        } else {
          part_name = rest;
          next_rest = NULL;
        }

        if (lcl_ns_get(current, part_name, &next) != LCL_OK) {
          used = (size_t)snprintf(msg, msglen,
                                  "namespace \"%.128s\" has no member "
                                  "\"%.128s\"",
                                  sofar, part_name);
          lcl_ref_dec(current);
          current = NULL;
          break;
        }

        lcl_ref_dec(current);
        current = next;

        if (strlen(sofar) + strlen(part_name) + 3 < sizeof(sofar)) {
          strcat(sofar, "::");
          strcat(sofar, part_name);
        }

        if (!next_rest || !*next_rest) {
          used = (size_t)snprintf(msg, msglen,
                                  kind == 1 ? "unknown command: %.200s"
                                            : "undefined variable \"%.200s\"",
                                  name);
          lcl_ref_dec(current);
          current = NULL;
          break;
        }

        rest = next_rest;
      }

      if (current) {
        lcl_ref_dec(current);
      }
    }
  }

  if (hint && used < msglen) {
    snprintf(msg + used, msglen - used,
             kind == 1 ? " (use ::%.200s for an intentional live top-level "
                         "reference)"
                       : " (use ${::%.200s} for an intentional live top-level "
                         "reference)",
             name);
  }
}

lcl_return_code lcl_eval_word(lcl_interp *interp, const lcl_word *w,
                              lcl_value **out);

static int setup_tail_call(lcl_interp *interp, int argc, lcl_value **argv) {
  int i;
  if (argc > 0) {
    interp->pending_tail.argv = malloc(sizeof(lcl_value *) * (size_t)argc);

    if (!interp->pending_tail.argv) {
      return 0;
    }
  } else {
    interp->pending_tail.argv = NULL;
  }

  interp->pending_tail.argc = argc;

  for (i = 0; i < argc; i++) {
    interp->pending_tail.argv[i] = lcl_ref_inc(argv[i]);
  }

  interp->pending_tail.valid = 1;
  return 1;
}

/* Bugfix/fuzzing:
 *
 * On a non-OK return, `*payload_out` receives the control payload
 * (+1) when the failing argument word carried one — a BREAK /
 * CONTINUE / RETURN surfacing from a `[...]` argument. The caller
 * forwards it so `puts [return 5]` propagates the value, exactly like
 * a direct `return 5` command would. */
static lcl_return_code build_argv(lcl_interp *interp, const lcl_command *cmd,
                                  int *argc_out, lcl_value ***argv_out,
                                  lcl_value **payload_out) {
  int i;
  int j;
  int word_count = cmd->argc - 1;
  lcl_return_code rc;
  lcl_value **argv = NULL;
  int argc = 0;
  int cap = 0;

  *payload_out = NULL;

  if (word_count < 0) {
    *argc_out = 0;
    *argv_out = NULL;
    return LCL_RC_OK;
  }

  cap = word_count > 0 ? word_count : 1;
  argv = (lcl_value **)calloc((size_t)cap, sizeof(*argv));

  if (!argv) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < word_count; i++) {
    const lcl_word *w = &cmd->w[i + 1];
    lcl_value *val = NULL;

    rc = lcl_eval_word(interp, w, &val);

    if (rc != LCL_RC_OK) {
      *payload_out = val;
      goto cleanup;
    }

    if (w->expand) {
      if (val->type == LCL_LIST) {
        size_t len = lcl_list_len(val);
        size_t k;

        if (argc + (int)len > cap) {
          int new_cap = argc + (int)len + 8;
          lcl_value **new_argv =
              (lcl_value **)realloc(argv, (size_t)new_cap * sizeof(*argv));

          if (!new_argv) {
            lcl_ref_dec(val);
            rc = LCL_RC_ERR;
            goto cleanup;
          }

          argv = new_argv;
          cap = new_cap;
        }

        for (k = 0; k < len; k++) {
          lcl_value *elem = NULL;

          if (lcl_list_get(val, k, &elem) != LCL_OK) {
            lcl_ref_dec(val);
            rc = LCL_RC_ERR;
            goto cleanup;
          }

          argv[argc++] = elem;
        }

        lcl_ref_dec(val);
      } else {
        if (argc >= cap) {
          int new_cap = cap * 2 + 1;
          lcl_value **new_argv =
              (lcl_value **)realloc(argv, (size_t)new_cap * sizeof(*argv));

          if (!new_argv) {
            lcl_ref_dec(val);
            rc = LCL_RC_ERR;
            goto cleanup;
          }

          argv = new_argv;
          cap = new_cap;
        }

        argv[argc++] = val;
      }
    } else {
      if (argc >= cap) {
        int new_cap = cap * 2 + 1;
        lcl_value **new_argv =
            (lcl_value **)realloc(argv, (size_t)new_cap * sizeof(*argv));

        if (!new_argv) {
          lcl_ref_dec(val);
          rc = LCL_RC_ERR;
          goto cleanup;
        }

        argv = new_argv;
        cap = new_cap;
      }

      argv[argc++] = val;
    }
  }

  *argc_out = argc;
  *argv_out = argv;

  return LCL_RC_OK;

cleanup:
  for (j = 0; j < argc; j++) {
    lcl_ref_dec(argv[j]);
  }
  free(argv);
  return rc;
}

lcl_return_code lcl_call_user_proc(lcl_interp *interp, lcl_value *proc_val,
                                   lcl_proc *p, const char *invoked_name,
                                   int argc, lcl_value **argv,
                                   lcl_value **out) {
  int i;
  lcl_return_code rc;
  lcl_env saved = interp->env;
  int saved_tail_position = interp->in_tail_position;
  lcl_value *saved_current_proc = interp->current_proc;
  /* Raise the def-target floor for the duration of this proc call so
   * that bare `let`/`var`/`proc` inside the body (or anything called
   * from it) does not write through to a `namespace` builder active
   * at the call site. A nested `namespace foo { ... }` inside the
   * body still pushes its own target above the floor and works
   * normally. Restored on every exit path. See spec §6 /
   * def_floor. */
  int saved_def_floor = interp->def_floor;
  int saved_dyn_mode = interp->env.dyn_mode;

  lcl_value **current_argv = argv;
  int current_argc = argc;
  int owns_argv = 0;

  interp->current_proc = proc_val;
  interp->def_floor = interp->def_depth;
  interp->env.dyn_mode = 0;

  for (;;) {
    /* Bugfix:
     *
     * Use caller's frame as parent for command lookup - no cycle
     * because proc doesn't store a reference to this frame (uses
     * upvalues instead) */
    lcl_frame *child = lcl_frame_new(NULL);

    if (child) {
      child->caller = lcl_frame_ref_inc(saved.frame);
    }

    if (!child) {
      if (owns_argv) {
        for (i = 0; i < current_argc; i++) {
          lcl_ref_dec(current_argv[i]);
        }

        free(current_argv);
      }

      interp->current_proc = saved_current_proc;
      interp->def_floor = saved_def_floor;
      interp->env.dyn_mode = saved_dyn_mode;
      return LCL_RC_ERR;
    }

    interp->env.frame = child;

    {
      int min_args = p->pspec.n_required;
      int max_args = p->pspec.n_required + p->pspec.n_optional;
      int has_rest = (p->pspec.rest_name != NULL);

      if (current_argc < min_args || (!has_rest && current_argc > max_args)) {
        char msg[160];
        const char *pname = invoked_name   ? invoked_name
                            : p->self_name ? p->self_name
                                           : "anonymous proc";

        if (has_rest) {
          snprintf(msg, sizeof(msg),
                   "%.64s: expected at least %d argument%s, got %d", pname,
                   min_args, min_args == 1 ? "" : "s", current_argc);
        } else if (min_args == max_args) {
          snprintf(msg, sizeof(msg), "%.64s: expected %d argument%s, got %d",
                   pname, min_args, min_args == 1 ? "" : "s", current_argc);
        } else {
          snprintf(msg, sizeof(msg),
                   "%.64s: expected %d to %d arguments, got %d", pname,
                   min_args, max_args, current_argc);
        }

        LCL_ERR_MSG_DUP(interp, msg);
        goto arity_error;
      }

      if (0) {
      arity_error:
        interp->env = saved;
        lcl_frame_ref_dec(child);
        if (owns_argv) {
          for (i = 0; i < current_argc; i++) {
            lcl_ref_dec(current_argv[i]);
          }

          free(current_argv);
        }

        interp->current_proc = saved_current_proc;
        interp->def_floor = saved_def_floor;
        interp->env.dyn_mode = saved_dyn_mode;
        return LCL_RC_ERR;
      }
    }

    for (i = 0; i < p->nupvals; i++) {
      lcl_value *bindv = p->upvals[i].value;

      if (p->upvals[i].is_ns_root) {
        if (!p->upvals[i].anchor) {
          continue;
        }

        bindv = p->upvals[i].anchor->target;

        if (!bindv) {
          char msg[300];

          snprintf(msg, sizeof(msg), "%.256s: namespace no longer exists",
                   p->upvals[i].name);
          LCL_ERR_MSG_DUP(interp, msg);
          goto bind_error;
        }
      }

      if (!hash_table_put(child->locals, p->upvals[i].name, bindv)) {
        LCL_ERR_MSG(interp, "out of memory binding upvalue");
        goto bind_error;
      }
    }

    if (p->self_name != NULL) {
      if (!hash_table_put(child->locals, p->self_name, proc_val)) {
        LCL_ERR_MSG(interp, "out of memory binding proc name");
        goto bind_error;
      }
    }

    {
      int arg_idx = 0;

      for (i = 0; i < p->pspec.n_required; i++) {
        if (lcl_env_let(&interp->env, p->pspec.params[i].name,
                        current_argv[arg_idx++]) != LCL_OK) {
          LCL_ERR_MSG(interp, "out of memory binding parameter");
          goto bind_error;
        }
      }

      for (i = 0; i < p->pspec.n_optional; i++) {
        int pidx = p->pspec.n_required + i;

        if (arg_idx < current_argc) {
          if (lcl_env_let(&interp->env, p->pspec.params[pidx].name,
                          current_argv[arg_idx++]) != LCL_OK) {
            LCL_ERR_MSG(interp, "out of memory binding parameter");
            goto bind_error;
          }
        } else {
          lcl_value *def_val = NULL;
          lcl_return_code def_rc = lcl_eval_program(
              interp, p->pspec.params[pidx].def_prog, &def_val);

          if (def_rc != LCL_RC_OK) {
            /* Fuzzing: a control code surfacing from a default-value
             * program carries a +1 payload (#93); it dies here —
             * there is no enclosing loop or return target across the
             * call boundary. */
            lcl_ref_dec(def_val);
            interp->env = saved;
            lcl_frame_ref_dec(child);

            if (owns_argv) {
              int j;

              for (j = 0; j < current_argc; j++) {
                lcl_ref_dec(current_argv[j]);
              }

              free(current_argv);
            }

            interp->current_proc = saved_current_proc;
            interp->def_floor = saved_def_floor;
            interp->env.dyn_mode = saved_dyn_mode;
            return def_rc;
          }

          if (lcl_env_let(&interp->env, p->pspec.params[pidx].name, def_val) !=
              LCL_OK) {
            lcl_ref_dec(def_val);
            LCL_ERR_MSG(interp, "out of memory binding parameter");
            goto bind_error;
          }
          lcl_ref_dec(def_val);
        }
      }

      if (p->pspec.rest_name) {
        lcl_value *rest_list = lcl_list_new();

        if (!rest_list) {
          LCL_ERR_MSG(interp, "out of memory allocating rest list");
          goto bind_error;
        }

        while (arg_idx < current_argc) {
          if (lcl_list_push(&rest_list, current_argv[arg_idx++]) != LCL_OK) {
            lcl_ref_dec(rest_list);
            LCL_ERR_MSG(interp, "out of memory appending to rest list");
            goto bind_error;
          }
        }

        if (lcl_env_let(&interp->env, p->pspec.rest_name, rest_list) !=
            LCL_OK) {
          lcl_ref_dec(rest_list);
          LCL_ERR_MSG(interp, "out of memory binding rest parameter");
          goto bind_error;
        }
        lcl_ref_dec(rest_list);
      }

      if (0) {
      bind_error:
        interp->env = saved;
        lcl_frame_ref_dec(child);

        if (owns_argv) {
          int j;

          for (j = 0; j < current_argc; j++) {
            lcl_ref_dec(current_argv[j]);
          }

          free(current_argv);
        }

        interp->current_proc = saved_current_proc;
        interp->def_floor = saved_def_floor;
        interp->env.dyn_mode = saved_dyn_mode;
        return LCL_RC_ERR;
      }
    }

    interp->in_tail_position = 1;
    rc = lcl_eval_program(interp, (lcl_program *)p->body, out);
    interp->in_tail_position = saved_tail_position;
    interp->env = saved;
    lcl_frame_ref_dec(child);

    if (owns_argv) {
      for (i = 0; i < current_argc; i++) {
        lcl_ref_dec(current_argv[i]);
      }

      free(current_argv);
      owns_argv = 0;
    }

    if (rc == LCL_RC_RETURN) {
      rc = LCL_RC_OK;
      break;
    }

    if (rc == LCL_RC_TAILCALL) {
      current_argv = interp->pending_tail.argv;
      current_argc = interp->pending_tail.argc;
      owns_argv = 1;
      interp->pending_tail.argv = NULL;
      interp->pending_tail.argc = 0;
      interp->pending_tail.valid = 0;
      continue;
    }

    break;
  }

  interp->current_proc = saved_current_proc;
  interp->def_floor = saved_def_floor;
  interp->env.dyn_mode = saved_dyn_mode;
  return rc;
}

lcl_return_code lcl_eval_word(lcl_interp *interp, const lcl_word *w,
                              lcl_value **out) {
  /* #75 rule 1: numeric literals were typed at scan time; the word
   * denotes that value directly. */
  if (w && w->typed) {
    *out = lcl_ref_inc(w->typed);
    return LCL_RC_OK;
  }

  if (w && w->np == 1) {
    lcl_word_piece *wp = &w->wp[0];

    switch (wp->kind) {
    case LCL_WP_VAR: {
      lcl_value *val = NULL;

      if (lcl_env_get_value(interp, wp->as.var.name, &val) != LCL_OK) {
        char msg[900];

        describe_name_failure(interp, wp->as.var.name, 0, msg, sizeof(msg));
        LCL_ERR_MSG_DUP(interp, msg);
        return LCL_RC_ERR;
      }

      if (val->type == LCL_CELL) {
        lcl_value *inner = NULL;
        /* Bugfix: distinguish cleared-cell from other cell_get
         * failures so the user sees a useful error instead of a
         * generic propagation. */
        if (!val->as.cell.inner) {
          LCL_ERR_MSG(interp, "use of cleared cell");
          lcl_ref_dec(val);
          return LCL_RC_ERR;
        }

        if (lcl_cell_get(val, &inner) != LCL_OK) {
          lcl_ref_dec(val);
          return LCL_RC_ERR;
        }

        lcl_ref_dec(val);
        val = inner;
      }

      *out = val;
      return LCL_RC_OK;
    }
    case LCL_WP_SUBCMD: {
      lcl_return_code sub_rc;
      int saved_in_subcmd = interp->in_subcmd;
      interp->in_subcmd = 1;
      sub_rc = lcl_eval_program(interp, wp->as.sub.program, out);
      interp->in_subcmd = saved_in_subcmd;
      return sub_rc;
    }
    case LCL_WP_LIT: break;
    }
  }

  return lcl_eval_word_to_str(interp, w, out);
}

static int lit_is_numeric(const char *s, size_t n) {
  return lcl_num_text_classify(s, n) != LCL_NUM_NONE;
}

lcl_return_code lcl_call_from_words(lcl_interp *interp, const lcl_command *cmd,
                                    lcl_value **out) {
  lcl_value *callee = NULL;
  lcl_return_code rc;
  int saved_tail_position = interp->in_tail_position;
  int was_in_subcmd = interp->in_subcmd;
  char invoked_buf[64];
  const char *invoked_name = NULL;
  interp->in_subcmd = 0;

  if (cmd->argc == 0) {
    *out = lcl_value_new_string("");

    return LCL_RC_OK;
  }

  if (cmd->argc == 1) {
    int is_bare_identifier =
        (cmd->w[0].np == 1 && cmd->w[0].wp[0].kind == LCL_WP_LIT &&
         !cmd->w[0].braced && !cmd->w[0].quoted &&
         !lit_is_numeric(cmd->w[0].wp[0].as.lit.s, cmd->w[0].wp[0].as.lit.n));

    if (!is_bare_identifier) {
      if (cmd->w[0].np == 1 && cmd->w[0].wp[0].kind == LCL_WP_SUBCMD) {
        return lcl_eval_program(interp, cmd->w[0].wp[0].as.sub.program, out);
      }

      interp->in_tail_position = 0;
      rc = lcl_eval_word(interp, &cmd->w[0], out);
      interp->in_tail_position = saved_tail_position;
      return rc;
    }
  }

  interp->in_tail_position = 0;

  rc = lcl_eval_word(interp, &cmd->w[0], &callee);

  if (rc != LCL_RC_OK) {
    interp->in_tail_position = saved_tail_position;

    /* Bugfix: a `[...]` head can surface a control payload (`[break]
     * foo`); forward it like any other command result. */
    if (callee) {
      *out = callee;
    }

    return rc;
  }

  /* Fuzz: An empty subcommand (`[]`) evaluates to no value; as a
   * command head that is an error, not a dispatch. */
  if (callee == NULL) {
    interp->in_tail_position = saved_tail_position;
    LCL_ERR_MSG(interp, "empty command name");
    return LCL_RC_ERR;
  }

  if (callee->type == LCL_STRING) {
    lcl_value *name = callee;
    const char *cmd_name;
    callee = NULL;

    if (lcl_value_to_cstring(interp, name, &cmd_name) != LCL_OK) {
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }

    /* Fuzz: */
    if (cmd_name[0] == '\0') {
      LCL_ERR_MSG(interp, "empty command name");
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_command(interp, cmd_name, &callee) != LCL_OK) {
      {
        char msg[900];

        describe_name_failure(interp, cmd_name, 1, msg, sizeof(msg));
        LCL_ERR_MSG_DUP(interp, msg);
        lcl_ref_dec(name);
      }

      return LCL_RC_ERR;
    }

    /* Keep the caller-facing name for diagnostics (e.g. arity errors
     * report the alias used at the call site, not the proc's
     * definition name). Copied because `name` is released here. */
    strncpy(invoked_buf, cmd_name, sizeof(invoked_buf) - 1);
    invoked_buf[sizeof(invoked_buf) - 1] = '\0';
    invoked_name = invoked_buf;

    lcl_ref_dec(name);

    if (callee->type != LCL_PROC && callee->type != LCL_CPROC) {
      if (cmd->argc == 1) {
        *out = callee;
        return LCL_RC_OK;
      }

      LCL_ERR_MSG(interp, "value is not callable");
      lcl_ref_dec(callee);

      return LCL_RC_ERR;
    }
  } else if (callee->type != LCL_PROC && callee->type != LCL_CPROC) {
    if (cmd->argc == 1) {
      *out = callee;
      return LCL_RC_OK;
    }

    {
      lcl_value *name = callee;
      const char *name_str;
      callee = NULL;

      if (lcl_value_to_cstring(interp, name, &name_str) != LCL_OK) {
        lcl_ref_dec(name);
        return LCL_RC_ERR;
      }

      if (lcl_env_get_command(interp, name_str, &callee) != LCL_OK) {
        /* Message parity with the STRING-head path above: a typed
         * numeric head (`42 foo`) resolves through here now. */
        LCL_ERR_MSG(interp, "unknown command");
        lcl_ref_dec(name);
        return LCL_RC_ERR;
      }

      lcl_ref_dec(name);
    }
  }

  if (callee->type == LCL_CPROC &&
      callee->as.c_proc.fn->kind == LCL_CK_SPECIAL) {
    int spec_argc = cmd->argc - 1;
    const lcl_word **raw = NULL;
    int i;

    if (spec_argc > 0) {
      raw = (const lcl_word **)malloc((size_t)spec_argc * sizeof(*raw));

      if (!raw) {
        interp->in_tail_position = saved_tail_position;
        lcl_ref_dec(callee);
        return LCL_RC_ERR;
      }

      for (i = 0; i < spec_argc; i++) {
        raw[i] = &cmd->w[i + 1];
      }
    }

    interp->in_tail_position = saved_tail_position;
    rc = callee->as.c_proc.fn->fn.spec(interp, spec_argc, raw, out);
    free(raw);
    lcl_ref_dec(callee);

    return rc;
  }

  {
    int argc = 0;
    int i;
    lcl_value **argv = NULL;
    lcl_value *payload = NULL;
    rc = build_argv(interp, cmd, &argc, &argv, &payload);

    if (rc != LCL_RC_OK) {
      interp->in_tail_position = saved_tail_position;
      lcl_ref_dec(callee);

      if (payload) {
        *out = payload;
      }

      return rc;
    }

    interp->in_tail_position = saved_tail_position;

    if (callee->type == LCL_CPROC) {
      rc = callee->as.c_proc.fn->fn.proc(interp, argc, argv, out);
    } else if (callee->type == LCL_PROC) {
      lcl_proc *p = (lcl_proc *)callee->as.procedure.proc;

      /* TCO: Check if this is a self-recursive tail call by comparing
       * against the currently executing proc, not just name
       * lookup. */
      if (saved_tail_position && callee == interp->current_proc) {
        if (!setup_tail_call(interp, argc, argv)) {
          for (i = 0; i < argc; i++) {
            lcl_ref_dec(argv[i]);
          }

          free(argv);
          lcl_ref_dec(callee);
          LCL_ERR_MSG(interp, "out of memory in tail call");
          return LCL_RC_ERR;
        }

        for (i = 0; i < argc; i++) {
          lcl_ref_dec(argv[i]);
        }

        free(argv);
        lcl_ref_dec(callee);
        *out = NULL;
        return LCL_RC_TAILCALL;
      }

      rc = lcl_call_user_proc(interp, callee, p, invoked_name, argc, argv, out);
      if (rc == LCL_RC_OK && p->is_macro) {
        if (was_in_subcmd) {
          LCL_ERR_MSG(interp, "macro cannot be used in value position");

          if (*out) {
            lcl_ref_dec(*out);
            *out = NULL;
          }

          rc = LCL_RC_ERR;
        } else {
          lcl_value *macro_result = *out;
          lcl_program *macro_prog;
          const char *macro_src;
          *out = NULL;

          if (lcl_value_to_cstring(interp, macro_result, &macro_src) !=
              LCL_OK) {
            lcl_ref_dec(macro_result);
            rc = LCL_RC_ERR;
          } else {
            {
              char tag[96];
              char name[256];
              snprintf(tag, sizeof(tag), "macro %s",
                       invoked_name ? invoked_name : "");
              macro_prog = lcl_compile_report(
                  interp, macro_src,
                  lcl_dyn_source_name(interp, tag, name, sizeof(name)));
            }
            lcl_ref_dec(macro_result);

            if (!macro_prog) {
              rc = LCL_RC_ERR;
            } else {
              /* Template evaluation is a dynamic-evaluation operation
               * (spec D4): like eval, run in the caller's frame with
               * dyn_mode set. */
              int saved_dyn = interp->env.dyn_mode;

              interp->env.dyn_mode = 1;
              rc = lcl_eval_program(interp, macro_prog, out);
              interp->env.dyn_mode = saved_dyn;
              lcl_program_free(macro_prog);
            }
          }
        }
      }
    } else {
      rc = LCL_RC_ERR;
    }

    for (i = 0; i < argc; i++) {
      lcl_ref_dec(argv[i]);
    }

    free(argv);
    lcl_ref_dec(callee);

    return rc;
  }
}

/* Compile `src`, recording a compile failure in the interp's error
 * state (message + file + line) the same way runtime errors are
 * recorded. Returns NULL on failure with the error already set. */
lcl_program *lcl_compile_report_at(lcl_interp *interp, const char *src,
                                   const char *file, long start_line) {
  lcl_compile_err cerr;
  lcl_program *p =
      lcl_program_compile_at(src, strlen(src), file, &cerr, 0, start_line);

  if (!p) {
    const char *saved_file = interp->cur_file;
    int saved_line = interp->cur_line;

    interp->cur_file = file;
    interp->cur_line = (int)cerr.line;
    LCL_ERR_MSG(interp, cerr.msg);
    interp->cur_file = saved_file;
    interp->cur_line = saved_line;
  }

  return p;
}

lcl_program *lcl_compile_report(lcl_interp *interp, const char *src,
                                const char *file) {
  return lcl_compile_report_at(interp, src, file, 1);
}

/* Name the source of a program compiled from a runtime string, for
 * example:
 *
 * "<eval at game.lcl:40>" (or "<eval>" with no current file). */
const char *lcl_dyn_source_name(lcl_interp *interp, const char *tag, char *buf,
                                size_t n) {
  if (interp->cur_file) {
    snprintf(buf, n, "<%s at %s:%d>", tag, interp->cur_file, interp->cur_line);
  } else {
    snprintf(buf, n, "<%s>", tag);
  }

  return buf;
}

lcl_return_code lcl_eval_string_file(lcl_interp *interp, const char *src,
                                     const char *file, lcl_value **out) {
  lcl_program *P = lcl_compile_report(interp, src, file ? file : "<string>");
  lcl_return_code rc;

  if (!P) {
    return LCL_RC_ERR;
  }

  rc = lcl_eval_program(interp, P, out);
  lcl_program_free(P);

  return rc;
}

lcl_return_code lcl_eval_string(lcl_interp *interp, const char *src,
                                lcl_value **out) {
  return lcl_eval_string_file(interp, src, NULL, out);
}

lcl_return_code lcl_eval_bytes_file(lcl_interp *interp, const char *src,
                                    size_t len, const char *file,
                                    lcl_value **out) {
  lcl_compile_err cerr;
  lcl_program *P =
      lcl_program_compile_bytes_ex(src, len, file ? file : "<bytes>", &cerr);
  lcl_return_code rc;

  if (!P) {
    const char *saved_file = interp->cur_file;
    int saved_line = interp->cur_line;

    interp->cur_file = file ? file : "<bytes>";
    interp->cur_line = (int)cerr.line;
    LCL_ERR_MSG(interp, cerr.msg);
    interp->cur_file = saved_file;
    interp->cur_line = saved_line;
    return LCL_RC_ERR;
  }

  rc = lcl_eval_program(interp, P, out);
  lcl_program_free(P);

  return rc;
}

lcl_return_code lcl_eval_bytes(lcl_interp *interp, const char *src, size_t len,
                               lcl_value **out) {
  return lcl_eval_bytes_file(interp, src, len, NULL, out);
}

lcl_return_code lcl_eval_word_to_str(lcl_interp *interp, const lcl_word *w,
                                     lcl_value **out) {
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  int i;

  if (!w || w->np == 0) {
    *out = lcl_value_new_string("");
    return *out ? LCL_RC_OK : LCL_RC_ERR;
  }

  if (w->np == 1 && w->wp[0].kind == LCL_WP_LIT) {
    char *s = (char *)malloc(w->wp[0].as.lit.n + 1);

    if (!s) {
      return LCL_RC_ERR;
    }

    memcpy(s, w->wp[0].as.lit.s, w->wp[0].as.lit.n);
    s[w->wp[0].as.lit.n] = '\0';
    *out = lcl_value_new_string(s);
    free(s);
    return *out ? LCL_RC_OK : LCL_RC_ERR;
  }

  for (i = 0; i < w->np; i++) {
    lcl_word_piece *wp = &w->wp[i];

    switch (wp->kind) {
    case LCL_WP_LIT: {
      size_t need = len + wp->as.lit.n + 1;

      if (need > cap) {
        size_t newcap = cap ? cap * 2 : 64;
        char *newbuf;

        while (newcap < need) {
          newcap *= 2;
        }

        newbuf = (char *)realloc(buf, newcap);

        if (!newbuf) {
          free(buf);
          return LCL_RC_ERR;
        }

        buf = newbuf;
        cap = newcap;
      }

      memcpy(buf + len, wp->as.lit.s, wp->as.lit.n);
      len += wp->as.lit.n;
      break;
    }
    case LCL_WP_VAR: {
      lcl_value *val = NULL;
      const char *s;
      size_t slen;
      size_t need;

      if (lcl_env_get_value(interp, wp->as.var.name, &val) != LCL_OK) {
        char msg[900];

        describe_name_failure(interp, wp->as.var.name, 0, msg, sizeof(msg));
        LCL_ERR_MSG_DUP(interp, msg);
        free(buf);
        return LCL_RC_ERR;
      }

      if (val->type == LCL_CELL) {
        lcl_value *inner = NULL;

        /* Bugfix: see lcl_eval_word above; same cleared-cell guard
         * applies for the multi-piece concatenation path. */
        if (!val->as.cell.inner) {
          LCL_ERR_MSG(interp, "use of cleared cell");
          lcl_ref_dec(val);
          free(buf);
          return LCL_RC_ERR;
        }

        if (lcl_cell_get(val, &inner) != LCL_OK) {
          lcl_ref_dec(val);
          free(buf);
          return LCL_RC_ERR;
        }

        lcl_ref_dec(val);
        val = inner;
      }

      if (lcl_value_to_cstring(interp, val, &s) != LCL_OK) {
        lcl_ref_dec(val);
        free(buf);
        return LCL_RC_ERR;
      }

      slen = strlen(s);
      need = len + slen + 1;

      if (need > cap) {
        size_t newcap = cap ? cap * 2 : 64;
        char *newbuf;

        while (newcap < need) {
          newcap *= 2;
        }

        newbuf = (char *)realloc(buf, newcap);

        if (!newbuf) {
          lcl_ref_dec(val);
          free(buf);
          return LCL_RC_ERR;
        }

        buf = newbuf;
        cap = newcap;
      }

      memcpy(buf + len, s, slen);
      len += slen;
      lcl_ref_dec(val);
      break;
    }
    case LCL_WP_SUBCMD: {
      lcl_value *result = NULL;
      const char *s;
      size_t slen;
      size_t need;
      lcl_return_code rc;
      int saved_in_subcmd = interp->in_subcmd;
      interp->in_subcmd = 1;
      rc = lcl_eval_program(interp, wp->as.sub.program, &result);
      interp->in_subcmd = saved_in_subcmd;

      if (rc != LCL_RC_OK) {
        /* Fuzzing: a control code (BREAK/CONTINUE/RETURN) carries a
         * +1 payload; the concatenation consumes it here so this
         * function never hands a value to its caller on non-OK. */
        lcl_ref_dec(result);
        free(buf);
        return rc;
      }

      if (lcl_value_to_cstring(interp, result, &s) != LCL_OK) {
        lcl_ref_dec(result);
        free(buf);
        return LCL_RC_ERR;
      }

      slen = strlen(s);
      need = len + slen + 1;

      if (need > cap) {
        size_t newcap = cap ? cap * 2 : 64;
        char *newbuf;

        while (newcap < need) {
          newcap *= 2;
        }

        newbuf = (char *)realloc(buf, newcap);

        if (!newbuf) {
          lcl_ref_dec(result);
          free(buf);
          return LCL_RC_ERR;
        }

        buf = newbuf;
        cap = newcap;
      }

      memcpy(buf + len, s, slen);
      len += slen;
      lcl_ref_dec(result);
      break;
    }
    }
  }

  if (len == 0) {
    free(buf);
    *out = lcl_value_new_string("");
  } else {
    if (len >= cap) {
      char *newbuf = (char *)realloc(buf, len + 1);

      if (!newbuf) {
        free(buf);
        return LCL_RC_ERR;
      }

      buf = newbuf;
    }

    buf[len] = '\0';
    *out = lcl_value_new_string(buf);
    free(buf);
  }

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* One tick of the step-hook budget (lcl_set_step_hook). The eval
 * loop ticks once per command dispatched; loop specials tick once
 * per iteration so a loop whose test and body execute zero commands
 * (`while x {}`) still consumes budget instead of spinning
 * unbounded in C. Returns LCL_RC_ERR with the sticky abort recorded
 * when the budget is exhausted (or a prior abort is still in
 * effect), LCL_RC_OK otherwise. */
lcl_return_code lcl_step_tick(lcl_interp *interp) {
  if (interp->interrupted) {
    LCL_ERR_MSG(interp, "evaluation aborted by host");
    return LCL_RC_ERR;
  }

  if (interp->step_fn && --interp->step_countdown == 0) {
    interp->step_countdown = interp->step_interval;

    if (interp->step_fn(interp, interp->step_ud) != 0) {
      interp->interrupted = 1;
      LCL_ERR_MSG(interp, "evaluation aborted by host");
      return LCL_RC_ERR;
    }
  }

  return LCL_RC_OK;
}

lcl_return_code lcl_eval_program(lcl_interp *interp, const lcl_program *pr,
                                 lcl_value **out) {
  int i;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int saved_tail_position = interp->in_tail_position;
  /* `cur_file` is a borrowed pointer into the running program's owned
   * `file` field. If we leave it set after returning, the caller's
   * `lcl_program_free` turns it into a dangling pointer that the next
   * `LCL_ERR_MSG` will strdup from — a use-after-free.  Save+restore
   * around the whole program so the borrow stays scoped to the eval
   * that owns it. `cur_line` mirrors the same pattern for consistent
   * error context. */
  const char *saved_file = interp->cur_file;
  int saved_line = interp->cur_line;

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    LCL_ERR_MSG(interp, "maximum recursion depth exceeded");
    return LCL_RC_ERR;
  }

  /* Bugfix: A depth-0 entry is a fresh top-level evaluation from the
   * host: clear any sticky step-hook abort and restart its command
   * countdown, so one aborted eval doesn't poison the next. */
  if (interp->depth == 0) {
    interp->interrupted = 0;
    interp->step_countdown = interp->step_interval;
  }

  interp->depth++;

  for (i = 0; i < pr->ncmd; i++) {
    lcl_command *cmd = &pr->cmd[i];
    int is_last_cmd = (i == pr->ncmd - 1);

    interp->cur_file = pr->file;
    interp->cur_line = cmd->line;

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    /* Step hook (lcl_set_step_hook). `interrupted` is sticky: once
     * the hook aborts, every subsequent command errors until control
     * returns to the host (c_catch propagates instead of trapping),
     * so a script cannot outrun or swallow the abort.
     *
     * Added for fuzz testing and timeout budgeting. */
    if (lcl_step_tick(interp) != LCL_RC_OK) {
      rc = LCL_RC_ERR;
      break;
    }

    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->in_tail_position = saved_tail_position;
      interp->cur_file = saved_file;
      interp->cur_line = saved_line;
      interp->depth--;

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      break;
    }

    if (rc != LCL_RC_OK) {
      if (!interp->err_file) {
        interp->err_file = pr->file ? strdup(pr->file) : NULL;
        interp->err_file_owned = pr->file ? 1 : 0;

        if (!interp->err_line) {
          interp->err_line = cmd->line;
        }
      }
      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->cur_file = saved_file;
  interp->cur_line = saved_line;
  interp->depth--;

  if (out) {
    *out = last;

    /* Fuzz: A program with no commands (`[]`, an empty proc body) —
     * or a bare `return` — yields no value. Normalize to the empty
     * string here, at the single producer, so NULL never enters
     * bindings, argv arrays, or concatenation. TAILCALL is excluded:
     * its NULL result is internal and unwound before user code sees
     * it. */
    if (*out == NULL && (rc == LCL_RC_OK || rc == LCL_RC_RETURN)) {
      *out = lcl_string_new("");

      if (*out == NULL) {
        LCL_ERR_MSG(interp, "out of memory");
        rc = LCL_RC_ERR;
      }
    }
  } else if (last) {
    lcl_ref_dec(last);
  }

  return rc;
}
