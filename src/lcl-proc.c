#include <memory.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-values.h"
#include "lcl-lex.h"
#include "str-compat.h"

/* ============================================================================
 * Free Variable Extraction
 * ============================================================================ */

/* Simple string set for deduplication */
typedef struct {
  char **names;
  int count;
  int cap;
} name_set;

static void name_set_init(name_set *s) {
  s->names = NULL;
  s->count = 0;
  s->cap = 0;
}

static void name_set_free(name_set *s) {
  int i;
  for (i = 0; i < s->count; i++) {
    free(s->names[i]);
  }
  free(s->names);
}

static int name_set_contains(name_set *s, const char *name) {
  int i;
  for (i = 0; i < s->count; i++) {
    if (strcmp(s->names[i], name) == 0) return 1;
  }
  return 0;
}

static int name_set_add(name_set *s, const char *name) {
  char *copy;
  if (name_set_contains(s, name)) return 1; /* Already present */

  if (s->count >= s->cap) {
    int newcap = s->cap ? s->cap * 2 : 8;
    char **newnames = realloc(s->names, (size_t)newcap * sizeof(char *));
    if (!newnames) return 0;
    s->names = newnames;
    s->cap = newcap;
  }

  copy = strdup(name);
  if (!copy) return 0;

  s->names[s->count++] = copy;
  return 1;
}

/* Forward declaration */
static void collect_free_vars_program(const lcl_program *prog, name_set *vars);

static void collect_free_vars_word(const lcl_word *w, name_set *vars) {
  int i;
  if (!w) return;

  for (i = 0; i < w->np; i++) {
    lcl_word_piece *wp = &w->wp[i];
    switch (wp->kind) {
    case LCL_WP_VAR:
      name_set_add(vars, wp->as.var.name);
      break;
    case LCL_WP_SUBCMD:
      collect_free_vars_program(wp->as.sub.program, vars);
      break;
    case LCL_WP_LIT:
      /* Literals don't reference variables */
      break;
    }
  }
}

static void collect_free_vars_program(const lcl_program *prog, name_set *vars) {
  int i, j;
  if (!prog) return;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    for (j = 0; j < cmd->argc; j++) {
      collect_free_vars_word(&cmd->w[j], vars);
    }
  }
}

/* Build upvalues by capturing referenced variables from current environment.
 * params_list: list of parameter names (to exclude from capture)
 * Returns array of upvalues, sets *nout to count. Returns NULL on error. */
lcl_upvalue *lcl_build_upvalues(lcl_interp *interp, const lcl_program *body,
                                 lcl_value *params_list, int *nout) {
  name_set vars;
  lcl_upvalue *upvals = NULL;
  int i, j, nupvals = 0;

  name_set_init(&vars);
  *nout = 0;

  /* Collect all variable references from the body */
  collect_free_vars_program(body, &vars);

  if (vars.count == 0) {
    name_set_free(&vars);
    return NULL; /* No upvalues needed */
  }

  /* Allocate upvalues array (may be larger than needed) */
  upvals = calloc((size_t)vars.count, sizeof(lcl_upvalue));
  if (!upvals) {
    name_set_free(&vars);
    return NULL;
  }

  /* For each collected name, try to capture it */
  for (i = 0; i < vars.count; i++) {
    const char *name = vars.names[i];
    lcl_value *val = NULL;
    int is_param = 0;

    /* Skip if it's a parameter name */
    if (params_list) {
      int plen = (int)lcl_list_len(params_list);
      for (j = 0; j < plen; j++) {
        lcl_value *pname = NULL;
        if (lcl_list_get(params_list, (size_t)j, &pname) == LCL_OK) {
          if (strcmp(lcl_value_to_string(pname), name) == 0) {
            is_param = 1;
          }
          lcl_ref_dec(pname);
          if (is_param) break;
        }
      }
    }
    if (is_param) continue;

    /* Try to look up the variable in current environment */
    if (lcl_env_get_value(&interp->env, name, &val) == LCL_OK) {
      upvals[nupvals].name = strdup(name);
      if (!upvals[nupvals].name) {
        lcl_ref_dec(val);
        goto error;
      }

      if (val->type == LCL_CELL) {
        /* Capture the cell itself (for mutable variables) */
        upvals[nupvals].is_cell = 1;
        upvals[nupvals].value = val; /* Already incref'd by get_value */
      } else {
        /* Capture the value directly (for immutable let bindings) */
        upvals[nupvals].is_cell = 0;
        upvals[nupvals].value = val; /* Already incref'd by get_value */
      }
      nupvals++;
    }
    /* If not found, skip it - will be looked up dynamically (globals, etc.) */
  }

  name_set_free(&vars);

  /* Shrink array if we captured fewer than collected */
  if (nupvals == 0) {
    free(upvals);
    *nout = 0;
    return NULL;
  }

  *nout = nupvals;
  return upvals;

error:
  /* Clean up on error */
  for (j = 0; j < nupvals; j++) {
    free(upvals[j].name);
    lcl_ref_dec(upvals[j].value);
  }
  free(upvals);
  name_set_free(&vars);
  return NULL;
}

