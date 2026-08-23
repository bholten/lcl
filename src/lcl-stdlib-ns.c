#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include <stdio.h>

#include "lcl-name.h"
#include "lcl-stdlib-internal.h"

static lcl_result resolve_ns_path_at_root(lcl_interp *interp, const char *path,
                                          lcl_value **out) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;
  lcl_frame *global = lcl_std_find_global_frame(interp->env.frame);
  int has_more;

  if (!global) {
    return LCL_ERROR;
  }

  has_more = (lcl_ns_split(path, first, sizeof(first), &rest) != NULL);

  if (!hash_table_get(global->locals, has_more ? first : path, &current)) {
    return LCL_ERROR;
  }

  while (has_more && rest && *rest) {
    char part[256];
    const char *next_rest = NULL;
    const char *part_name;
    lcl_value *next = NULL;

    if (current->type != LCL_NAMESPACE) {
      lcl_ref_dec(current);
      return LCL_ERROR;
    }

    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      part_name = part;
    } else {
      part_name = rest;
      next_rest = NULL;
    }

    if (lcl_ns_get(current, part_name, &next) != LCL_OK) {
      lcl_ref_dec(current);
      return LCL_ERROR;
    }

    lcl_ref_dec(current);
    current = next;
    rest = next_rest;
  }

  *out = current;
  return LCL_OK;
}

static lcl_value *resolve_or_create_ns_path(lcl_interp *interp,
                                            const char *path) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  if (!lcl_ns_split(path, first, sizeof(first), &rest)) {
    lcl_value *ns = NULL;
    lcl_frame *global = NULL;

    if (lcl_env_get_value(interp, path, &ns) == LCL_OK ||
        resolve_ns_path_at_root(interp, path, &ns) == LCL_OK) {
      if (ns->type != LCL_NAMESPACE) {
        lcl_ref_dec(ns);
        return NULL;
      }

      return ns;
    }

    ns = lcl_ns_new(path);

    if (!ns) {
      return NULL;
    }

    global = lcl_std_find_global_frame(interp->env.frame);

    if (!global || !hash_table_put(global->locals, path, ns)) {
      lcl_ref_dec(ns);
      return NULL;
    }

    return ns;
  }

  if (lcl_env_get_value(interp, first, &current) != LCL_OK &&
      resolve_ns_path_at_root(interp, first, &current) != LCL_OK) {
    lcl_frame *global = NULL;

    current = lcl_ns_new(first);

    if (!current) {
      return NULL;
    }

    global = lcl_std_find_global_frame(interp->env.frame);

    if (!global || !hash_table_put(global->locals, first, current)) {
      lcl_ref_dec(current);
      return NULL;
    }
  } else if (current->type != LCL_NAMESPACE) {
    lcl_ref_dec(current);
    return NULL;
  }

  while (rest && *rest) {
    lcl_value *next = NULL;
    char part[256];
    const char *next_rest = NULL;
    const char *part_name;

    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      part_name = part;
    } else {
      part_name = rest;
      next_rest = NULL;
    }

    if (lcl_ns_get(current, part_name, &next) == LCL_OK) {
      if (next->type != LCL_NAMESPACE) {
        lcl_ref_dec(next);
        lcl_ref_dec(current);
        return NULL;
      }
    } else {
      next = lcl_ns_new(part_name);

      if (!next) {
        lcl_ref_dec(current);
        return NULL;
      }

      if (hash_table_put(current->as.namespace.namespace, part_name, next) ==
          0) {
        lcl_ref_dec(next);
        lcl_ref_dec(current);
        return NULL;
      }
    }

    lcl_ref_dec(current);
    current = next;
    rest = next_rest;
  }

  return current;
}

