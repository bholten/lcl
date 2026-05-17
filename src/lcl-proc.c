#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-lex.h"
#include "lcl-values.h"
#include "str-compat.h"

typedef struct {
  char **names;
  int count;
  int cap;
} name_set;

static void name_set_init(name_set *s);
static void name_set_free(name_set *s);
static int name_set_contains(name_set *s, const char *name);
static int name_set_add(name_set *s, const char *name);

void lcl_param_spec_free(lcl_param_spec *pspec) {
  int i;
  int total;

  if (!pspec) {
    return;
  }

  total = pspec->n_required + pspec->n_optional;

  for (i = 0; i < total; i++) {
    free(pspec->params[i].name);
    if (pspec->params[i].def_prog) {
      lcl_program_free(pspec->params[i].def_prog);
    }
  }

  free(pspec->params);
  free(pspec->rest_name);

  pspec->params = NULL;
  pspec->n_required = 0;
  pspec->n_optional = 0;
  pspec->rest_name = NULL;
}

static void skip_ws(const char **p) {
  while (**p && isspace((unsigned char)**p)) {
    (*p)++;
  }
}

static char *extract_param_token(const char **p) {
  const char *start;
  size_t len;
  char *result;

  skip_ws(p);

  if (!**p) {
    return NULL;
  }

  start = *p;

  if (**p == '(') {
    int depth = 1;
    (*p)++;
    while (**p && depth > 0) {
      if (**p == '(') {
        depth++;
      } else if (**p == ')') {
        depth--;
      } else if (**p == '{') {
        int bd = 1;
        (*p)++;

        while (**p && bd > 0) {
          if (**p == '{') {
            bd++;
          } else if (**p == '}') {
            bd--;
          }
          (*p)++;
        }
        continue;
      } else if (**p == '"') {
        (*p)++;

        while (**p && **p != '"') {
          if (**p == '\\' && *(*p + 1)) {
            (*p)++;
          }
          (*p)++;
        }

        if (**p == '"') {
          (*p)++;
        }

        continue;
      }
      (*p)++;
    }

    if (depth != 0) {
      return NULL;
    }

    len = (size_t)(*p - start);
  } else {
    while (**p && !isspace((unsigned char)**p)) {
      (*p)++;
    }
    len = (size_t)(*p - start);
  }

  result = (char *)malloc(len + 1);

  if (!result) {
    return NULL;
  }

  memcpy(result, start, len);
  result[len] = '\0';
  return result;
}

static lcl_result parse_optional_param(const char *token, char **name_out,
                                       char **default_out) {
  const char *p = token;
  const char *name_start;
  const char *name_end;
  const char *default_start;
  const char *default_end;

  *name_out = NULL;
  *default_out = NULL;

  if (*p != '(') {
    return LCL_ERROR;
  }
  p++;

  while (*p && isspace((unsigned char)*p)) {
    p++;
  }

  name_start = p;

  while (*p && !isspace((unsigned char)*p) && *p != ')') {
    p++;
  }
  name_end = p;

  if (name_start == name_end) {
    return LCL_ERROR;
  }

  while (*p && isspace((unsigned char)*p)) {
    p++;
  }

  default_start = p;

  {
    int depth = 0;
    while (*p) {
      if (*p == '(' || *p == '{') {
        depth++;
      } else if (*p == ')' || *p == '}') {
        if (depth > 0) {
          depth--;
        } else {
          break;
        }
      } else if (*p == '"') {
        p++;

        while (*p && *p != '"') {
          if (*p == '\\' && *(p + 1)) {
            p++;
          }
          p++;
        }
      }
      p++;
    }
  }

  default_end = p;

  while (default_end > default_start &&
         isspace((unsigned char)*(default_end - 1))) {
    default_end--;
  }

  *name_out = (char *)malloc((size_t)(name_end - name_start) + 1);

  if (!*name_out) {
    return LCL_ERROR;
  }
  memcpy(*name_out, name_start, (size_t)(name_end - name_start));
  (*name_out)[name_end - name_start] = '\0';

  *default_out = (char *)malloc((size_t)(default_end - default_start) + 1);

  if (!*default_out) {
    free(*name_out);
    *name_out = NULL;
    return LCL_ERROR;
  }

  memcpy(*default_out, default_start, (size_t)(default_end - default_start));
  (*default_out)[default_end - default_start] = '\0';

  return LCL_OK;
}