/* ============================================================================
 * Proc Creation
 * ============================================================================ */

lcl_value *lcl_proc_new(lcl_upvalue *upvals, int nupvals,
                        lcl_value *params, lcl_program *body) {
  lcl_proc *p = (lcl_proc *)calloc(1, sizeof(*p));
  lcl_value *v;

  if (!p) return NULL;

  /* Store upvalues (already have incremented refcounts from caller) */
  p->upvals = upvals;
  p->nupvals = nupvals;
  p->params = lcl_ref_inc(params);
  p->body = body;
  p->capture_ns = 0;
  p->captured_ns = NULL;

  v = (lcl_value *)calloc(1, sizeof(*v));
  if (!v) {
    /* Clean up upvalues on failure */
    int i;
    for (i = 0; i < nupvals; i++) {
      free(upvals[i].name);
      lcl_ref_dec(upvals[i].value);
    }
    free(upvals);
    lcl_ref_dec(p->params);
    lcl_program_free(p->body);
    free(p);
    return NULL;
  }

  v->type = LCL_PROC;
  v->refc = 1;
  v->as.procedure.proc = p;

  return v;
}

lcl_value *lcl_c_proc_new(const char *name, lcl_c_proc_fn fn) {
  lcl_value *proc = (lcl_value *)calloc(1, sizeof(*proc));
  lcl_c_func *func;
  char *name_copy;

  if (!proc) {
    return NULL;
  }

  func = (lcl_c_func *)calloc(1, sizeof(*func));
  if (!func) {
    free(proc);
    return NULL;
  }

  name_copy = strndup(name, strlen(name));
  if (!name_copy) {
    free(func);
    free(proc);
    return NULL;
  }

  func->kind = LCL_CK_PROC;
  func->name = "";
  func->fn.proc = fn;

  proc->type = LCL_CPROC;
  proc->refc = 1;
  proc->str_repr = name_copy;
  proc->as.c_proc.fn = func;

  return proc;
}

lcl_value *lcl_c_spec_new(const char *name, lcl_c_spec_fn fn) {
  lcl_value *proc = (lcl_value *)calloc(1, sizeof(*proc));
  lcl_c_func *func;
  char *name_copy;

  if (!proc) {
    return NULL;
  }

  func = (lcl_c_func *)calloc(1, sizeof(*func));
  if (!func) {
    free(proc);
    return NULL;
  }

  name_copy = strndup(name, strlen(name));
  if (!name_copy) {
    free(func);
    free(proc);
    return NULL;
  }

  func->kind = LCL_CK_SPECIAL;
  func->name = "";
  func->fn.spec = fn;

  proc->type = LCL_CPROC;
  proc->refc = 1;
  proc->str_repr = name_copy;
  proc->as.c_proc.fn = func;

  return proc;
}