static lcl_result ns_build_check_path(lcl_interp *interp, const char *path) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;
  char buf[384];

  if (lcl_name_has_empty_seg(path)) {
    sprintf(buf, "namespace: empty path segment in '%.200s'", path);
    LCL_ERR_MSG_DUP(interp, buf);
    return LCL_ERROR;
  }

  if (!lcl_ns_split(path, first, sizeof(first), &rest)) {
    lcl_value *v = NULL;
    int found;

    if (interp->def_depth > interp->def_floor) {
      lcl_def_target *enclosing = &interp->def_stack[interp->def_depth - 1];
      found = hash_table_get(enclosing->overlay->locals, path, &v);
    } else {
      found = (lcl_env_get_value(interp, path, &v) == LCL_OK);
    }

    if (!found) {
      return LCL_OK;
    }

    if (v->type != LCL_NAMESPACE) {
      sprintf(buf,
              "namespace: '%.200s' is already defined as a %s, not a "
              "namespace",
              path, lcl_type_name(v->type));
      lcl_ref_dec(v);
      LCL_ERR_MSG_DUP(interp, buf);
      return LCL_ERROR;
    }

    lcl_ref_dec(v);
    return LCL_OK;
  }

  if (lcl_env_get_value(interp, first, &current) != LCL_OK) {
    return LCL_OK;
  }

  if (current->type != LCL_NAMESPACE) {
    sprintf(buf,
            "namespace: '%.200s' is already defined as a %s, not a "
            "namespace",
            first, lcl_type_name(current->type));
    lcl_ref_dec(current);
    LCL_ERR_MSG_DUP(interp, buf);
    return LCL_ERROR;
  }

  while (rest && *rest) {
    char part[256];
    const char *next_rest = NULL;
    const char *part_name;
    lcl_value *next = NULL;
    size_t prefix_len;

    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      part_name = part;
      prefix_len = (size_t)(next_rest - path) - 2;
    } else {
      part_name = rest;
      next_rest = NULL;
      prefix_len = strlen(path);
    }

    if (lcl_ns_get(current, part_name, &next) != LCL_OK) {
      lcl_ref_dec(current);
      return LCL_OK;
    }

    if (next->type != LCL_NAMESPACE) {
      if (prefix_len > 200) {
        prefix_len = 200;
      }

      sprintf(buf,
              "namespace: '%.*s' is already defined as a %s, not a "
              "namespace",
              (int)prefix_len, path, lcl_type_name(next->type));
      lcl_ref_dec(next);
      lcl_ref_dec(current);
      LCL_ERR_MSG_DUP(interp, buf);
      return LCL_ERROR;
    }

    lcl_ref_dec(current);
    current = next;
    rest = next_rest;
  }

  lcl_ref_dec(current);
  return LCL_OK;
}

/* isolate { body }
 *
 * Evaluate body in the current frame with the namespace def-target
 * stack temporarily emptied. While the body runs, `let`/`var`/`proc`
 * create local bindings in the current frame instead of writing
 * through to any enclosing `namespace` builder's exports.
 *
 * This is the scope-barrier counterpart to `namespace`: it lets a
 * proc that runs inside a namespace body keep its own `let`s local
 * without leaking them into the namespace under construction. */
static lcl_return_code s_isolate(lcl_interp *interp, int argc,
                                 const lcl_word **args, lcl_value **out) {
  lcl_program *prog = NULL;
  int prog_owned = 0;
  int saved_def_floor;
  int saved_def_lookup_floor;
  int saved_tail_position;
  lcl_return_code rc;
  lcl_value *last = NULL;
  int i;

  if (!lcl_std_chk_argc(interp, "isolate", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_std_get_body_program(interp, args[0], "isolate", &prog,
                               &prog_owned) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  saved_def_floor = interp->def_floor;
  saved_def_lookup_floor = interp->def_lookup_floor;
  saved_tail_position = interp->in_tail_position;
  interp->def_floor = interp->def_depth;
  interp->def_lookup_floor = interp->def_depth;
  rc = LCL_RC_OK;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    int is_last_cmd = (i == prog->ncmd - 1);

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->def_floor = saved_def_floor;
      interp->def_lookup_floor = saved_def_lookup_floor;
      interp->in_tail_position = saved_tail_position;
      lcl_std_free_if_owned(prog, prog_owned);

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc != LCL_RC_OK) {
      if (rc != LCL_RC_RETURN) {
        interp->err_line = cmd->line;

        if (interp->err_file_owned && interp->err_file) {
          free((void *)interp->err_file);
        }

        interp->err_file = prog->file ? strdup(prog->file) : NULL;
        interp->err_file_owned = prog->file ? 1 : 0;
      }

      break;
    }
  }

  interp->def_floor = saved_def_floor;
  interp->def_lookup_floor = saved_def_lookup_floor;
  interp->in_tail_position = saved_tail_position;
  lcl_std_free_if_owned(prog, prog_owned);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
    return rc;
  }

  if (last) {
    lcl_ref_dec(last);
  }

  return rc;
}

/* Note: `namespace eval` simplified to `namespace`. */
#define NS_POP_HOME_MAX 32

