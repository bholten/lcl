#include <memory.h>
#include <stdlib.h>

#include "lcl-compile.h"
#include "lcl-values.h"
#include "str-compat.h"

#define MAX_DEPTH 1024

lcl_interp *lcl_interp_new(void) {
  lcl_interp *interp = (lcl_interp *)calloc(1, sizeof(*interp));
  lcl_env *env = NULL;

  if (!interp) {
    return NULL;
  }

  env = lcl_env_new();

  if (!env) {
    free(interp);
    return NULL;
  }

  interp->env = *env;
  free(env);
  interp->last = NULL;
  interp->cur_file = NULL;
  interp->cur_line = 0;
  interp->err_msg = NULL;
  interp->err_msg_owned = 0;
  interp->err_file = NULL;
  interp->err_file_owned = 0;
  interp->err_line = 0;
  interp->depth = 0;
  interp->max_depth = MAX_DEPTH;
  interp->pending_tail.argv = NULL;
  interp->pending_tail.argc = 0;
  interp->pending_tail.valid = 0;
  interp->in_tail_position = 0;
  interp->require_roots = NULL;
  interp->require_roots_len = 0;
  interp->require_roots_cap = 0;
  interp->require_stack = NULL;
  interp->require_stack_len = 0;
  interp->require_stack_cap = 0;
  interp->module_key_fn = NULL;
  interp->module_key_ud = NULL;
  interp->step_fn = NULL;
  interp->step_ud = NULL;
  interp->step_interval = 0;
  interp->step_countdown = 0;
  interp->interrupted = 0;

  return interp;
}

static void clear_pending_tail(lcl_interp *interp) {
  int i;
  if (!interp->pending_tail.valid) {
    return;
  }
  for (i = 0; i < interp->pending_tail.argc; i++) {
    lcl_ref_dec(interp->pending_tail.argv[i]);
  }
  free(interp->pending_tail.argv);
  interp->pending_tail.argv = NULL;
  interp->pending_tail.argc = 0;
  interp->pending_tail.valid = 0;
}

void lcl_interp_free(lcl_interp *interp) {
  if (!interp) {
    return;
  }

  lcl_ref_dec(interp->last);
  LCL_ERR_CLEAR(interp);
  clear_pending_tail(interp);

  /* Bugfix: Clear frame contents first to break circular references
   * (procs in frame have closures that reference the frame) */
  lcl_frame_clear(interp->env.frame);
  lcl_frame_ref_dec(interp->env.frame);

  lcl_ref_dec(interp->env.current_ns);
  lcl_ref_dec(interp->env.global_ns);
  lcl_ref_dec(interp->require_cache);

  {
    size_t i;

    for (i = 0; i < interp->require_roots_len; i++) {
      free(interp->require_roots[i]);
    }

    free(interp->require_roots);

    for (i = 0; i < interp->require_stack_len; i++) {
      free(interp->require_stack[i].key);
      free(interp->require_stack[i].path);
    }

    free(interp->require_stack);
  }

  free(interp);
}

void lcl_interp_set_user_data(lcl_interp *interp, void *data) {
  if (interp) {
    interp->user_data = data;
  }
}

void *lcl_interp_get_user_data(lcl_interp *interp) {
  return interp ? interp->user_data : NULL;
}

void lcl_add_require_root(lcl_interp *interp, const char *dir) {
  char *copy;

  if (!interp || !dir) {
    return;
  }

  copy = strdup(dir);

  if (!copy) {
    return;
  }

  if (interp->require_roots_len == interp->require_roots_cap) {
    size_t cap = interp->require_roots_cap ? interp->require_roots_cap * 2 : 4;
    char **grown =
        (char **)realloc(interp->require_roots, cap * sizeof(*grown));

    if (!grown) {
      free(copy);
      return;
    }

    interp->require_roots = grown;
    interp->require_roots_cap = cap;
  }

  interp->require_roots[interp->require_roots_len++] = copy;
}

void lcl_set_step_hook(lcl_interp *interp,
                       int (*fn)(lcl_interp *interp, void *userdata),
                       void *userdata, unsigned long interval) {
  if (!interp) {
    return;
  }

  interp->step_fn = fn;
  interp->step_ud = userdata;
  interp->step_interval = interval ? interval : 1;
  interp->step_countdown = interp->step_interval;
}

void lcl_set_module_key_fn(lcl_interp *interp,
                           char *(*fn)(const char *lexical_path,
                                       void *userdata),
                           void *userdata) {
  if (!interp) {
    return;
  }

  interp->module_key_fn = fn;
  interp->module_key_ud = userdata;
}