int lcl_parse_params(lcl_interp *interp, const char *param_str,
                     lcl_param_spec *pspec) {
  const char *p = param_str;
  char *token;
  char **req_names = NULL;
  int n_req = 0;
  int req_cap = 0;

  char **opt_names = NULL;
  char **opt_defaults = NULL;
  int n_opt = 0;
  int opt_cap = 0;

  char *rest = NULL;

  int saw_optional = 0;
  int saw_rest = 0;
  int i;

  pspec->params = NULL;
  pspec->n_required = 0;
  pspec->n_optional = 0;
  pspec->rest_name = NULL;

  while ((token = extract_param_token(&p)) != NULL) {
    if (token[0] == '*') {
      if (saw_rest) {
        LCL_ERR_MSG(interp, "duplicate rest parameter");
        free(token);
        goto error;
      }

      if (strlen(token) < 2) {
        LCL_ERR_MSG(interp, "rest parameter requires a name after *");
        free(token);
        goto error;
      }

      /* Skip the * */
      rest = strdup(token + 1);
      free(token);

      if (!rest) {
        LCL_ERR_MSG(interp, "out of memory");
        goto error;
      }

      saw_rest = 1;
    } else if (token[0] == '(') {
      char *opt_name = NULL;
      char *opt_default = NULL;

      if (saw_rest) {
        LCL_ERR_MSG(interp, "optional parameter after rest parameter");
        free(token);
        goto error;
      }

      if (parse_optional_param(token, &opt_name, &opt_default) != LCL_OK) {
        LCL_ERR_MSG(interp, "invalid optional parameter syntax");
        free(token);
        goto error;
      }
      free(token);

      if (n_opt >= opt_cap) {
        int newcap = opt_cap ? opt_cap * 2 : 4;
        char **new_names = realloc(opt_names, (size_t)newcap * sizeof(char *));
        char **new_defs =
            realloc(opt_defaults, (size_t)newcap * sizeof(char *));

        if (!new_names || !new_defs) {
          free(opt_name);
          free(opt_default);
          free(new_names);
          free(new_defs);
          LCL_ERR_MSG(interp, "out of memory");
          goto error;
        }

        opt_names = new_names;
        opt_defaults = new_defs;
        opt_cap = newcap;
      }

      opt_names[n_opt] = opt_name;
      opt_defaults[n_opt] = opt_default;
      n_opt++;
      saw_optional = 1;
    } else {
      if (saw_optional) {
        LCL_ERR_MSG(interp, "required parameter after optional parameter");
        free(token);
        goto error;
      }

      if (saw_rest) {
        LCL_ERR_MSG(interp, "required parameter after rest parameter");
        free(token);
        goto error;
      }

      if (n_req >= req_cap) {
        int newcap = req_cap ? req_cap * 2 : 4;
        char **new_names = realloc(req_names, (size_t)newcap * sizeof(char *));
        if (!new_names) {
          free(token);
          LCL_ERR_MSG(interp, "out of memory");
          goto error;
        }

        req_names = new_names;
        req_cap = newcap;
      }

      req_names[n_req++] = token;
    }
  }

  {
    name_set seen;
    name_set_init(&seen);

    for (i = 0; i < n_req; i++) {
      if (name_set_contains(&seen, req_names[i])) {
        LCL_ERR_MSG(interp, "duplicate parameter name");
        name_set_free(&seen);
        goto error;
      }
      name_set_add(&seen, req_names[i]);
    }

    for (i = 0; i < n_opt; i++) {
      if (name_set_contains(&seen, opt_names[i])) {
        LCL_ERR_MSG(interp, "duplicate parameter name");
        name_set_free(&seen);
        goto error;
      }
      name_set_add(&seen, opt_names[i]);
    }

    if (rest && name_set_contains(&seen, rest)) {
      LCL_ERR_MSG(interp, "duplicate parameter name");
      name_set_free(&seen);
      goto error;
    }

    name_set_free(&seen);
  }

  pspec->n_required = n_req;
  pspec->n_optional = n_opt;
  pspec->rest_name = rest;

  if (n_req + n_opt > 0) {
    pspec->params =
        (lcl_param *)calloc((size_t)(n_req + n_opt), sizeof(lcl_param));

    if (!pspec->params) {
      LCL_ERR_MSG(interp, "out of memory");
      goto error;
    }

    for (i = 0; i < n_req; i++) {
      pspec->params[i].name = req_names[i];
      pspec->params[i].def_prog = NULL;
    }

    for (i = 0; i < n_opt; i++) {
      lcl_program *def_prog;

      pspec->params[n_req + i].name = opt_names[i];
      def_prog = lcl_program_compile(opt_defaults[i], "<default>");
      free(opt_defaults[i]);
      opt_defaults[i] = NULL;

      if (!def_prog) {
        LCL_ERR_MSG(interp, "failed to compile default expression");
        for (; i >= 0; i--) {
          if (pspec->params[n_req + i].def_prog) {
            lcl_program_free(pspec->params[n_req + i].def_prog);
          }
        }
        goto error;
      }
      pspec->params[n_req + i].def_prog = def_prog;
    }
  }

  free(req_names);
  free(opt_names);
  free(opt_defaults);

  return LCL_RC_OK;

error:
  for (i = 0; i < n_req; i++) {
    free(req_names[i]);
  }

  free(req_names);

  for (i = 0; i < n_opt; i++) {
    free(opt_names[i]);
    free(opt_defaults[i]);
  }

  free(opt_names);
  free(opt_defaults);
  free(rest);

  pspec->params = NULL;
  pspec->n_required = 0;
  pspec->n_optional = 0;
  pspec->rest_name = NULL;

  return LCL_RC_ERR;
}

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
    if (strcmp(s->names[i], name) == 0) {
      return 1;
    }
  }

  return 0;
}