static void resolve_popped_pending(lcl_interp *interp, lcl_value *ns,
                                   const char *qname) {
  int i;
  lcl_ns_anchor *chain[NS_POP_HOME_MAX];
  int nchain = 0;

  if (ns) {
    lcl_ns_anchor *a = lcl_ns_anchor_get(ns);

    if (a) {
      chain[nchain++] = a;
    }
  }

  if (qname && lcl_name_has_sep(qname)) {
    char seg[256];
    const char *rest = NULL;

    if (lcl_ns_split(qname, seg, sizeof(seg), &rest)) {
      lcl_value *cur = NULL;

      if (lcl_env_get_value(interp, seg, &cur) == LCL_OK) {
        while (cur) {
          lcl_value *next = NULL;
          lcl_ns_anchor *a;

          if (cur->type != LCL_NAMESPACE) {
            break;
          }

          a = lcl_ns_anchor_get(cur);

          if (a && nchain < NS_POP_HOME_MAX) {
            chain[nchain++] = a;
          }

          if (rest && lcl_name_has_sep(rest) &&
              lcl_ns_split(rest, seg, sizeof(seg), &rest)) {
            if (lcl_ns_get(cur, seg, &next) != LCL_OK) {
              next = NULL;
            }
          }

          lcl_ref_dec(cur);
          cur = next;
        }

        if (cur) {
          lcl_ref_dec(cur);
        }
      }
    }
  }

  for (i = 0; i < interp->n_popped_pending; i++) {
    lcl_value *pv = interp->popped_pending[i];
    lcl_proc *p = pv->as.procedure.proc;
    int u;
    int c;

    for (c = 0; c < nchain; c++) {
      int dup = 0;
      int j;

      for (j = 0; j < p->nhome; j++) {
        if (p->home[j] == chain[c]) {
          dup = 1;
          break;
        }
      }

      if (!dup) {
        lcl_ns_anchor **h =
            realloc(p->home, (size_t)(p->nhome + 1) * sizeof(*h));

        if (h) {
          p->home = h;
          p->home[p->nhome++] = lcl_ns_anchor_ref(chain[c]);
        }
      }
    }

    for (u = 0; u < p->nupvals; u++) {
      lcl_upvalue *uv = &p->upvals[u];

      if (uv->is_ns_root && !uv->anchor) {
        lcl_value *v = NULL;

        if (lcl_env_get_value(interp, uv->name, &v) == LCL_OK) {
          if (v->type == LCL_NAMESPACE) {
            lcl_ns_anchor *a = lcl_ns_anchor_get(v);

            if (a) {
              uv->anchor = lcl_ns_anchor_ref(a);
            }
          }

          lcl_ref_dec(v);
        }
      }
    }

    lcl_ref_dec(pv);
  }

  free(interp->popped_pending);
  interp->popped_pending = NULL;
  interp->n_popped_pending = 0;
}

