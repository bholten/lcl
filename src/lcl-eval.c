#include <stdio.h>
#include <string.h>

#include "lcl-eval.h"
#include "lcl-compile.h"
#include "lcl-lex.h"
#include "lcl-values.h"

/* Forward declaration */
int lcl_eval_word(lcl_interp *interp, const lcl_word *w,
                  lcl_value **out);

static int build_argv(lcl_interp *interp, const lcl_command *cmd, int *argc_out,
                      lcl_value ***argv_out) {
  int i;
  int argc = cmd->argc - 1;
  int rc;
  lcl_value **argv;

  if (argc < 0) {
    *argc_out = 0;
    *argv_out = NULL;
    return LCL_RC_OK;
  }
  
  argv = (lcl_value **)calloc((size_t)argc, sizeof(*argv));
  
  if (!argv) return LCL_RC_ERR;
  
  for (i = 0; i < argc; i++) {
    rc = lcl_eval_word(interp, &cmd->w[i + 1], &argv[i]);
    
    if (rc != LCL_RC_OK) {
      while (i--) {
        lcl_ref_dec(argv[i]);
      }
      
      free(argv);

      return rc;
    }
  }
  
  *argc_out = argc;
  *argv_out = argv;
  
  return LCL_RC_OK;  
}

int lcl_call_user_proc(lcl_interp *interp, lcl_proc *p,
                       int argc, lcl_value **argv, lcl_value **out) {
  int i;
  int rc;
  lcl_frame *child = lcl_frame_new(p->closure);
  lcl_env saved = interp->env;

  interp->env.frame = child;
  if (p->capture_ns && p->captured_ns) {
    if (interp->env.current_ns) {
      lcl_ref_dec(interp->env.current_ns);
    }
    
    interp->env.current_ns = lcl_ref_inc(p->captured_ns);
  }

  if ((int)lcl_list_len(p->params) != argc) {
    interp->env = saved;
    lcl_frame_ref_dec(child);
    
    return LCL_RC_ERR;
  }
  
  for (i = 0; i < argc; i++) {
    lcl_value *nameV = NULL;
    const char *pname;

    lcl_list_get(p->params, i, &nameV);
    pname = lcl_value_to_string(nameV);
    lcl_env_let(&interp->env, pname, argv[i]);
    lcl_ref_dec(nameV);
  }

  rc = lcl_eval_program(interp, (lcl_program *)p->body, out);

  if (rc == LCL_RC_RETURN) {
    rc = LCL_RC_OK;
  }    

  interp->env = saved;
  lcl_frame_ref_dec(child);
  
  return rc;
}

/* Evaluate a word and return the value directly (not forced to string) */
int lcl_eval_word(lcl_interp *interp, const lcl_word *w,
                  lcl_value **out) {
  /* Handle single-piece words specially to preserve value types */
  if (w && w->np == 1) {
    lcl_word_piece *wp = &w->wp[0];

    switch (wp->kind) {
    case LCL_WP_VAR: {
      lcl_value *val = NULL;
      if (lcl_env_get_value(&interp->env, wp->as.var.name, &val) != LCL_OK) {
        return LCL_RC_ERR;
      }
      /* Unwrap cell if needed */
      if (val->type == LCL_CELL) {
        lcl_value *inner = NULL;
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
      /* Evaluate subcommand and return result directly */
      return lcl_eval_program(interp, wp->as.sub.program, out);
    }
    case LCL_WP_LIT:
      /* Fall through to string evaluation for literals */
      break;
    }
  }
  /* Multi-piece words or literals need string evaluation */
  return lcl_eval_word_to_str(interp, w, out);
}

int lcl_call_from_words(lcl_interp *interp, const lcl_command *cmd,
                        lcl_value **out) {
  lcl_value *callee = NULL;
  int rc;

  if (cmd->argc == 0) {
    *out = lcl_value_new_string("");

    return LCL_RC_OK;
  }

  /* Evaluate first word to get command/callee value */
  rc = lcl_eval_word(interp, &cmd->w[0], &callee);
  if (rc != LCL_RC_OK) return rc;

  /* Look up command by name if result is a string or convertible to one */
  if (callee->type == LCL_STRING) {
    lcl_value *name = callee;
    callee = NULL;
    if (lcl_env_get_command(&interp->env, lcl_value_to_string(name), &callee) != LCL_OK) {
      /* If lookup fails and this is a single-word command, return the value itself */
      if (cmd->argc == 1) {
        *out = name;
        return LCL_RC_OK;
      }
      lcl_ref_dec(name);
      return LCL_RC_ERR;
    }
    lcl_ref_dec(name);
    /* Check if the looked-up value is callable; if not and single-word, return it */
    if (callee->type != LCL_PROC && callee->type != LCL_CPROC) {
      if (cmd->argc == 1) {
        *out = callee;
        return LCL_RC_OK;
      }
      /* Non-callable with args - error */
      lcl_ref_dec(callee);
      return LCL_RC_ERR;
    }
  } else if (callee->type != LCL_PROC && callee->type != LCL_CPROC) {
    /* Non-callable value - for single-word command, return the value itself */
    if (cmd->argc == 1) {
      *out = callee;
      return LCL_RC_OK;
    }
    /* Otherwise try to look it up as a command name */
    {
      lcl_value *name = callee;
      callee = NULL;
      if (lcl_env_get_command(&interp->env, lcl_value_to_string(name), &callee) != LCL_OK) {
        lcl_ref_dec(name);
        return LCL_RC_ERR;
      }
      lcl_ref_dec(name);
    }
  }

  if (callee->type == LCL_CPROC && callee->as.c_proc.fn->kind == LCL_CK_SPECIAL) {
    int spec_argc = cmd->argc - 1;
    const lcl_word **raw = NULL;
    int i;

    if (spec_argc > 0) {
      raw = (const lcl_word **)malloc((size_t)spec_argc * sizeof(*raw));
      if (!raw) {
        lcl_ref_dec(callee);
        return LCL_RC_ERR;
      }
      for (i = 0; i < spec_argc; i++) {
        raw[i] = &cmd->w[i + 1];
      }
    }

    rc = callee->as.c_proc.fn->fn.spec(interp, spec_argc, raw, out);
    free(raw);
    lcl_ref_dec(callee);

    return rc;
  }

  {
    int argc = 0;
    int i;
    lcl_value **argv = NULL;
    rc = build_argv(interp, cmd, &argc, &argv);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(callee);
      
      return rc;
    }

    if (callee->type == LCL_CPROC) {
      rc = callee->as.c_proc.fn->fn.proc(interp, argc, argv, out);
    } else if (callee->type == LCL_PROC) {
      rc = lcl_call_user_proc(interp, (lcl_proc *)callee->as.procedure.proc,
                              argc, argv, out);
    } else {
      rc = LCL_RC_ERR;
    }

    for (i = 0; i < argc; i++) lcl_ref_dec(argv[i]);
    free(argv);
    lcl_ref_dec(callee);
    
    return rc;
  }
}