static int name_set_add(name_set *s, const char *name) {
  char *copy;

  if (name_set_contains(s, name)) {
    return 1;
  }

  if (s->count >= s->cap) {
    int newcap = s->cap ? s->cap * 2 : 8;
    char **newnames = realloc(s->names, (size_t)newcap * sizeof(char *));

    if (!newnames) {
      return 0;
    }

    s->names = newnames;
    s->cap = newcap;
  }

  copy = strdup(name);

  if (!copy) {
    return 0;
  }

  s->names[s->count++] = copy;
  return 1;
}

static void collect_free_vars_program(const lcl_program *prog, name_set *vars);

static void collect_free_vars_word(const lcl_word *w, name_set *vars) {
  int i;

  if (!w) {
    return;
  }

  for (i = 0; i < w->np; i++) {
    lcl_word_piece *wp = &w->wp[i];
    switch (wp->kind) {
    case LCL_WP_VAR: name_set_add(vars, wp->as.var.name); break;
    case LCL_WP_SUBCMD:
      collect_free_vars_program(wp->as.sub.program, vars);
      break;
    case LCL_WP_LIT: break;
    }
  }
}

static int word_is_literal(const lcl_word *w, const char *s) {
  if (!w || w->np != 1) {
    return 0;
  }

  if (w->wp[0].kind != LCL_WP_LIT) {
    return 0;
  }

  return strcmp(w->wp[0].as.lit.s, s) == 0;
}

static const char *word_get_literal(const lcl_word *w) {
  if (!w || w->np != 1) {
    return NULL;
  }

  if (w->wp[0].kind != LCL_WP_LIT) {
    return NULL;
  }

  return w->wp[0].as.lit.s;
}

static void collect_free_vars_program(const lcl_program *prog, name_set *vars) {
  int i;
  int j;

  if (!prog) {
    return;
  }

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];

    /* Bugfix: Special case: set! command - capture the variable name
       being set */
    if (cmd->argc >= 2 && word_is_literal(&cmd->w[0], "set!")) {
      const char *var_name = word_get_literal(&cmd->w[1]);
      if (var_name) {
        name_set_add(vars, var_name);
      }
    }

    /* Bugfix:
     *
     * Capture command names (first word) - this allows procs to call
     * other procs by name within namespaces. If the name isn't in the
     * environment, the capture will simply be skipped later in
     * lcl_build_upvalues. */
    if (cmd->argc >= 1) {
      const char *cmd_name = word_get_literal(&cmd->w[0]);

      if (cmd_name) {
        name_set_add(vars, cmd_name);
      }
    }

    for (j = 0; j < cmd->argc; j++) {
      lcl_word *w = &cmd->w[j];
      collect_free_vars_word(w, vars);

      /* Scan pre-compiled braced bodies for variables. Braced words
       * are pre-compiled at parse time so the upvalue scanner can see
       * variables inside code bodies (eval, foreach, while, etc.). */
      if (w->compiled) {
        collect_free_vars_program(w->compiled, vars);
      }
    }
  }
}

static int is_param_name(const lcl_param_spec *pspec, const char *name) {
  int i;
  int total;

  if (!pspec) {
    return 0;
  }

  total = pspec->n_required + pspec->n_optional;
  for (i = 0; i < total; i++) {
    if (strcmp(pspec->params[i].name, name) == 0) {
      return 1;
    }
  }

  if (pspec->rest_name && strcmp(pspec->rest_name, name) == 0) {
    return 1;
  }

  return 0;
}