static lcl_return_code s_namespace(lcl_interp *interp, int argc,
                                   const lcl_word **args, lcl_value **out) {
  /* namespace { body }      - anonymous, returns ns value
   * namespace name { body } - named, auto-attaches to registry
   *
   * Re-entering an existing namespace mutates the existing
   * namespace's hash table in place rather than rebuilding and
   * rebinding a new namespace value. This makes `namespace foo { let
   * X 1 }` from inside a proc body persist correctly (the bind
   * wouldn't propagate out of the proc frame otherwise) and makes
   * qualified-path re-entry (`namespace a::b { ... }`) preserve prior
   * bindings.
   *
   * A namesspace name can't collide with anything else, e.g. a proc
   * or anything. A namespace's parent must be a namespace itself. */
  int named;
  const lcl_word *body_word;
  lcl_value *name_v = NULL;
  char *ns_name = NULL;
  lcl_program *prog = NULL;
  int prog_owned = 0;
  lcl_frame *old_frame = NULL;
  lcl_def_target *target;
  lcl_return_code rc;
  int i;
  lcl_value *last = NULL;
  lcl_value *exports = NULL;
  lcl_value *ns = NULL;
  lcl_value *existing_ns = NULL;

  if (!lcl_std_chk_argc(interp, "namespace", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  named = (argc == 2);
  body_word = named ? args[1] : args[0];

  if (named) {
    const char *name_cstr;

    if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, name_v, &name_cstr) != LCL_OK) {
      lcl_ref_dec(name_v);
      return LCL_RC_ERR;
    }

    ns_name = strdup(name_cstr);
    lcl_ref_dec(name_v);

    if (!ns_name) {
      LCL_ERR_MSG(interp, "namespace: out of memory");
      return LCL_RC_ERR;
    }

    if (ns_build_check_path(interp, ns_name) != LCL_OK) {
      free(ns_name);
      return LCL_RC_ERR;
    }
  }

  {
    int prog_owned_flag = 0;

    if (lcl_std_get_body_program(interp, body_word, "namespace", &prog,
                                 &prog_owned_flag) != LCL_RC_OK) {
      free(ns_name);
      return LCL_RC_ERR;
    }

    prog_owned = prog_owned_flag;
  }

  if (lcl_def_target_push(interp, interp->env.frame, ns_name) != LCL_OK) {
    if (interp->def_depth >= LCL_DEF_STACK_MAX) {
      LCL_ERR_MSG(interp, "namespace: builders nested too deeply");
    } else {
      LCL_ERR_MSG(interp, "namespace: out of memory");
    }

    lcl_std_free_if_owned(prog, prog_owned);
    free(ns_name);
    return LCL_RC_ERR;
  }

  target = &interp->def_stack[interp->def_depth - 1];
  old_frame = interp->env.frame;

  if (ns_name) {
    lcl_value *found = NULL;
    int have_found;

    if (!lcl_name_has_sep(ns_name) &&
        interp->def_depth - 1 > interp->def_floor) {
      lcl_def_target *enclosing = &interp->def_stack[interp->def_depth - 2];

      have_found = hash_table_get(enclosing->overlay->locals, ns_name, &found);
    } else {
      have_found = (lcl_env_get_value(interp, ns_name, &found) == LCL_OK);

      if (!have_found) {
        have_found =
            (resolve_ns_path_at_root(interp, ns_name, &found) == LCL_OK);
      }
    }

    if (have_found) {
      if (found->type == LCL_NAMESPACE) {
        hash_iter it = {0};
        const char *key;
        lcl_value *value;
        int prepop_failed = 0;

        while (hash_table_iterate(found->as.namespace.namespace, &it, &key,
                                  &value)) {
          if (!prepop_failed) {
            if (!hash_table_put(target->overlay->locals, key, value) ||
                lcl_dict_put(&target->exports, key, value) != LCL_OK) {
              prepop_failed = 1;
            }
          }

          lcl_ref_dec(value);
        }

        if (prepop_failed) {
          lcl_value *leaked_exports;
          lcl_ref_dec(found);
          leaked_exports = lcl_def_target_pop(interp);

          if (leaked_exports) {
            lcl_ref_dec(leaked_exports);
          }

          lcl_std_free_if_owned(prog, prog_owned);
          free(ns_name);
          LCL_ERR_MSG(interp,
                      "namespace: out of memory pre-populating builder");
          return LCL_RC_ERR;
        }

        existing_ns = found;
      } else {
        char buf[384];
        lcl_value *leaked_exports;

        sprintf(buf,
                "namespace: '%.200s' is already defined as a %s, not a "
                "namespace",
                ns_name, lcl_type_name(found->type));
        lcl_ref_dec(found);
        leaked_exports = lcl_def_target_pop(interp);

        if (leaked_exports) {
          lcl_ref_dec(leaked_exports);
        }

        lcl_std_free_if_owned(prog, prog_owned);
        free(ns_name);
        LCL_ERR_MSG_DUP(interp, buf);
        return LCL_RC_ERR;
      }
    }
  }

  interp->env.frame = target->overlay;

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_value *leaked_exports;
    interp->env.frame = old_frame;
    leaked_exports = lcl_def_target_pop(interp);

    if (leaked_exports) {
      lcl_ref_dec(leaked_exports);
    }

    lcl_ref_dec(existing_ns);
    lcl_std_free_if_owned(prog, prog_owned);
    free(ns_name);
    LCL_ERR_MSG(interp, "namespace: max recursion depth exceeded");
    return LCL_RC_ERR;
  }

  interp->depth++;
  rc = LCL_RC_OK;

  {
    int saved_tail_position = interp->in_tail_position;
    interp->in_tail_position = 0;

    for (i = 0; i < prog->ncmd; i++) {
      lcl_command *cmd = &prog->cmd[i];

      if (last) {
        lcl_ref_dec(last);
        last = NULL;
      }

      rc = lcl_call_from_words(interp, cmd, &last);

      if (rc != LCL_RC_OK) {
        if (rc != LCL_RC_RETURN) {
          interp->err_line = cmd->line;

          if (interp->err_file_owned && interp->err_file) {
            free((void *)interp->err_file);
          }

          interp->err_file = prog->file ? strdup(prog->file) : NULL;
          interp->err_file_owned = prog->file ? 1 : 0;
        }

        break;
      }
    }

    interp->in_tail_position = saved_tail_position;
  }

  interp->depth--;
  interp->env.frame = old_frame;
  exports = lcl_def_target_pop(interp);
  lcl_std_free_if_owned(prog, prog_owned);

  if (last) {
    lcl_ref_dec(last);
  }

  if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
    if (exports) {
      lcl_ref_dec(exports);
    }

    lcl_ref_dec(existing_ns);
    free(ns_name);
    return rc;
  }

  /* Re-entry path: mutate the existing namespace in place by dumping
   * the (pre-pop + body) exports into its hash table. This skips
   * rebuild-and-rebind entirely; all live references to the namespace
   * value observe the new bindings immediately, and a
   * proc-body-scoped rebind can no longer shadow the outer
   * binding. */
  if (existing_ns) {
    hash_iter it = {0};
    const char *key;
    lcl_value *value;
    int put_failed = 0;

    {
      hash_iter cit = {0};
      const char *ck;
      lcl_value *cv;
      int cycle_found = 0;
      char msg[900];

      while (hash_table_iterate(exports->as.dict.dictionary, &cit, &ck, &cv)) {
        if (!cycle_found && lcl_value_would_cycle(existing_ns, cv)) {
          size_t used;

          cycle_found = 1;
          used = (size_t)snprintf(msg, sizeof(msg),
                                  "namespace: binding member \"%.128s\" "
                                  "would create a reference cycle",
                                  ck);

          if (used < sizeof(msg)) {
            lcl_value_cycle_explain(existing_ns, cv, msg + used,
                                    sizeof(msg) - used);
          }

          used = strlen(msg);

          if (used < sizeof(msg)) {
            snprintf(msg + used, sizeof(msg) - used, "%s",
                     " (a ::-rooted spelling is non-owning for a "
                     "top-level-reachable namespace)");
          }
        }

        lcl_ref_dec(cv);
      }

      if (cycle_found) {
        lcl_ref_dec(exports);
        lcl_ref_dec(existing_ns);
        free(ns_name);
        LCL_ERR_MSG_DUP(interp, msg);
        return LCL_RC_ERR;
      }
    }

    while (hash_table_iterate(exports->as.dict.dictionary, &it, &key, &value)) {
      if (!put_failed) {
        if (!hash_table_put(existing_ns->as.namespace.namespace, key, value)) {
          put_failed = 1;
        }
      }

      lcl_ref_dec(value);
    }

    lcl_ref_dec(exports);

    if (put_failed) {
      lcl_ref_dec(existing_ns);
      free(ns_name);
      LCL_ERR_MSG(interp,
                  "namespace: out of memory mutating existing namespace");
      return LCL_RC_ERR;
    }

    resolve_popped_pending(interp, existing_ns, ns_name);
    free(ns_name);
    *out = existing_ns;
    return LCL_RC_OK;
  }

  ns = lcl_ns_from_dict(exports, ns_name);

  if (!ns) {
    free(ns_name);
    LCL_ERR_MSG(interp, "namespace: failed to create namespace");
    return LCL_RC_ERR;
  }

  if (ns_name) {
    char first[256];
    const char *rest = NULL;

    if (lcl_ns_split(ns_name, first, sizeof(first), &rest)) {
      lcl_value *parent = resolve_or_create_ns_path(interp, first);

      if (!parent) {
        lcl_ref_dec(ns);
        free(ns_name);
        LCL_ERR_MSG(interp, "namespace: failed to resolve parent path");
        return LCL_RC_ERR;
      }

      while (rest && *rest) {
        char part[256];
        const char *next_rest = NULL;
        lcl_value *next = NULL;

        if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
          if (lcl_ns_get(parent, part, &next) == LCL_OK) {
            /* Bugfix: backstop for the pre-body path check: the body
             * may have rebound a segment (e.g. via Ns::set). Never
             * walk into a non-namespace. */
            if (next->type != LCL_NAMESPACE) {
              LCL_ERR_MSG(interp, "namespace: path collides with a "
                                  "non-namespace binding");
              lcl_ref_dec(next);
              lcl_ref_dec(parent);
              lcl_ref_dec(ns);
              free(ns_name);

              return LCL_RC_ERR;
            }
          } else {
            next = lcl_ns_new(part);

            if (!next ||
                !hash_table_put(parent->as.namespace.namespace, part, next)) {
              LCL_ERR_MSG(interp, "namespace: out of memory");

              if (next) {
                lcl_ref_dec(next);
              }

              lcl_ref_dec(parent);
              lcl_ref_dec(ns);
              free(ns_name);

              return LCL_RC_ERR;
            }
          }

          lcl_ref_dec(parent);
          parent = next;
          rest = next_rest;
        } else {
          lcl_value *member = NULL;

          if (lcl_ns_get(parent, rest, &member) == LCL_OK) {
            int member_is_ns = (member->type == LCL_NAMESPACE);

            lcl_ref_dec(member);

            if (!member_is_ns) {
              LCL_ERR_MSG(interp, "namespace: path collides with a "
                                  "non-namespace binding");
              lcl_ref_dec(parent);
              lcl_ref_dec(ns);
              free(ns_name);

              return LCL_RC_ERR;
            }
          }

          if (lcl_value_would_cycle(parent, ns)) {
            char msg[900];
            size_t used;

            used = (size_t)snprintf(msg, sizeof(msg),
                                    "namespace: binding would create a "
                                    "reference cycle (a member contains an "
                                    "ancestor namespace)");

            if (used < sizeof(msg)) {
              lcl_value_cycle_explain(parent, ns, msg + used,
                                      sizeof(msg) - used);
            }

            LCL_ERR_MSG_DUP(interp, msg);
            lcl_ref_dec(parent);
            lcl_ref_dec(ns);
            free(ns_name);

            return LCL_RC_ERR;
          }

          if (!hash_table_put(parent->as.namespace.namespace, rest, ns)) {
            lcl_ref_dec(parent);
            lcl_ref_dec(ns);
            free(ns_name);
            LCL_ERR_MSG(interp, "namespace: failed to bind in parent");
            return LCL_RC_ERR;
          }

          lcl_ref_dec(parent);
          rest = NULL;
        }
      }
    } else {
      if (interp->def_depth > interp->def_floor) {
        if (lcl_def_target_bind(interp, ns_name, ns) != LCL_OK) {
          lcl_ref_dec(ns);
          free(ns_name);
          LCL_ERR_MSG(interp, "namespace: failed to bind in parent builder");
          return LCL_RC_ERR;
        }
      } else {
        if (lcl_env_let(&interp->env, ns_name, ns) != LCL_OK) {
          lcl_ref_dec(ns);
          free(ns_name);
          LCL_ERR_MSG(interp, "namespace: failed to attach namespace");
          return LCL_RC_ERR;
        }
      }
    }
  }

  resolve_popped_pending(interp, ns, ns_name);
  free(ns_name);
  *out = ns;
  return LCL_RC_OK;
}

