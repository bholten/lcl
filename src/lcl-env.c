#include "hash-table.h"
#include "lcl-compile.h"
#include "lcl-values.h"
#include <stdio.h>
#include <string.h>

void lcl_env_free(lcl_env *env) {
  if (!env) {
    return;
  }

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
  if (!env || !env->frame) {
    return LCL_ERROR;
  }

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
  if (!env || !env->frame) {
    return LCL_ERROR;
  }

  {
    lcl_result r;
    lcl_value *cell = lcl_cell_new(value);

    if (!cell) {
      return LCL_ERROR;
    }

    r = hash_table_put(env->frame->locals, name, cell) ? LCL_RC_OK : LCL_ERROR;

    lcl_ref_dec(cell);

    return r;
  }
}

static lcl_result env_get_simple(lcl_env *env, const char *key,
                                 lcl_value **out) {
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

lcl_result lcl_env_get_command(lcl_interp *interp, const char *key,
                               lcl_value **out) {
  if (!interp || !out) {
    return LCL_ERROR;
  }
  return lcl_env_get_value(interp, key, out);
}

/* find_def_target_for_self: walk the def_target stack from top to
 * def_lookup_floor, returning the topmost target whose name is a
 * "::"-bounded prefix of `key` and writing the un-consumed suffix
 * into *suffix_out.
 *
 * Used by lcl_env_get_value to resolve qualified self-references like
 * `$foo::X` (inside `namespace foo { ... }`, target name "foo") or
 * `$alpha::beta::X` (inside `namespace alpha::beta { ... }`, target
 * name "alpha::beta") while the namespace value itself isn't yet
 * bound. Exact-match-without-suffix never resolves, because there's
 * no namespace value to return — the partial build state isn't a
 * first-class value.
 *
 * Walks down to def_lookup_floor (not def_floor) so that a helper
 * proc called from inside `namespace foo { ... }` can still resolve
 * $foo::X — user-proc calls raise def_floor (to block writes) but
 * leave def_lookup_floor alone (so reads remain transparent through
 * the call). `isolate` raises both. */
static lcl_def_target *find_def_target_for_self(lcl_interp *interp,
                                                const char *key,
                                                const char **suffix_out) {
  int i;

  for (i = interp->def_depth - 1; i >= interp->def_lookup_floor; i--) {
    lcl_def_target *t = &interp->def_stack[i];
    size_t namelen;

    if (!t->name) {
      continue;
    }

    namelen = strlen(t->name);

    if (strncmp(key, t->name, namelen) == 0 && key[namelen] == ':' &&
        key[namelen + 1] == ':') {
      *suffix_out = key + namelen + 2;
      return t;
    }
  }

  return NULL;
}

lcl_result lcl_env_get_value(lcl_interp *interp, const char *key,
                             lcl_value **out) {
  lcl_env *env;
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  if (!interp || !out) {
    return LCL_ERROR;
  }

  env = &interp->env;

  if (env_get_simple(env, key, out) == LCL_OK) {
    return LCL_OK;
  }

  /* The key may be a qualified self-reference into an in-progress
   * namespace builder — `namespace foo { puts $foo::X }` or
   * `namespace alpha::beta { puts $alpha::beta::X }`. Check the
   * def_target stack for a name whose "::"-bounded prefix matches
   * the key, and resolve the un-consumed suffix from that target's
   * overlay. */
  {
    const char *suffix = NULL;
    lcl_def_target *target = find_def_target_for_self(interp, key, &suffix);

    if (target) {
      char part[256];
      const char *next_rest = NULL;
      const char *has_more =
          lcl_ns_split(suffix, part, sizeof(part), &next_rest);
      const char *part_name = has_more ? part : suffix;
      lcl_value *first_val = NULL;

      if (!hash_table_get(target->overlay->locals, part_name, &first_val)) {
        return LCL_ERROR;
      }

      if (!has_more) {
        /* If the overlay binding is a cell (created via `var`), follow
         * to the inner value so `$foo::counter` matches non-builder
         * semantics where `$foo::counter` dereferences the cell. */
        if (first_val->type == LCL_CELL) {
          lcl_value *inner = NULL;

          if (lcl_cell_get(first_val, &inner) != LCL_OK) {
            lcl_ref_dec(first_val);
            return LCL_ERROR;
          }

          lcl_ref_dec(first_val);
          *out = inner;
          return LCL_OK;
        }

        *out = first_val;
        return LCL_OK;
      }

      current = first_val;
      rest = next_rest;
      goto walk_rest;
    }
  }

  if (!lcl_ns_split(key, first, sizeof(first), &rest)) {
    return LCL_ERROR;
  }

  if (env_get_simple(env, first, &current) != LCL_OK) {
    return LCL_ERROR;
  }

walk_rest:
  while (rest && *rest) {
    lcl_value *next = NULL;
    char part[256];
    const char *next_rest = NULL;

    if (current->type != LCL_NAMESPACE) {
      lcl_ref_dec(current);
      return LCL_ERROR;
    }

    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      if (lcl_ns_get(current, part, &next) != LCL_OK) {
        lcl_ref_dec(current);
        return LCL_ERROR;
      }

      lcl_ref_dec(current);
      current = next;
      rest = next_rest;
    } else {
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

static lcl_result env_set_bang_simple(lcl_env *env, const char *name,
                                      lcl_value *value) {
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

lcl_result lcl_env_set_bang(lcl_env *env, const char *name, lcl_value *value) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  if (!env) {
    return LCL_ERROR;
  }

  if (env_set_bang_simple(env, name, value) == LCL_OK) {
    return LCL_OK;
  }

  if (!lcl_ns_split(name, first, sizeof(first), &rest)) {
    return LCL_ERROR;
  }

  if (env_get_simple(env, first, &current) != LCL_OK) {
    return LCL_ERROR;
  }

  while (rest && *rest) {
    lcl_value *next = NULL;
    char part[256];
    const char *next_rest = NULL;

    if (current->type != LCL_NAMESPACE) {
      lcl_ref_dec(current);
      return LCL_ERROR;
    }

    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      if (lcl_ns_get(current, part, &next) != LCL_OK) {
        lcl_ref_dec(current);
        return LCL_ERROR;
      }

      lcl_ref_dec(current);
      current = next;
      rest = next_rest;
    } else {
      /* rest is the final part - look it up and set! if it's a
         cell */
      if (lcl_ns_get(current, rest, &next) != LCL_OK) {
        lcl_ref_dec(current);
        return LCL_ERROR;
      }

      lcl_ref_dec(current);

      if (next->type == LCL_CELL) {
        lcl_result r = lcl_cell_set(next, value);
        lcl_ref_dec(next);
        return r;
      }

      lcl_ref_dec(next);
      return LCL_ERROR;
    }
  }

  lcl_ref_dec(current);
  return LCL_ERROR;
}

lcl_result lcl_def_target_push(lcl_interp *interp, lcl_frame *parent,
                               const char *name) {
  lcl_def_target *target;
  lcl_value *exports;
  lcl_frame *overlay;
  char *name_copy = NULL;

  if (!interp) {
    return LCL_ERROR;
  }

  if (interp->def_depth >= LCL_DEF_STACK_MAX) {
    return LCL_ERROR;
  }

  exports = lcl_dict_new();

  if (!exports) {
    return LCL_ERROR;
  }

  overlay = lcl_frame_new(parent);

  if (!overlay) {
    lcl_ref_dec(exports);
    return LCL_ERROR;
  }

  if (name) {
    name_copy = strdup(name);

    if (!name_copy) {
      lcl_frame_ref_dec(overlay);
      lcl_ref_dec(exports);
      return LCL_ERROR;
    }
  }

  target = &interp->def_stack[interp->def_depth];
  target->exports = exports;
  target->overlay = overlay;
  target->name = name_copy;
  interp->def_depth++;

  return LCL_OK;
}

lcl_value *lcl_def_target_pop(lcl_interp *interp) {
  lcl_def_target *target;
  lcl_value *exports;

  if (!interp || interp->def_depth <= 0) {
    return NULL;
  }

  interp->def_depth--;
  target = &interp->def_stack[interp->def_depth];

  exports = target->exports;
  target->exports = NULL;

  if (target->overlay) {
    lcl_frame_ref_dec(target->overlay);
    target->overlay = NULL;
  }

  if (target->name) {
    free(target->name);
    target->name = NULL;
  }

  return exports;
}

lcl_result lcl_def_target_bind(lcl_interp *interp, const char *name,
                               lcl_value *value) {
  lcl_def_target *target;

  if (!interp || interp->def_depth <= 0 || !name || !value) {
    return LCL_ERROR;
  }

  target = &interp->def_stack[interp->def_depth - 1];

  if (lcl_dict_put(&target->exports, name, value) != LCL_OK) {
    return LCL_ERROR;
  }

  if (!hash_table_put(target->overlay->locals, name, value)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

lcl_result lcl_def_target_var(lcl_interp *interp, const char *name,
                              lcl_value *value) {
  lcl_def_target *target;
  lcl_value *cell;

  if (!interp || interp->def_depth <= 0 || !name || !value) {
    return LCL_ERROR;
  }

  target = &interp->def_stack[interp->def_depth - 1];

  cell = lcl_cell_new(value);

  if (!cell) {
    return LCL_ERROR;
  }

  if (lcl_dict_put(&target->exports, name, cell) != LCL_OK) {
    lcl_ref_dec(cell);
    return LCL_ERROR;
  }

  if (!hash_table_put(target->overlay->locals, name, cell)) {
    lcl_ref_dec(cell);
    return LCL_ERROR;
  }

  lcl_ref_dec(cell);

  return LCL_OK;
}
