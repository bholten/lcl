#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "lcl-path.h"
#include "lcl-stdlib-internal.h"

static char *read_file(const char *path, size_t *out_len) {
  FILE *f;
  long len;
  char *buf;
  size_t nread;

  f = fopen(path, "rb");

  if (!f) {
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  len = ftell(f);
  if (len < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  nread = fread(buf, 1, (size_t)len, f);
  fclose(f);

  if ((long)nread != len) {
    free(buf);
    return NULL;
  }

  buf[len] = '\0';

  if (out_len) {
    *out_len = (size_t)len;
  }

  return buf;
}

static lcl_return_code s_load(lcl_interp *interp, int argc,
                              const lcl_word **args, lcl_value **out) {
  lcl_value *path_v = NULL;
  const char *path;
  char *src = NULL;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int i;
  int saved_tail_position = interp->in_tail_position;
  const char *saved_cur_file = interp->cur_file;
  int saved_cur_line = interp->cur_line;

  if (!lcl_std_chk_argc(interp, "load", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, path_v, &path) != LCL_OK) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  src = read_file(path, NULL);

  if (!src) {
    char msg[192];

    snprintf(msg, sizeof(msg), "load: could not read file \"%.128s\"", path);
    LCL_ERR_MSG_DUP(interp, msg);
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  prog = lcl_compile_report(interp, src, path);
  free(src);

  if (!prog) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(path_v);

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    LCL_ERR_MSG(interp, "load: max recursion depth exceeded");
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    int is_last_cmd = (i == prog->ncmd - 1);

    interp->cur_file = prog->file;
    interp->cur_line = cmd->line;

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->in_tail_position = saved_tail_position;
      interp->cur_file = saved_cur_file;
      interp->cur_line = saved_cur_line;
      interp->depth--;
      lcl_program_free(prog);

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc != LCL_RC_OK) {
      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->cur_file = saved_cur_file;
  interp->cur_line = saved_cur_line;
  interp->depth--;
  lcl_program_free(prog);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) {
      lcl_ref_dec(last);
    }
  }

  return rc;
}

/* lift_namespaces_to_caller
 *
 * Iterate a dict of (name -> namespace value) and bind each entry into
 * the caller's frame (or into the surrounding namespace builder when
 * `require` is itself called from inside a `namespace` block).
 *
 * Returns LCL_OK on success. On failure, partial bindings may have
 * already been made; caller is responsible for error reporting. */
static lcl_result lift_namespaces_to_caller(lcl_interp *interp,
                                            lcl_value *cached_dict) {
  hash_iter it = {0};
  const char *key;
  lcl_value *value;
  lcl_result final_rc = LCL_OK;

  while (
      hash_table_iterate(cached_dict->as.dict.dictionary, &it, &key, &value)) {
    lcl_result r;

    if (interp->def_depth > interp->def_floor) {
      r = lcl_def_target_bind(interp, key, value);
    } else {
      r = lcl_env_let(&interp->env, key, value);
    }

    lcl_ref_dec(value);

    if (r != LCL_OK) {
      final_rc = LCL_ERROR;
    }
  }

  return final_rc;
}

/* require_str_append: grow-and-append for building dynamic error
 * messages. Returns 1 on success, 0 on OOM (buffer freed, *buf
 * NULLed, so the caller can bail with a static message). */
static int require_str_append(char **buf, size_t *len, size_t *cap,
                              const char *s) {
  size_t n = strlen(s);

  if (*len + n + 1 > *cap) {
    size_t grown_cap = *cap ? *cap : 64;
    char *grown;

    while (*len + n + 1 > grown_cap) {
      grown_cap *= 2;
    }

    grown = (char *)realloc(*buf, grown_cap);

    if (!grown) {
      free(*buf);
      *buf = NULL;
      return 0;
    }

    *buf = grown;
    *cap = grown_cap;
  }

  memcpy(*buf + *len, s, n + 1);
  *len += n;
  return 1;
}

/* require_current_dir
 *
 * Lexical directory of the file currently being evaluated
 * (interp->cur_file), as a malloc'd string. Returns "." for a bare
 * filename (which is CWD-relative anyway), and NULL when evaluation
 * is not file-backed (cur_file unset or the "<bytes>" placeholder
 * from string eval). */
static char *require_current_dir(const lcl_interp *interp) {
  const char *file = interp->cur_file;

  if (!file || strcmp(file, "<bytes>") == 0) {
    return NULL;
  }

  return lcl_path_dirname(file);
}

/* require_resolve
 *
 * Resolve the `require` argument to a cleaned lexical path per the
 * module-loader contract:
 *   - rooted paths ("/...") are used as-is;
 *   - "./" and "../" paths join to the lexical directory of the file
 *     whose evaluation triggered the require (interp->cur_file),
 *     falling back to the argument itself (host-CWD-relative) when
 *     evaluation is not file-backed (REPL, eval of a string);
 *   - bare paths are searched under the registered require roots in
 *     registration order (lcl_add_require_root); with no roots
 *     registered they resolve against the CWD (legacy behavior).
 *
 * Resolution is lexical (lcl-path.h): candidates are normalized by
 * string rules alone and probed with fopen. The filesystem is never
 * asked to canonicalize -- symlinks are not resolved, and the result
 * stays relative when its inputs are relative; what a relative name
 * means is host state, per the contract in lcl.h.
 *
 * On success returns the malloc'd lexical path of the first openable
 * candidate. On failure returns NULL with an interp error naming the
 * argument and every candidate path attempted. */
static char *require_resolve(lcl_interp *interp, const char *arg) {
  char **candidates = NULL;
  size_t ncand = 0;
  size_t i;
  char *resolved = NULL;
  int oom = 0;
  int is_dot_relative =
      (arg[0] == '.' && (arg[1] == '/' || (arg[1] == '.' && arg[2] == '/')));

  if (arg[0] == '\0') {
    LCL_ERR_MSG(interp, "require: empty path");
    return NULL;
  }

  if (is_dot_relative) {
    char *base = require_current_dir(interp);

    candidates = (char **)malloc(sizeof(*candidates));
    ncand = 1;

    if (candidates) {
      candidates[0] = base ? lcl_path_join(base, arg) : lcl_path_clean(arg);
    }

    free(base);
  } else if (arg[0] != '/' && interp->require_roots_len > 0) {
    ncand = interp->require_roots_len;
    candidates = (char **)malloc(ncand * sizeof(*candidates));

    if (candidates) {
      for (i = 0; i < ncand; i++) {
        candidates[i] = lcl_path_join(interp->require_roots[i], arg);
      }
    }
  } else {
    candidates = (char **)malloc(sizeof(*candidates));
    ncand = 1;

    if (candidates) {
      candidates[0] = lcl_path_clean(arg);
    }
  }

  if (!candidates) {
    LCL_ERR_MSG(interp, "require: out of memory resolving path");
    return NULL;
  }

  for (i = 0; i < ncand; i++) {
    if (!candidates[i]) {
      oom = 1;
      continue;
    }

    if (!resolved) {
      FILE *probe = fopen(candidates[i], "rb");

      if (probe) {
        fclose(probe);
        resolved = candidates[i];
        candidates[i] = NULL;
      }
    }
  }

  if (!resolved) {
    if (oom) {
      LCL_ERR_MSG(interp, "require: out of memory resolving path");
    } else {
      char *msg = NULL;
      size_t len = 0;
      size_t cap = 0;
      int ok = require_str_append(&msg, &len, &cap, "require: cannot find \"");

      ok = ok && require_str_append(&msg, &len, &cap, arg);
      ok = ok && require_str_append(&msg, &len, &cap, "\" (tried");

      for (i = 0; ok && i < ncand; i++) {
        ok = require_str_append(&msg, &len, &cap, i == 0 ? " \"" : ", \"");
        ok = ok && require_str_append(&msg, &len, &cap, candidates[i]);
        ok = ok && require_str_append(&msg, &len, &cap, "\"");
      }

      ok = ok && require_str_append(&msg, &len, &cap, ")");

      if (ok) {
        LCL_ERR_MSG_DUP(interp, msg);
        free(msg);
      } else {
        LCL_ERR_MSG(interp, "require: cannot resolve path");
      }
    }
  }

  for (i = 0; i < ncand; i++) {
    free(candidates[i]);
  }

  free(candidates);
  return resolved;
}

/* require_stack_contains: is the module identified by `key` currently
 * being evaluated by an in-progress require? */
static int require_stack_contains(const lcl_interp *interp, const char *key) {
  size_t i;

  for (i = 0; i < interp->require_stack_len; i++) {
    if (strcmp(interp->require_stack[i].key, key) == 0) {
      return 1;
    }
  }

  return 0;
}

/* require_stack_push: record a module as in-progress. `key` is the
 * identity compared by require_stack_contains; `path` is the lexical
 * path shown in cycle diagnostics. Returns 1 on success, 0 on OOM. */
static int require_stack_push(lcl_interp *interp, const char *key,
                              const char *path) {
  char *key_copy = strdup(key);
  char *path_copy = strdup(path);

  if (!key_copy || !path_copy) {
    free(key_copy);
    free(path_copy);
    return 0;
  }

  if (interp->require_stack_len == interp->require_stack_cap) {
    size_t cap = interp->require_stack_cap ? interp->require_stack_cap * 2 : 8;
    lcl_require_entry *grown = (lcl_require_entry *)realloc(
        interp->require_stack, cap * sizeof(*grown));

    if (!grown) {
      free(key_copy);
      free(path_copy);
      return 0;
    }

    interp->require_stack = grown;
    interp->require_stack_cap = cap;
  }

  interp->require_stack[interp->require_stack_len].key = key_copy;
  interp->require_stack[interp->require_stack_len].path = path_copy;
  interp->require_stack_len++;
  return 1;
}

static void require_stack_pop(lcl_interp *interp) {
  if (interp->require_stack_len > 0) {
    interp->require_stack_len--;
    free(interp->require_stack[interp->require_stack_len].key);
    free(interp->require_stack[interp->require_stack_len].path);
    interp->require_stack[interp->require_stack_len].key = NULL;
    interp->require_stack[interp->require_stack_len].path = NULL;
  }
}

/* require_cycle_error: build "require: dependency cycle: a -> b -> a"
 * from the in-progress stack, starting at the first occurrence of
 * `key`. The chain is printed with lexical paths. */
static void require_cycle_error(lcl_interp *interp, const char *key,
                                const char *path) {
  char *msg = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t start = 0;
  size_t i;
  int ok;

  for (i = 0; i < interp->require_stack_len; i++) {
    if (strcmp(interp->require_stack[i].key, key) == 0) {
      start = i;
      break;
    }
  }

  ok = require_str_append(&msg, &len, &cap, "require: dependency cycle: ");

  for (i = start; ok && i < interp->require_stack_len; i++) {
    ok = require_str_append(&msg, &len, &cap, interp->require_stack[i].path);
    ok = ok && require_str_append(&msg, &len, &cap, " -> ");
  }

  ok = ok && require_str_append(&msg, &len, &cap, path);

  if (ok) {
    LCL_ERR_MSG_DUP(interp, msg);
    free(msg);
  } else {
    LCL_ERR_MSG(interp, "require: dependency cycle detected");
  }
}

/* require <path>
 *
 * Scoped load: evaluate <path> in a fresh frame parented to global,
 * collect every top-level binding whose value is a namespace, and lift
 * just those into the caller's scope. Loose let/var/proc bindings are
 * discarded. Subsequent calls with the same resolved absolute path are
 * served from cache without re-evaluating the file.
 *
 * Path resolution (see require_resolve): "/rooted" as-is, "./" and
 * "../" relative to the requiring file's lexical directory, bare
 * names via the registered search roots (or CWD when none are
 * registered). The cleaned lexical path is the module's identity --
 * the cache and cycle key -- unless the host installed a stronger
 * identity via lcl_set_module_key_fn (the key never affects
 * resolution or diagnostics, only identity). A file that requires
 * (directly or transitively) a module already being evaluated by
 * require is a dependency-cycle error.
 *
 * Differs from `load` in that:
 *   - `load` evaluates inline at the call site (textual include);
 *     `require` evaluates in an isolated frame (module import).
 *   - `load` runs the file every call; `require` caches by abs path.
 *   - `load` exposes every top-level binding; `require` exposes only
 *     namespaces (the explicit "module surface").
 *   - `load` stays CWD/caller-relative; only `require` implements the
 *     module-loader resolution contract. */
static lcl_return_code s_require(lcl_interp *interp, int argc,
                                 const lcl_word **args, lcl_value **out) {
  lcl_value *path_v = NULL;
  const char *path;
  char *mod_path = NULL;
  char *mod_key = NULL;
  lcl_value *cached_dict = NULL;
  char *src = NULL;
  lcl_program *prog = NULL;
  lcl_frame *overlay = NULL;
  lcl_frame *saved_frame = NULL;
  lcl_frame *global_frame = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int i;
  int saved_tail_position;
  const char *saved_cur_file;
  int saved_cur_line;
  hash_iter it = {0};
  const char *key;
  lcl_value *value;

  if (!lcl_std_chk_argc(interp, "require", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, path_v, &path) != LCL_OK) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  mod_path = require_resolve(interp, path);
  lcl_ref_dec(path_v);

  if (!mod_path) {
    return LCL_RC_ERR;
  }

  if (interp->module_key_fn) {
    mod_key = interp->module_key_fn(mod_path, interp->module_key_ud);
  }

  if (!mod_key) {
    mod_key = strdup(mod_path);
  }

  if (!mod_key) {
    free(mod_path);
    LCL_ERR_MSG(interp, "require: out of memory");
    return LCL_RC_ERR;
  }

  if (interp->require_cache &&
      lcl_dict_get(interp->require_cache, mod_key, &cached_dict) == LCL_OK) {
    lcl_result lift_rc = lift_namespaces_to_caller(interp, cached_dict);
    lcl_ref_dec(cached_dict);
    free(mod_path);
    free(mod_key);

    if (lift_rc != LCL_OK) {
      LCL_ERR_MSG(interp, "require: failed to bind cached namespace");
      return LCL_RC_ERR;
    }

    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  if (require_stack_contains(interp, mod_key)) {
    require_cycle_error(interp, mod_key, mod_path);
    free(mod_path);
    free(mod_key);
    return LCL_RC_ERR;
  }

  src = read_file(mod_path, NULL);

  if (!src) {
    char msg[192];

    snprintf(msg, sizeof(msg), "require: could not read file \"%.128s\"",
             mod_path);
    LCL_ERR_MSG_DUP(interp, msg);
    free(mod_path);
    free(mod_key);
    return LCL_RC_ERR;
  }

  prog = lcl_compile_report(interp, src, mod_path);
  free(src);

  if (!prog) {
    free(mod_path);
    free(mod_key);
    return LCL_RC_ERR;
  }

  global_frame = lcl_std_find_global_frame(interp->env.frame);
  overlay = lcl_frame_new(global_frame);

  if (!overlay) {
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: out of memory");
    return LCL_RC_ERR;
  }

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: max recursion depth exceeded");
    return LCL_RC_ERR;
  }

  if (!require_stack_push(interp, mod_key, mod_path)) {
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: out of memory");
    return LCL_RC_ERR;
  }

  saved_frame = interp->env.frame;
  saved_tail_position = interp->in_tail_position;
  saved_cur_file = interp->cur_file;
  saved_cur_line = interp->cur_line;
  interp->env.frame = overlay;
  interp->depth++;
  interp->in_tail_position = 0;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];

    interp->cur_file = prog->file;
    interp->cur_line = cmd->line;

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc != LCL_RC_OK) {
      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->depth--;
  interp->env.frame = saved_frame;
  interp->cur_file = saved_cur_file;
  interp->cur_line = saved_cur_line;
  require_stack_pop(interp);

  if (last) {
    lcl_ref_dec(last);
    last = NULL;
  }

  if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
    lcl_frame_clear(overlay);
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    return rc;
  }

  cached_dict = lcl_dict_new();

  if (!cached_dict) {
    lcl_frame_clear(overlay);
    lcl_frame_ref_dec(overlay);
    lcl_program_free(prog);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: out of memory creating cache entry");
    return LCL_RC_ERR;
  }

  while (hash_table_iterate(overlay->locals, &it, &key, &value)) {
    if (value->type == LCL_NAMESPACE) {
      if (lcl_dict_put(&cached_dict, key, value) != LCL_OK) {
        lcl_ref_dec(value);
        lcl_ref_dec(cached_dict);
        lcl_frame_clear(overlay);
        lcl_frame_ref_dec(overlay);
        lcl_program_free(prog);
        free(mod_path);
        free(mod_key);
        LCL_ERR_MSG(interp, "require: out of memory caching namespace");
        return LCL_RC_ERR;
      }
    }

    lcl_ref_dec(value);
  }

  lcl_frame_ref_dec(overlay);
  lcl_program_free(prog);

  if (!interp->require_cache) {
    interp->require_cache = lcl_dict_new();

    if (!interp->require_cache) {
      lcl_ref_dec(cached_dict);
      free(mod_path);
      free(mod_key);
      LCL_ERR_MSG(interp, "require: out of memory creating cache");
      return LCL_RC_ERR;
    }
  }

  if (lcl_dict_put(&interp->require_cache, mod_key, cached_dict) != LCL_OK) {
    lcl_ref_dec(cached_dict);
    free(mod_path);
    free(mod_key);
    LCL_ERR_MSG(interp, "require: failed to cache require result");
    return LCL_RC_ERR;
  }

  free(mod_path);
  free(mod_key);
  mod_path = NULL;
  mod_key = NULL;

  if (lift_namespaces_to_caller(interp, cached_dict) != LCL_OK) {
    lcl_ref_dec(cached_dict);
    LCL_ERR_MSG(interp, "require: failed to bind namespace into caller");
    return LCL_RC_ERR;
  }

  lcl_ref_dec(cached_dict);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

void lcl_std_register_module(lcl_interp *interp) {
  lcl_register_spec(interp, "load", s_load);
  lcl_register_spec(interp, "require", s_require);
}