/* import <namespace> ?name1 name2 ...?
 * Imports bindings from a namespace into the current scope.
 * If no names given, imports all bindings.
 * Errors if any name already exists in the current frame. */
static lcl_return_code s_import(lcl_interp *interp, int argc,
                                const lcl_word **argv, lcl_value **out) {
  lcl_value *ns_name_v = NULL;
  lcl_value *ns = NULL;
  const char *ns_name;
  int i;

  if (!lcl_std_chk_argc(interp, "import", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word(interp, argv[0], &ns_name_v) != LCL_RC_OK) {
    lcl_ref_dec(ns_name_v);
    return LCL_RC_ERR;
  }

  if (ns_name_v->type == LCL_NAMESPACE) {
    ns = ns_name_v;
  } else if (ns_name_v->type == LCL_CELL && ns_name_v->as.cell.inner != NULL &&
             ns_name_v->as.cell.inner->type == LCL_NAMESPACE) {
    ns = lcl_ref_inc(ns_name_v->as.cell.inner);
    lcl_ref_dec(ns_name_v);
  } else {
    if (lcl_value_to_cstring(interp, ns_name_v, &ns_name) != LCL_OK) {
      lcl_ref_dec(ns_name_v);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_value(interp, ns_name, &ns) != LCL_OK) {
      LCL_ERR_MSG(interp, "import: namespace not found");
      lcl_ref_dec(ns_name_v);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(ns_name_v);

    if (ns->type == LCL_CELL) {
      lcl_value *inner;

      if (!ns->as.cell.inner) {
        LCL_ERR_MSG(interp, "import: cell is empty");
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      inner = lcl_ref_inc(ns->as.cell.inner);
      lcl_ref_dec(ns);
      ns = inner;
    }
  }

  if (ns->type != LCL_NAMESPACE) {
    lcl_std_err_expected_got(interp, "import", "namespace", ns);
    lcl_ref_dec(ns);
    return LCL_RC_ERR;
  }

  if (argc == 1) {
    hash_iter it = {0};
    const char *key;
    lcl_value *value;

    while (hash_table_iterate(ns->as.namespace.namespace, &it, &key, &value)) {
      lcl_value *existing = NULL;

      if (hash_table_get(interp->env.frame->locals, key, &existing)) {
        char buf[256];
        sprintf(buf, "import: '%s' already exists in current scope", key);
        lcl_ref_dec(existing);
        lcl_ref_dec(value);
        lcl_ref_dec(ns);
        LCL_ERR_MSG_DUP(interp, buf);
        return LCL_RC_ERR;
      }

      if (interp->def_depth > interp->def_floor) {
        if (lcl_def_target_bind(interp, key, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      } else {
        if (lcl_env_let(&interp->env, key, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      }
      lcl_ref_dec(value); /* Balance iterate */
    }
  } else {
    for (i = 1; i < argc; i++) {
      lcl_value *name_v = NULL;
      const char *name_str;
      lcl_value *value = NULL;
      lcl_value *existing = NULL;

      if (lcl_eval_word_to_str(interp, argv[i], &name_v) != LCL_RC_OK) {
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      if (lcl_value_to_cstring(interp, name_v, &name_str) != LCL_OK) {
        lcl_ref_dec(name_v);
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      if (lcl_ns_get(ns, name_str, &value) != LCL_OK) {
        char buf[256];
        sprintf(buf, "import: '%s' not found in namespace", name_str);
        LCL_ERR_MSG_DUP(interp, buf);
        lcl_ref_dec(name_v);
        lcl_ref_dec(ns);
        return LCL_RC_ERR;
      }

      if (hash_table_get(interp->env.frame->locals, name_str, &existing)) {
        char buf[256];
        sprintf(buf, "import: '%s' already exists in current scope", name_str);
        lcl_ref_dec(existing);
        lcl_ref_dec(value);
        lcl_ref_dec(name_v);
        lcl_ref_dec(ns);
        LCL_ERR_MSG_DUP(interp, buf);
        return LCL_RC_ERR;
      }

      if (interp->def_depth > interp->def_floor) {
        if (lcl_def_target_bind(interp, name_str, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(name_v);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      } else {
        if (lcl_env_let(&interp->env, name_str, value) != LCL_OK) {
          lcl_ref_dec(value);
          lcl_ref_dec(name_v);
          lcl_ref_dec(ns);
          LCL_ERR_MSG(interp, "import: failed to bind");
          return LCL_RC_ERR;
        }
      }

      lcl_ref_dec(value);
      lcl_ref_dec(name_v);
    }
  }

  lcl_ref_dec(ns);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* Ns::keys ns - return list of binding names in a namespace */
static lcl_return_code c_ns_keys(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;
  lcl_value *result;
  (void)interp;

  if (!lcl_std_chk_argc(interp, "Ns::keys", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return lcl_std_err_expected_got(interp, "Ns::keys", "namespace", argv[0]);
  }

  result = lcl_list_new();
  while (hash_table_iterate(argv[0]->as.namespace.namespace, &it, &key, &val)) {
    lcl_value *key_v = lcl_string_new(key);
    lcl_list_push(&result, key_v);
    lcl_ref_dec(key_v);
    lcl_ref_dec(val);
  }

  *out = result;
  return LCL_RC_OK;
}

/* Ns::name ns - return the qualified name of a namespace */
static lcl_return_code c_ns_name(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "Ns::name", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return lcl_std_err_expected_got(interp, "Ns::name", "namespace", argv[0]);
  }

  *out = lcl_string_new(argv[0]->as.namespace.qname);
  return LCL_RC_OK;
}

/* Ns::set ns name value - bind name to value in namespace
 *
 * Programmatic write into a namespace, without going through the
 * syntactic `namespace foo { ... }` builder. Mutates the namespace's
 * underlying hash table in place, so all references to the namespace
 * value observe the new binding. Returns the bound value. */
static lcl_return_code c_ns_set(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *name;

  if (!lcl_std_chk_argc(interp, "Ns::set", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return lcl_std_err_expected_got(interp, "Ns::set", "namespace", argv[0]);
  }

  if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_would_cycle(argv[0], argv[2])) {
    char msg[900];
    size_t used;

    used = (size_t)snprintf(msg, sizeof(msg),
                            "Ns::set: would create a reference cycle "
                            "(value contains the target namespace)");

    if (used < sizeof(msg)) {
      lcl_value_cycle_explain(argv[0], argv[2], msg + used, sizeof(msg) - used);
    }

    used = strlen(msg);

    if (used < sizeof(msg)) {
      snprintf(msg + used, sizeof(msg) - used, "%s",
               " (a ::-rooted spelling is non-owning for a "
               "top-level-reachable namespace; or define the member inside "
               "the namespace body)");
    }

    LCL_ERR_MSG_DUP(interp, msg);
    return LCL_RC_ERR;
  }

  if (!hash_table_put(argv[0]->as.namespace.namespace, name, argv[2])) {
    LCL_ERR_MSG(interp, "Ns::set: failed to bind in namespace");
    return LCL_RC_ERR;
  }

  *out = lcl_ref_inc(argv[2]);
  return LCL_RC_OK;
}

/* Ns::has? ns name - check if binding exists in namespace */
lcl_return_code lcl_std_ns_has(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  lcl_value *found = NULL;
  const char *name;

  if (!lcl_std_chk_argc(interp, "Ns::has?", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return lcl_std_err_expected_got(interp, "Ns::has?", "namespace", argv[0]);
  }

  if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_ns_get(argv[0], name, &found) == LCL_OK) {
    lcl_ref_dec(found);
    *out = lcl_int_new(1);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

/* Ns::del ns name - remove a binding from a namespace
 *
 * Mutates the namespace's hash table in place, so all references to
 * the namespace value observe the removal. Cells captured elsewhere
 * (closures, imports) keep their own references and stay usable.
 * Returns 1 if the binding was removed, 0 if it was absent. */
static lcl_return_code c_ns_del(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *name;

  if (!lcl_std_chk_argc(interp, "Ns::del", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return lcl_std_err_expected_got(interp, "Ns::del", "namespace", argv[0]);
  }

  if (lcl_value_to_cstring(interp, argv[1], &name) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      hash_table_delete(argv[0]->as.namespace.namespace, name) ? 1 : 0);

  return LCL_RC_OK;
}

/* Ns::clear ns - remove all bindings from a namespace
 *
 * Empties the namespace's hash table in place; the value's identity
 * is preserved, so all live references see the emptied namespace
 * immediately. Cells captured elsewhere keep their own references. */
static lcl_return_code c_ns_clear(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  if (!lcl_std_chk_argc(interp, "Ns::clear", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (argv[0]->type != LCL_NAMESPACE) {
    return lcl_std_err_expected_got(interp, "Ns::clear", "namespace", argv[0]);
  }

  hash_table_clear(argv[0]->as.namespace.namespace);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

void lcl_std_register_ns(lcl_interp *interp) {
  lcl_value *ns_ns;

  lcl_register_spec(interp, "namespace", s_namespace);
  lcl_register_spec(interp, "isolate", s_isolate);
  lcl_register_spec(interp, "import", s_import);
  ns_ns = lcl_ns_new("Ns");
  lcl_define_take(interp, "Ns", ns_ns);
  lcl_ns_def_take(ns_ns, "keys", lcl_c_proc_new("Ns::keys", c_ns_keys));
  lcl_ns_def_take(ns_ns, "name", lcl_c_proc_new("Ns::name", c_ns_name));
  lcl_ns_def_take(ns_ns, "has?", lcl_c_proc_new("Ns::has?", lcl_std_ns_has));
  lcl_ns_def_take(ns_ns, "set", lcl_c_proc_new("Ns::set", c_ns_set));
  lcl_ns_def_take(ns_ns, "del", lcl_c_proc_new("Ns::del", c_ns_del));
  lcl_ns_def_take(ns_ns, "clear", lcl_c_proc_new("Ns::clear", c_ns_clear));
}