int lcl_eval_string(lcl_interp *interp, const char *src, lcl_value **out) {
  lcl_program *P = lcl_program_compile(src, "<string>");
  int rc;

  if (!P) return LCL_RC_ERR;

  rc = lcl_eval_program(interp, P, out);
  lcl_program_free(P);

  return rc;
}

int lcl_eval_word_to_str(lcl_interp *interp,
                         const lcl_word *w,
                         lcl_value **out) {
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  int i;

  if (!w || w->np == 0) {
    *out = lcl_value_new_string("");
    return *out ? LCL_RC_OK : LCL_RC_ERR;
  }

  /* Fast path: single literal piece */
  if (w->np == 1 && w->wp[0].kind == LCL_WP_LIT) {
    char *s = (char *)malloc(w->wp[0].as.lit.n + 1);
    if (!s) return LCL_RC_ERR;
    memcpy(s, w->wp[0].as.lit.s, w->wp[0].as.lit.n);
    s[w->wp[0].as.lit.n] = '\0';
    *out = lcl_value_new_string(s);
    free(s);
    return *out ? LCL_RC_OK : LCL_RC_ERR;
  }

  /* Build string from pieces */
  for (i = 0; i < w->np; i++) {
    lcl_word_piece *wp = &w->wp[i];

    switch (wp->kind) {
    case LCL_WP_LIT: {
      size_t need = len + wp->as.lit.n + 1;
      if (need > cap) {
        size_t newcap = cap ? cap * 2 : 64;
        char *newbuf;
        while (newcap < need) newcap *= 2;
        newbuf = (char *)realloc(buf, newcap);
        if (!newbuf) { free(buf); return LCL_RC_ERR; }
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

      if (lcl_env_get_value(&interp->env, wp->as.var.name, &val) != LCL_OK) {
        free(buf);
        return LCL_RC_ERR;
      }

      /* Unwrap cell if needed */
      if (val->type == LCL_CELL) {
        lcl_value *inner = NULL;
        if (lcl_cell_get(val, &inner) != LCL_OK) {
          lcl_ref_dec(val);
          free(buf);
          return LCL_RC_ERR;
        }
        lcl_ref_dec(val);
        val = inner;
      }

      s = lcl_value_to_string(val);
      slen = strlen(s);
      need = len + slen + 1;

      if (need > cap) {
        size_t newcap = cap ? cap * 2 : 64;
        char *newbuf;
        while (newcap < need) newcap *= 2;
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
      int rc = lcl_eval_program(interp, wp->as.sub.program, &result);

      if (rc != LCL_RC_OK) {
        free(buf);
        return rc;
      }

      s = lcl_value_to_string(result);
      slen = strlen(s);
      need = len + slen + 1;

      if (need > cap) {
        size_t newcap = cap ? cap * 2 : 64;
        char *newbuf;
        while (newcap < need) newcap *= 2;
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

  /* Null-terminate */
  if (len == 0) {
    free(buf);
    *out = lcl_value_new_string("");
  } else {
    if (len >= cap) {
      char *newbuf = (char *)realloc(buf, len + 1);
      if (!newbuf) { free(buf); return LCL_RC_ERR; }
      buf = newbuf;
    }
    buf[len] = '\0';
    *out = lcl_value_new_string(buf);
    free(buf);
  }

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

lcl_return_code lcl_call(lcl_interp *interp, const lcl_command *command,
                         lcl_value **out) {
  /* Stub function - use lcl_call_from_words instead */
  (void)interp;
  (void)command;
  (void)out;
  return LCL_RC_ERR;
}

int lcl_eval_program(lcl_interp *interp, const lcl_program *pr,
                     lcl_value **out) {
  int i;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    return LCL_RC_ERR;
  }

  interp->depth++;

  for (i = 0; i < pr->ncmd; i++) {
    lcl_command *cmd = &pr->cmd[i];

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_RETURN) {
      rc = LCL_RC_OK;
      break;
    }

    if (rc != LCL_RC_OK) {
      interp->err_line = cmd->line;
      interp->err_file = pr->file;
      break;
    }
  }

  interp->depth--;

  if (out) {
    *out = last;
  }
  else if (last) {
    lcl_ref_dec(last);
  }  
  
  return rc;
}