/* Build upvalues by capturing referenced variables from current environment.
 * pspec: parameter specification (names to exclude from capture)
 * self_name: name for self-reference (to exclude from capture), or NULL
 * upvals_out: receives the upvalues array (may be NULL if no captures needed)
 * nout: receives the count
 * Scans both the body and any default programs in pspec for free variables.
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error (with message set). */
int lcl_build_upvalues(lcl_interp *interp, const lcl_program *body,
                       const lcl_param_spec *pspec, const char *self_name,
                       lcl_upvalue **upvals_out, int *nout) {
  name_set vars;
  lcl_upvalue *upvals = NULL;
  int i;
  int j;
  int nupvals = 0;

  name_set_init(&vars);
  *nout = 0;
  *upvals_out = NULL;
  collect_free_vars_program(body, &vars);

  if (pspec) {
    for (i = 0; i < pspec->n_optional; i++) {
      int pidx = pspec->n_required + i;

      if (pspec->params[pidx].def_prog) {
        collect_free_vars_program(pspec->params[pidx].def_prog, &vars);
      }
    }
  }

  if (vars.count == 0) {
    name_set_free(&vars);
    return LCL_RC_OK;
  }

  upvals = calloc((size_t)vars.count, sizeof(lcl_upvalue));

  if (!upvals) {
    name_set_free(&vars);
    LCL_ERR_MSG(interp, "out of memory");

    return LCL_RC_ERR;
  }

  for (i = 0; i < vars.count; i++) {
    const char *name = vars.names[i];
    lcl_value *val = NULL;

    if (self_name && strcmp(name, self_name) == 0) {
      continue;
    }

    if (is_param_name(pspec, name)) {
      continue;
    }

    if (lcl_env_get_value(interp, name, &val) == LCL_OK) {
      upvals[nupvals].name = strdup(name);

      if (!upvals[nupvals].name) {
        lcl_ref_dec(val);
        LCL_ERR_MSG(interp, "out of memory");
        goto error;
      }

      if (val->type == LCL_CELL) {
        upvals[nupvals].is_cell = 1;
        upvals[nupvals].value = val;
      } else {
        upvals[nupvals].is_cell = 0;
        upvals[nupvals].value = val;
      }
      nupvals++;
    } else {
      /* Variable not found in current environment.
       * This could be either:
       *
       * 1. A forward reference (variable will be defined later in
       * outer scope)
       *
       * 2. A local variable that will be defined within this proc's
       * body We can't distinguish these at parse time, so we skip and
       * let runtime handle it. If it's a true forward reference,
       * runtime will error. */
    }
  }

  name_set_free(&vars);

  if (nupvals == 0) {
    free(upvals);
    *nout = 0;
    *upvals_out = NULL;
    return LCL_RC_OK;
  }

  *nout = nupvals;
  *upvals_out = upvals;
  return LCL_RC_OK;

error:
  for (j = 0; j < nupvals; j++) {
    free(upvals[j].name);
    lcl_ref_dec(upvals[j].value);
  }
  free(upvals);
  name_set_free(&vars);
  return LCL_RC_ERR;
}

lcl_value *lcl_proc_new(const char *self_name, lcl_upvalue *upvals, int nupvals,
                        lcl_param_spec *pspec, lcl_program *body) {
  lcl_proc *p = (lcl_proc *)calloc(1, sizeof(*p));
  lcl_value *v;

  if (!p) {
    goto error_early;
  }

  if (self_name) {
    p->self_name = strdup(self_name);

    if (!p->self_name) {
      free(p);
      goto error_early;
    }
  } else {
    p->self_name = NULL;
  }

  p->upvals = upvals;
  p->nupvals = nupvals;
  p->pspec = *pspec;
  pspec->params = NULL;
  pspec->n_required = 0;
  pspec->n_optional = 0;
  pspec->rest_name = NULL;

  p->body = body;

  v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    int i;

    for (i = 0; i < nupvals; i++) {
      free(upvals[i].name);
      lcl_ref_dec(upvals[i].value);
    }

    free(upvals);
    free(p->self_name);
    lcl_param_spec_free(&p->pspec);
    lcl_program_free(p->body);
    free(p);
    return NULL;
  }

  v->type = LCL_PROC;
  v->refc = 1;
  v->as.procedure.proc = p;

  return v;

error_early: {
  int i;
  for (i = 0; i < nupvals; i++) {
    free(upvals[i].name);
    lcl_ref_dec(upvals[i].value);
  }
  free(upvals);
  lcl_param_spec_free(pspec);
  lcl_program_free(body);
}
  return NULL;
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
