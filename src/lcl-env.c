#include <stdio.h>
#include <string.h>
#include "hash-table.h"
#include "lcl-compile.h"
#include "lcl-values.h"

void lcl_env_free(lcl_env *env) {
  if (!env) return;

  if (env->frame) {
    lcl_frame_ref_dec(env->frame);
  }

  if (env->current_ns) {
    lcl_ref_dec(env->current_ns);
  }

  if (env->global_ns) {
    lcl_ref_dec(env->global_ns);
  }

  free(env);
}

lcl_env *lcl_env_new(void) {
  lcl_env *env = (lcl_env *)calloc(1, sizeof(*env));

  if (!env) {
    return NULL;
  }

  env->frame = lcl_frame_new(NULL);

  if (!env->frame) {
    free(env);
    return NULL;
  }

  env->global_ns = lcl_ns_new("global");
  env->current_ns = lcl_ref_inc(env->global_ns);

  if (!env->global_ns || !env->current_ns) {
    lcl_env_free(env);
    return NULL;
  }

  return env;
}

lcl_result lcl_env_let(lcl_env *env, const char *name, lcl_value *value) {
  if (!env || !env->frame) return LCL_ERROR;

  if (!hash_table_put(env->frame->locals, name, value)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

lcl_result lcl_env_let_take(lcl_env *env, const char *name, lcl_value *value) {
  lcl_result r = lcl_env_let(env, name, value);
  lcl_ref_dec(value);
  return r;
}

lcl_result lcl_env_var(lcl_env *env, const char *name, lcl_value *value) {
  if (!env || !env->frame) return LCL_ERROR;

  {
    lcl_result r;
    lcl_value *cell = lcl_cell_new(value);

    if (!cell) return LCL_ERROR;

    r = hash_table_put(env->frame->locals, name, cell) ? LCL_RC_OK : LCL_ERROR;

    lcl_ref_dec(cell);

    return r;
  }
}

/* Simple lookup without qualified names */
static lcl_result env_get_simple(lcl_env *env, const char *key, lcl_value **out) {
  lcl_value *b = NULL;

  if (lcl_frame_get_binding(env->frame, key, &b)) {
    *out = b;
    return LCL_OK;
  }

  if (env->current_ns && lcl_ns_get(env->current_ns, key, out) == LCL_OK) {
    return LCL_OK;
  }

  if (env->global_ns && lcl_ns_get(env->global_ns, key, out) == LCL_OK) {
    return LCL_OK;
  }

  return LCL_ERROR;
}

/* Lookup for command names - supports qualified names */
lcl_result lcl_env_get_command(lcl_env *env, const char *key, lcl_value **out) {
  if (!env || !out) return LCL_ERROR;
  return lcl_env_get_value(env, key, out);
}

lcl_result lcl_env_get_value(lcl_env *env, const char *key, lcl_value **out) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  if (!env || !out) return LCL_ERROR;

  /* First try direct lookup (handles command names containing ::) */
  if (env_get_simple(env, key, out) == LCL_OK) {
    return LCL_OK;
  }

  /* Check for qualified name (contains ::) */
  if (!lcl_ns_split(key, first, sizeof(first), &rest)) {
    /* No ::, simple lookup already failed */
    return LCL_ERROR;
  }

  /* Look up first part in env */
  if (env_get_simple(env, first, &current) != LCL_OK) {
    return LCL_ERROR;
  }

  /* Walk through qualified path */
  while (rest && *rest) {
    lcl_value *next = NULL;
    char part[256];
    const char *next_rest = NULL;

    if (current->type != LCL_NAMESPACE) {
      lcl_ref_dec(current);
      return LCL_ERROR;
    }

    /* Try to split rest into part::next_rest */
    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      if (lcl_ns_get(current, part, &next) != LCL_OK) {
        lcl_ref_dec(current);
        return LCL_ERROR;
      }
      lcl_ref_dec(current);
      current = next;
      rest = next_rest;
    } else {
      /* rest is the final part */
      if (lcl_ns_get(current, rest, &next) != LCL_OK) {
        lcl_ref_dec(current);
        return LCL_ERROR;
      }
      lcl_ref_dec(current);
      *out = next;
      return LCL_OK;
    }
  }

  *out = current;
  return LCL_OK;
}

lcl_result lcl_env_set_bang(lcl_env *env, const char *name, lcl_value *value) {
  if (!env) return LCL_ERROR;

  {
    lcl_frame *f = env->frame;

    while (f) {
      lcl_value *b = NULL;

      if (hash_table_get(f->locals, name, &b)) {
        if (b->type == LCL_CELL) {
          lcl_result r = lcl_cell_set(b, value);
          lcl_ref_dec(b);
          return r;
        }

        lcl_ref_dec(b);
        return LCL_ERROR;
      }

      f = f->parent;
    }

    return LCL_ERROR;
  }
}
