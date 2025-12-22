#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"

#include "lcl-stdlib.h"

int c_puts(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  int i;
  (void)interp;

  for (i = 0; i < argc; i++) {
    const char *str = lcl_value_to_string(argv[i]);
    fputs(str, stdout);

    if (i + 1 < argc) {
      fputc(' ', stdout);
    }
  }

  fputc('\n', stdout);
  fflush(stdout);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

int c_add(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  float sum = 0.0f;
  int i;
  float v;
  (void)interp;

  for (i = 0; i < argc; i++) {
    if (lcl_value_to_float(argv[i], &v) != LCL_OK) {
      return LCL_RC_ERR;
    }

    sum += v;
  }
  
  *out = lcl_float_new(sum);

  return LCL_RC_OK;
}

int c_let(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  if (argc != 2) return LCL_RC_ERR;

  /* hash_table_put inside lcl_env_let will incref the value */
  if (lcl_env_let(&interp->env, lcl_value_to_string(argv[0]), argv[1]) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_ref_inc(argv[1]);

  return LCL_RC_OK;
}

int c_ref(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  (void)interp;
  if (argc != 1) return LCL_RC_ERR;

  *out = lcl_cell_new(argv[0]);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

int c_ns(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argv;
  if (argc != 0) return LCL_RC_ERR;

  *out = lcl_ns_new(NULL);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

int c_ns_def(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  (void)interp;
  if (argc != 3) return LCL_RC_ERR;
  if (argv[0]->type != LCL_NAMESPACE) return LCL_RC_ERR;

  if (lcl_ns_def(argv[0], lcl_value_to_string(argv[1]), lcl_ref_inc(argv[2])) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_ref_inc(argv[2]);

  return LCL_RC_OK;
}

int c_get(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  lcl_value *val = NULL;

  if (argc != 1) return LCL_RC_ERR;

  if (lcl_env_get_value(&interp->env, lcl_value_to_string(argv[0]), &val) != LCL_OK) {
    return LCL_RC_ERR;
  }

  /* If the value is a cell, dereference it */
  if (val->type == LCL_CELL) {
    if (lcl_cell_get(val, out) != LCL_OK) {
      lcl_ref_dec(val);
      return LCL_RC_ERR;
    }
    lcl_ref_dec(val);
  } else {
    *out = val;
  }

  return LCL_RC_OK;
}

static int s_set_bang(lcl_interp *interp,
                      int argc,
                      const lcl_word **args,
                      lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *val_v = NULL;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[1], &val_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_set_bang(&interp->env,
                       lcl_value_to_string(name_v),
                       val_v) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    
    return LCL_RC_ERR;
  }
  
  lcl_ref_dec(name_v);
  *out = lcl_ref_inc(val_v);
  lcl_ref_dec(val_v);;
  
  return LCL_RC_OK;
}

int s_var(lcl_interp *interp, int argc, const lcl_word **argv, lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *init_v = NULL;

  if (argc != 2) return LCL_RC_ERR;

  if (lcl_eval_word_to_str(interp, argv[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, argv[1], &init_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_env_var(&interp->env, lcl_value_to_string(name_v), init_v) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(init_v);

    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(init_v);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

int s_return(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  if (argc == 0) {
    *out = lcl_string_new("");

    return LCL_RC_RETURN;
  }

  if (lcl_eval_word(interp, args[0], out) == LCL_RC_OK) {
    return LCL_RC_RETURN;
  }

  return LCL_RC_ERR;
}

lcl_value *lcl_list_new_from_cwords(const char *words) {
  lcl_value *list = lcl_list_new();
  const char *p = words;
  const char *start;

  if (!list) return NULL;
  if (!words) return list;

  while (*p) {
    /* Skip whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    }

    if (!*p) break;

    start = p;

    /* Find end of word */
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }

    if (p > start) {
      size_t len = (size_t)(p - start);
      char *word = (char *)malloc(len + 1);
      lcl_value *val;

      if (!word) {
        lcl_ref_dec(list);
        return NULL;
      }

      memcpy(word, start, len);
      word[len] = '\0';
      val = lcl_value_new_string(word);
      free(word);

      if (!val) {
        lcl_ref_dec(list);
        return NULL;
      }

      if (lcl_list_push(&list, val) != LCL_OK) {
        lcl_ref_dec(val);
        lcl_ref_dec(list);
        return NULL;
      }

      lcl_ref_dec(val);
    }
  }

  return list;
}

int s_lambda(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  lcl_value *params_s = NULL;
  lcl_value *body_s = NULL;
  lcl_program *body_p = NULL;
  lcl_value *params_list = NULL;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &params_s) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[1], &body_s) != LCL_RC_OK) {
    lcl_ref_dec(params_s);
    return LCL_RC_ERR;
  }

  /* TODO: proper Tcl list parser; MVP split on spaces */
  params_list = lcl_list_new_from_cwords(lcl_value_to_string(params_s));
  lcl_ref_dec(params_s);

  body_p = lcl_program_compile(lcl_value_to_string(body_s), "<lambda>");
  lcl_ref_dec(body_s);

  if (!body_p) {
    lcl_ref_dec(params_list);
    return LCL_RC_ERR;
  }

  /* lcl_proc_new takes ownership of body_p */
  *out = lcl_proc_new(interp->env.frame, params_list, body_p);
  lcl_ref_dec(params_list);

  if (!*out) {
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

static int is_name_char(int c) {
  return (c == '_' ||
          c == ':' ||
          (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9'));
}

static int is_name_start(int c) {
  return (c == '_' ||
          (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z'));
}

/* Helper: append n bytes to a dynamic buffer */
static int buf_append(char **buf, size_t *len, size_t *cap,
                      const char *s, size_t n) {
  if (*len + n + 1 > *cap) {
    size_t newcap = (*cap == 0) ? 64 : *cap * 2;
    char *newbuf;

    while (newcap < *len + n + 1) {
      newcap *= 2;
    }

    newbuf = realloc(*buf, newcap);
    if (!newbuf) return 0;

    *buf = newbuf;
    *cap = newcap;
  }

  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';

  return 1;
}

static int buf_append_char(char **buf, size_t *len, size_t *cap, char c) {
  return buf_append(buf, len, cap, &c, 1);
}

/* Helper: resolve or create a namespace path like "a::b::c".
 * Creates intermediate namespaces as needed.
 * Returns the final namespace with +1 refcount, or NULL on error. */
static lcl_value *resolve_or_create_ns_path(lcl_interp *interp, const char *path) {
  char first[256];
  const char *rest = NULL;
  lcl_value *current = NULL;

  /* Check for qualified name */
  if (!lcl_ns_split(path, first, sizeof(first), &rest)) {
    /* Simple name - look up or create in current frame */
    lcl_value *ns = NULL;

    if (lcl_env_get_value(&interp->env, path, &ns) == LCL_OK) {
      if (ns->type != LCL_NAMESPACE) {
        lcl_ref_dec(ns);
        return NULL;
      }

      return ns;
    }

    /* Create new namespace */
    ns = lcl_ns_new(path);

    if (!ns) return NULL;

    if (lcl_env_let(&interp->env, path, ns) != LCL_OK) {
      lcl_ref_dec(ns);
      return NULL;
    }

    return ns;  /* Already has refcount 1 from lcl_ns_new, +1 from hash_table_put */
  }

  /* Qualified name - resolve first part */
  if (lcl_env_get_value(&interp->env, first, &current) != LCL_OK) {
    /* Create first namespace */
    current = lcl_ns_new(first);

    if (!current) return NULL;

    if (lcl_env_let(&interp->env, first, current) != LCL_OK) {
      lcl_ref_dec(current);
      return NULL;
    }
  } else if (current->type != LCL_NAMESPACE) {
    lcl_ref_dec(current);
    return NULL;
  }

  /* Walk through rest of path */
  while (rest && *rest) {
    lcl_value *next = NULL;
    char part[256];
    const char *next_rest = NULL;
    const char *part_name;

    /* Try to split rest into part::next_rest */
    if (lcl_ns_split(rest, part, sizeof(part), &next_rest)) {
      part_name = part;
    } else {
      /* rest is the final part */
      part_name = rest;
      next_rest = NULL;
    }

    /* Look up or create this part in current namespace */
    if (lcl_ns_get(current, part_name, &next) == LCL_OK) {
      if (next->type != LCL_NAMESPACE) {
        lcl_ref_dec(next);
        lcl_ref_dec(current);
        return NULL;
      }
    } else {
      /* Create new namespace inside current */
      next = lcl_ns_new(part_name);

      if (!next) {
        lcl_ref_dec(current);
        return NULL;
      }

      /* ns_def will incref, so we need to decref after */
      if (hash_table_put(current->as.namespace.namespace, part_name, next) == 0) {
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

int s_namespace_eval(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_value *path_v = NULL;
  lcl_value *body_v = NULL;
  lcl_value *ns = NULL;
  lcl_program *prog = NULL;
  lcl_frame *ns_frame = NULL;
  lcl_frame *old_frame = NULL;
  lcl_return_code rc;
  int i;
  lcl_value *last = NULL;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  /* Evaluate namespace path */
  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Resolve or create namespace path */
  ns = resolve_or_create_ns_path(interp, lcl_value_to_string(path_v));
  lcl_ref_dec(path_v);

  if (!ns) {
    return LCL_RC_ERR;
  }

  /* Evaluate body string */
  if (lcl_eval_word_to_str(interp, args[1], &body_v) != LCL_RC_OK) {
    lcl_ref_dec(ns);
    return LCL_RC_ERR;
  }

  /* Compile body */
  prog = lcl_program_compile(lcl_value_to_string(body_v), "<namespace eval>");
  lcl_ref_dec(body_v);

  if (!prog) {
    lcl_ref_dec(ns);
    return LCL_RC_ERR;
  }

  /* Create namespace frame */
  ns_frame = lcl_frame_new_ns(interp->env.frame, ns->as.namespace.namespace);

  if (!ns_frame) {
    lcl_program_free(prog);
    lcl_ref_dec(ns);
    return LCL_RC_ERR;
  }

  /* Push namespace frame */
  old_frame = interp->env.frame;
  interp->env.frame = ns_frame;

  /* Evaluate body */
  if (interp->max_depth && interp->depth >= interp->max_depth) {
    interp->env.frame = old_frame;
    lcl_frame_ref_dec(ns_frame);
    lcl_program_free(prog);
    lcl_ref_dec(ns);
    return LCL_RC_ERR;
  }

  interp->depth++;
  rc = LCL_RC_OK;

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
        interp->err_file = prog->file;
      }

      break;
    }
  }

  interp->depth--;

  /* Pop namespace frame */
  interp->env.frame = old_frame;
  lcl_frame_ref_dec(ns_frame);
  lcl_program_free(prog);
  lcl_ref_dec(ns);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) lcl_ref_dec(last);
  }

  return rc;
}

int s_namespace(lcl_interp *interp, int argc, const lcl_word **args,
                lcl_value **out) {
  lcl_value *subcmd_v = NULL;
  const char *subcmd;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  /* Get subcommand name */
  if (lcl_eval_word_to_str(interp, args[0], &subcmd_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  subcmd = lcl_value_to_string(subcmd_v);

  if (strcmp(subcmd, "eval") == 0) {
    lcl_ref_dec(subcmd_v);

    /* namespace eval <nsPath> { body } */
    return s_namespace_eval(interp, argc - 1, args + 1, out);
  }

  /* Unknown subcommand */
  lcl_ref_dec(subcmd_v);

  return LCL_RC_ERR;
}

int s_subst(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  lcl_value *input_v = NULL;
  const char *src;
  size_t src_len;
  size_t i;
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  src = lcl_value_to_string(input_v);
  src_len = strlen(src);

  for (i = 0; i < src_len; ) {
    char c = src[i];

    /* Backslash escape */
    if (c == '\\' && i + 1 < src_len) {
      char next = src[i + 1];
      char esc;

      switch (next) {
        case 'n':  esc = '\n'; break;
        case 't':  esc = '\t'; break;
        case 'r':  esc = '\r'; break;
        case '\\': esc = '\\'; break;
        case '[':  esc = '[';  break;
        case ']':  esc = ']';  break;
        case '$':  esc = '$';  break;
        case '{':  esc = '{';  break;
        case '}':  esc = '}';  break;
        case '"':  esc = '"';  break;
        default:
          /* Unknown escape - keep both chars */
          if (!buf_append_char(&result, &result_len, &result_cap, '\\')) {
            goto err;
          }
          esc = next;
          break;
      }

      if (!buf_append_char(&result, &result_len, &result_cap, esc)) {
        goto err;
      }

      i += 2;
      continue;
    }

    /* Variable substitution */
    if (c == '$') {
      i++;

      /* ${name} form */
      if (i < src_len && src[i] == '{') {
        size_t start = ++i;

        while (i < src_len && src[i] != '}') {
          i++;
        }

        if (i >= src_len) {
          /* Unterminated ${...} */
          goto err;
        }

        {
          size_t name_len = i - start;
          char *name = malloc(name_len + 1);
          lcl_value *val = NULL;
          const char *val_str;

          if (!name) goto err;

          memcpy(name, src + start, name_len);
          name[name_len] = '\0';

          if (lcl_env_get_value(&interp->env, name, &val) != LCL_OK) {
            free(name);
            goto err;
          }

          free(name);

          /* Dereference cell if needed */
          if (val->type == LCL_CELL) {
            lcl_value *content = NULL;

            if (lcl_cell_get(val, &content) != LCL_OK) {
              lcl_ref_dec(val);
              goto err;
            }

            lcl_ref_dec(val);
            val = content;
          }

          val_str = lcl_value_to_string(val);

          if (!buf_append(&result, &result_len, &result_cap,
                          val_str, strlen(val_str))) {
            lcl_ref_dec(val);
            goto err;
          }

          lcl_ref_dec(val);
        }

        i++; /* skip closing } */
        continue;
      }

      /* $name form */
      if (i < src_len && is_name_start((unsigned char)src[i])) {
        size_t start = i;

        i++;

        while (i < src_len && is_name_char((unsigned char)src[i])) {
          i++;
        }

        {
          size_t name_len = i - start;
          char *name = malloc(name_len + 1);
          lcl_value *val = NULL;
          const char *val_str;

          if (!name) goto err;

          memcpy(name, src + start, name_len);
          name[name_len] = '\0';

          if (lcl_env_get_value(&interp->env, name, &val) != LCL_OK) {
            free(name);
            goto err;
          }

          free(name);

          /* Dereference cell if needed */
          if (val->type == LCL_CELL) {
            lcl_value *content = NULL;

            if (lcl_cell_get(val, &content) != LCL_OK) {
              lcl_ref_dec(val);
              goto err;
            }

            lcl_ref_dec(val);
            val = content;
          }

          val_str = lcl_value_to_string(val);

          if (!buf_append(&result, &result_len, &result_cap,
                          val_str, strlen(val_str))) {
            lcl_ref_dec(val);
            goto err;
          }

          lcl_ref_dec(val);
        }

        continue;
      }

      /* Bare $ - copy literally */
      if (!buf_append_char(&result, &result_len, &result_cap, '$')) {
        goto err;
      }

      continue;
    }

    /* Subcommand substitution */
    if (c == '[') {
      size_t start = ++i;
      int depth = 1;

      /* Find matching ] */
      while (i < src_len && depth > 0) {
        if (src[i] == '[') {
          depth++;
        } else if (src[i] == ']') {
          depth--;
        } else if (src[i] == '\\' && i + 1 < src_len) {
          i++; /* skip escaped char */
        }

        if (depth > 0) {
          i++;
        }
      }

      if (depth != 0) {
        /* Unterminated [...] */
        goto err;
      }

      {
        size_t subcmd_len = i - start;
        char *subcmd_src = malloc(subcmd_len + 1);
        lcl_program *prog;
        lcl_value *subcmd_result = NULL;
        int rc;
        const char *result_str;

        if (!subcmd_src) goto err;

        memcpy(subcmd_src, src + start, subcmd_len);
        subcmd_src[subcmd_len] = '\0';

        prog = lcl_program_compile(subcmd_src, "<subst>");
        free(subcmd_src);

        if (!prog) goto err;

        /* Evaluate in current frame (like eval) */
        if (interp->max_depth && interp->depth >= interp->max_depth) {
          lcl_program_free(prog);
          goto err;
        }

        interp->depth++;

        rc = LCL_RC_OK;

        {
          int j;

          for (j = 0; j < prog->ncmd; j++) {
            lcl_command *cmd = &prog->cmd[j];

            if (subcmd_result) {
              lcl_ref_dec(subcmd_result);
              subcmd_result = NULL;
            }

            rc = lcl_call_from_words(interp, cmd, &subcmd_result);

            if (rc != LCL_RC_OK) {
              if (rc != LCL_RC_RETURN) {
                interp->err_line = cmd->line;
                interp->err_file = prog->file;
              }

              break;
            }
          }
        }

        interp->depth--;
        lcl_program_free(prog);

        if (rc != LCL_RC_OK) {
          if (subcmd_result) lcl_ref_dec(subcmd_result);
          goto err;
        }

        result_str = subcmd_result ? lcl_value_to_string(subcmd_result) : "";

        if (!buf_append(&result, &result_len, &result_cap,
                        result_str, strlen(result_str))) {
          if (subcmd_result) lcl_ref_dec(subcmd_result);
          goto err;
        }

        if (subcmd_result) lcl_ref_dec(subcmd_result);
      }

      i++; /* skip closing ] */
      continue;
    }

    /* Regular character - copy as-is */
    if (!buf_append_char(&result, &result_len, &result_cap, c)) {
      goto err;
    }

    i++;
  }

  lcl_ref_dec(input_v);
  *out = lcl_value_new_string(result ? result : "");
  free(result);

  return *out ? LCL_RC_OK : LCL_RC_ERR;

err:
  lcl_ref_dec(input_v);
  free(result);

  return LCL_RC_ERR;
}

int s_eval(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  int i;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  /* MVP: single argument */
  if (argc == 1) {
    lcl_value *script_v = NULL;

    if (lcl_eval_word_to_str(interp, args[0], &script_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    prog = lcl_program_compile(lcl_value_to_string(script_v), "<eval>");
    lcl_ref_dec(script_v);
  } else {
    /* Join multiple args with spaces */
    size_t total_len = 0;
    lcl_value **parts = NULL;
    char *script_str = NULL;
    char *p;

    parts = malloc(sizeof(lcl_value *) * (size_t)argc);
    if (!parts) return LCL_RC_ERR;

    for (i = 0; i < argc; i++) {
      if (lcl_eval_word_to_str(interp, args[i], &parts[i]) != LCL_RC_OK) {
        int j;
        for (j = 0; j < i; j++) lcl_ref_dec(parts[j]);
        free(parts);
        return LCL_RC_ERR;
      }
      total_len += strlen(lcl_value_to_string(parts[i]));
    }

    /* Add space separators */
    total_len += (size_t)(argc - 1);

    script_str = malloc(total_len + 1);
    if (!script_str) {
      for (i = 0; i < argc; i++) lcl_ref_dec(parts[i]);
      free(parts);
      return LCL_RC_ERR;
    }

    p = script_str;
    for (i = 0; i < argc; i++) {
      const char *s = lcl_value_to_string(parts[i]);
      size_t l = strlen(s);
      memcpy(p, s, l);
      p += l;
      if (i + 1 < argc) {
        *p++ = ' ';
      }
    }
    *p = '\0';

    for (i = 0; i < argc; i++) lcl_ref_dec(parts[i]);
    free(parts);

    prog = lcl_program_compile(script_str, "<eval>");
    free(script_str);
  }

  if (!prog) {
    return LCL_RC_ERR;
  }

  /* Evaluate program in current frame, propagating RETURN (not absorbing it) */
  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

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
        interp->err_file = prog->file;
      }
      break;
    }
  }

  interp->depth--;
  lcl_program_free(prog);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) lcl_ref_dec(last);
  }

  return rc;
}

/* Helper: read entire file into a malloc'd string */
static char *read_file(const char *path, size_t *out_len) {
  FILE *f;
  long len;
  char *buf;
  size_t nread;

  f = fopen(path, "rb");
  if (!f) return NULL;

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

int s_load(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  lcl_value *path_v = NULL;
  const char *path;
  char *src = NULL;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int i;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  /* Get file path */
  if (lcl_eval_word_to_str(interp, args[0], &path_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(path_v);

  /* Read file contents */
  src = read_file(path, NULL);
  if (!src) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  /* Compile the file */
  prog = lcl_program_compile(src, path);
  free(src);

  if (!prog) {
    lcl_ref_dec(path_v);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(path_v);

  /* Evaluate in current frame (like eval) */
  if (interp->max_depth && interp->depth >= interp->max_depth) {
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

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
        interp->err_file = prog->file;
      }

      break;
    }
  }

  interp->depth--;
  lcl_program_free(prog);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) lcl_ref_dec(last);
  }

  return rc;
}

int s_proc(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out){
  /* proc name {params} {body} */
  lcl_value *name_v = NULL;
  lcl_value *lam = NULL;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  { /* build lambda from args[1], args[2] reusing s_lambda */
    const lcl_word *lam_args[2] = { args[1], args[2] };

    if (s_lambda(interp, 2, lam_args, &lam) != LCL_RC_OK) {
      lcl_ref_dec(name_v);
      
      return LCL_RC_ERR;
    }
  }

  /* hash_table_put inside lcl_env_let will incref lam */
  if (lcl_env_let(&interp->env,
                  lcl_value_to_string(name_v),
                  lam) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(lam);

    return LCL_RC_ERR;
  }

  /* lam now has refcount 2 (original + hash table), decref to balance */
  lcl_ref_dec(name_v);
  lcl_ref_dec(lam);
  *out = lcl_string_new("");
  
  return LCL_RC_OK;
}

void lcl_register_core(lcl_interp *interp) {
  lcl_env_let_take(&interp->env, "puts",    lcl_c_proc_new("puts", c_puts));
  lcl_env_let_take(&interp->env, "+",       lcl_c_proc_new("+", c_add));
  lcl_env_let_take(&interp->env, "let",     lcl_c_proc_new("let", c_let));
  lcl_env_let_take(&interp->env, "ref",     lcl_c_proc_new("ref", c_ref));
  lcl_env_let_take(&interp->env, "ns",      lcl_c_proc_new("ns", c_ns));
  lcl_env_let_take(&interp->env, "ns::def", lcl_c_proc_new("ns::def", c_ns_def));
  lcl_env_let_take(&interp->env, "get",     lcl_c_proc_new("get", c_get));
  lcl_env_let_take(&interp->env, "var",     lcl_c_spec_new("var", s_var));
  lcl_env_let_take(&interp->env, "set!",    lcl_c_spec_new("set!", s_set_bang));
  lcl_env_let_take(&interp->env, "return",  lcl_c_spec_new("return", s_return));
  lcl_env_let_take(&interp->env, "lambda",  lcl_c_spec_new("lambda", s_lambda));
  lcl_env_let_take(&interp->env, "proc",    lcl_c_spec_new("proc", s_proc));
  lcl_env_let_take(&interp->env, "eval",    lcl_c_spec_new("eval", s_eval));
  lcl_env_let_take(&interp->env, "load",    lcl_c_spec_new("load", s_load));
  lcl_env_let_take(&interp->env, "subst",   lcl_c_spec_new("subst", s_subst));
  lcl_env_let_take(&interp->env, "namespace", lcl_c_spec_new("namespace", s_namespace));
}
