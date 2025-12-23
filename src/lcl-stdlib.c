#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"

#include "lcl-stdlib.h"

/* Forward declarations */
lcl_value *lcl_list_new_from_cwords(const char *words);

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

  /* Use lcl_eval_word to preserve value type (e.g., lists) */
  if (lcl_eval_word(interp, args[1], &val_v) != LCL_RC_OK) {
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
  lcl_ref_dec(val_v);

  return LCL_RC_OK;
}

int s_var(lcl_interp *interp, int argc, const lcl_word **argv, lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *init_v = NULL;

  if (argc != 2) return LCL_RC_ERR;

  if (lcl_eval_word_to_str(interp, argv[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Use lcl_eval_word to preserve value type (e.g., lists) */
  if (lcl_eval_word(interp, argv[1], &init_v) != LCL_RC_OK) {
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

/* break - exit from innermost loop */
int s_break(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  (void)interp;
  (void)args;

  if (argc != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_BREAK;
}

/* continue - skip to next iteration of innermost loop */
int s_continue(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  (void)interp;
  (void)args;

  if (argc != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_CONTINUE;
}

/* Helper: check if a value is "truthy" (non-zero number or non-empty string) */
static int lcl_value_is_true(lcl_value *v) {
  const char *s;
  long n;
  char *endptr;

  if (!v) return 0;

  /* Integer type: non-zero is true */
  if (v->type == LCL_INT) {
    return v->as.i != 0;
  }

  /* Float type: non-zero is true */
  if (v->type == LCL_FLOAT) {
    return v->as.f != 0.0;
  }

  /* String: try to parse as number */
  s = lcl_value_to_string(v);
  if (!s || *s == '\0') return 0;  /* empty string is false */

  n = strtol(s, &endptr, 10);
  if (*endptr == '\0') {
    /* Successfully parsed as integer */
    return n != 0;
  }

  /* Non-numeric non-empty string is true */
  return 1;
}

/* if condition body ?elseif condition body ...? ?else body? */
int s_if(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  int i = 0;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  while (i < argc) {
    lcl_value *cond_v = NULL;
    lcl_value *body_v = NULL;
    lcl_program *body_p = NULL;
    int is_true;
    int rc;

    /* Check for 'else' keyword (must be followed by body) */
    if (i > 0) {
      lcl_value *kw = NULL;
      const char *kw_str;

      if (lcl_eval_word_to_str(interp, args[i], &kw) != LCL_RC_OK) {
        return LCL_RC_ERR;
      }

      kw_str = lcl_value_to_string(kw);

      if (strcmp(kw_str, "else") == 0) {
        lcl_ref_dec(kw);

        if (i + 1 >= argc) {
          return LCL_RC_ERR;  /* else requires body */
        }

        /* Evaluate else body */
        if (lcl_eval_word_to_str(interp, args[i + 1], &body_v) != LCL_RC_OK) {
          return LCL_RC_ERR;
        }

        body_p = lcl_program_compile(lcl_value_to_string(body_v), "<if-else>");
        lcl_ref_dec(body_v);

        if (!body_p) {
          return LCL_RC_ERR;
        }

        rc = lcl_eval_program(interp, body_p, out);
        lcl_program_free(body_p);

        return rc;
      }

      if (strcmp(kw_str, "elseif") == 0) {
        lcl_ref_dec(kw);
        i++;  /* skip 'elseif', process condition+body below */

        if (i + 1 >= argc) {
          return LCL_RC_ERR;  /* elseif requires condition and body */
        }
      } else {
        lcl_ref_dec(kw);
        return LCL_RC_ERR;  /* unexpected token */
      }
    }

    /* Evaluate condition */
    if (lcl_eval_word(interp, args[i], &cond_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (is_true) {
      /* Evaluate body */
      if (lcl_eval_word_to_str(interp, args[i + 1], &body_v) != LCL_RC_OK) {
        return LCL_RC_ERR;
      }

      body_p = lcl_program_compile(lcl_value_to_string(body_v), "<if>");
      lcl_ref_dec(body_v);

      if (!body_p) {
        return LCL_RC_ERR;
      }

      rc = lcl_eval_program(interp, body_p, out);
      lcl_program_free(body_p);

      return rc;
    }

    /* Condition was false, skip to next clause */
    i += 2;
  }

  /* No condition was true and no else clause */
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* while test body - loop while test is true, re-evaluating test each iteration */
int s_while(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  lcl_value *body_v = NULL;
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  lcl_value *last = NULL;
  int test_is_braced;
  int rc;

  if (argc != 2) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[0]->braced;

  /* If test is braced, compile it as a script to evaluate each iteration */
  if (test_is_braced) {
    lcl_value *test_v = NULL;
    if (lcl_eval_word_to_str(interp, args[0], &test_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }
    test_p = lcl_program_compile(lcl_value_to_string(test_v), "<while-test>");
    lcl_ref_dec(test_v);
    if (!test_p) {
      return LCL_RC_ERR;
    }
  }

  /* Compile body script */
  if (lcl_eval_word_to_str(interp, args[1], &body_v) != LCL_RC_OK) {
    if (test_p) lcl_program_free(test_p);
    return LCL_RC_ERR;
  }
  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<while-body>");
  lcl_ref_dec(body_v);
  if (!body_p) {
    if (test_p) lcl_program_free(test_p);
    return LCL_RC_ERR;
  }

  /* Loop while condition is true */
  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    /* Evaluate test each iteration */
    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);
      if (rc != LCL_RC_OK) {
        lcl_program_free(test_p);
        lcl_program_free(body_p);
        if (last) lcl_ref_dec(last);
        return rc;
      }
    } else {
      /* Non-braced: evaluate word directly (handles $var) */
      if (lcl_eval_word(interp, args[0], &cond_v) != LCL_RC_OK) {
        lcl_program_free(body_p);
        if (last) lcl_ref_dec(last);
        return LCL_RC_ERR;
      }
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (!is_true) {
      break;
    }

    /* Execute body */
    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      if (test_p) lcl_program_free(test_p);
      lcl_program_free(body_p);
      if (last) lcl_ref_dec(last);
      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      if (test_p) lcl_program_free(test_p);
      lcl_program_free(body_p);
      *out = last;
      return LCL_RC_RETURN;
    }
  }

  if (test_p) lcl_program_free(test_p);
  lcl_program_free(body_p);
  *out = last ? last : lcl_string_new("");
  return LCL_RC_OK;
}

/* for start test next body - Tcl-style for loop */
int s_for(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  lcl_value *start_v = NULL;
  lcl_value *body_v = NULL;
  lcl_value *next_v = NULL;
  lcl_program *start_p = NULL;
  lcl_program *test_p = NULL;
  lcl_program *body_p = NULL;
  lcl_program *next_p = NULL;
  lcl_value *last = NULL;
  lcl_value *tmp = NULL;
  int test_is_braced;
  int rc;

  if (argc != 4) {
    return LCL_RC_ERR;
  }

  test_is_braced = args[1]->braced;

  /* Compile start script */
  if (lcl_eval_word_to_str(interp, args[0], &start_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }
  start_p = lcl_program_compile(lcl_value_to_string(start_v), "<for-start>");
  lcl_ref_dec(start_v);
  if (!start_p) {
    return LCL_RC_ERR;
  }

  /* If test is braced, compile it as a script to evaluate each iteration */
  if (test_is_braced) {
    lcl_value *test_v = NULL;
    if (lcl_eval_word_to_str(interp, args[1], &test_v) != LCL_RC_OK) {
      lcl_program_free(start_p);
      return LCL_RC_ERR;
    }
    test_p = lcl_program_compile(lcl_value_to_string(test_v), "<for-test>");
    lcl_ref_dec(test_v);
    if (!test_p) {
      lcl_program_free(start_p);
      return LCL_RC_ERR;
    }
  }

  /* Compile next script (args[2]) */
  if (lcl_eval_word_to_str(interp, args[2], &next_v) != LCL_RC_OK) {
    lcl_program_free(start_p);
    if (test_p) lcl_program_free(test_p);
    return LCL_RC_ERR;
  }
  next_p = lcl_program_compile(lcl_value_to_string(next_v), "<for-next>");
  lcl_ref_dec(next_v);
  if (!next_p) {
    lcl_program_free(start_p);
    if (test_p) lcl_program_free(test_p);
    return LCL_RC_ERR;
  }

  /* Compile body script (args[3]) */
  if (lcl_eval_word_to_str(interp, args[3], &body_v) != LCL_RC_OK) {
    lcl_program_free(start_p);
    if (test_p) lcl_program_free(test_p);
    lcl_program_free(next_p);
    return LCL_RC_ERR;
  }
  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<for-body>");
  lcl_ref_dec(body_v);
  if (!body_p) {
    lcl_program_free(start_p);
    if (test_p) lcl_program_free(test_p);
    lcl_program_free(next_p);
    return LCL_RC_ERR;
  }

  /* Execute start script once */
  rc = lcl_eval_program(interp, start_p, &tmp);
  lcl_program_free(start_p);
  if (tmp) lcl_ref_dec(tmp);
  if (rc != LCL_RC_OK) {
    if (test_p) lcl_program_free(test_p);
    lcl_program_free(body_p);
    lcl_program_free(next_p);
    return rc;
  }

  /* Loop while condition is true */
  for (;;) {
    lcl_value *cond_v = NULL;
    int is_true;

    /* Evaluate test each iteration */
    if (test_is_braced) {
      rc = lcl_eval_program(interp, test_p, &cond_v);
      if (rc != LCL_RC_OK) {
        lcl_program_free(test_p);
        lcl_program_free(body_p);
        lcl_program_free(next_p);
        if (last) lcl_ref_dec(last);
        return rc;
      }
    } else {
      /* Non-braced: evaluate word directly */
      if (lcl_eval_word(interp, args[1], &cond_v) != LCL_RC_OK) {
        lcl_program_free(body_p);
        lcl_program_free(next_p);
        if (last) lcl_ref_dec(last);
        return LCL_RC_ERR;
      }
    }

    is_true = lcl_value_is_true(cond_v);
    lcl_ref_dec(cond_v);

    if (!is_true) {
      break;
    }

    /* Execute body */
    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      /* Still execute next before continuing */
      tmp = NULL;
      rc = lcl_eval_program(interp, next_p, &tmp);
      if (tmp) lcl_ref_dec(tmp);
      if (rc != LCL_RC_OK && rc != LCL_RC_CONTINUE) {
        if (test_p) lcl_program_free(test_p);
        lcl_program_free(body_p);
        lcl_program_free(next_p);
        if (last) lcl_ref_dec(last);
        return rc;
      }
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      if (test_p) lcl_program_free(test_p);
      lcl_program_free(body_p);
      lcl_program_free(next_p);
      if (last) lcl_ref_dec(last);
      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      if (test_p) lcl_program_free(test_p);
      lcl_program_free(body_p);
      lcl_program_free(next_p);
      *out = last;
      return LCL_RC_RETURN;
    }

    /* Execute next script */
    tmp = NULL;
    rc = lcl_eval_program(interp, next_p, &tmp);
    if (tmp) lcl_ref_dec(tmp);
    if (rc != LCL_RC_OK) {
      if (test_p) lcl_program_free(test_p);
      lcl_program_free(body_p);
      lcl_program_free(next_p);
      if (last) lcl_ref_dec(last);
      return rc;
    }
  }

  if (test_p) lcl_program_free(test_p);
  lcl_program_free(body_p);
  lcl_program_free(next_p);
  *out = last ? last : lcl_string_new("");
  return LCL_RC_OK;
}

/* foreach varname list body - iterate over list elements */
int s_foreach(lcl_interp *interp, int argc, const lcl_word **args, lcl_value **out) {
  lcl_value *varname_v = NULL;
  lcl_value *list_v = NULL;
  lcl_value *body_v = NULL;
  lcl_program *body_p = NULL;
  lcl_value *last = NULL;
  const char *varname;
  int i, list_len;
  int rc;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  /* Get variable name */
  if (lcl_eval_word_to_str(interp, args[0], &varname_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }
  varname = lcl_value_to_string(varname_v);

  /* Evaluate the list */
  if (lcl_eval_word(interp, args[1], &list_v) != LCL_RC_OK) {
    lcl_ref_dec(varname_v);
    return LCL_RC_ERR;
  }

  /* If not already a list, try to parse as a list */
  if (list_v->type != LCL_LIST) {
    lcl_value *parsed = lcl_list_new_from_cwords(lcl_value_to_string(list_v));
    lcl_ref_dec(list_v);
    if (!parsed) {
      lcl_ref_dec(varname_v);
      return LCL_RC_ERR;
    }
    list_v = parsed;
  }

  /* Compile body script */
  if (lcl_eval_word_to_str(interp, args[2], &body_v) != LCL_RC_OK) {
    lcl_ref_dec(varname_v);
    lcl_ref_dec(list_v);
    return LCL_RC_ERR;
  }
  body_p = lcl_program_compile(lcl_value_to_string(body_v), "<foreach>");
  lcl_ref_dec(body_v);
  if (!body_p) {
    lcl_ref_dec(varname_v);
    lcl_ref_dec(list_v);
    return LCL_RC_ERR;
  }

  list_len = (int)lcl_list_len(list_v);

  /* Iterate over list elements */
  for (i = 0; i < list_len; i++) {
    lcl_value *elem = NULL;

    if (lcl_list_get(list_v, (size_t)i, &elem) != LCL_OK) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);
      if (last) lcl_ref_dec(last);
      return LCL_RC_ERR;
    }

    /* Bind element to variable (using let - rebinds each iteration) */
    if (lcl_env_let(&interp->env, varname, elem) != LCL_OK) {
      lcl_ref_dec(elem);
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);
      if (last) lcl_ref_dec(last);
      return LCL_RC_ERR;
    }
    lcl_ref_dec(elem);  /* env_let increments refcount */

    /* Execute body */
    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    rc = lcl_eval_program(interp, body_p, &last);

    if (rc == LCL_RC_BREAK) {
      break;
    }

    if (rc == LCL_RC_CONTINUE) {
      continue;
    }

    if (rc != LCL_RC_OK && rc != LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);
      if (last) lcl_ref_dec(last);
      return rc;
    }

    if (rc == LCL_RC_RETURN) {
      lcl_ref_dec(varname_v);
      lcl_ref_dec(list_v);
      lcl_program_free(body_p);
      *out = last;
      return LCL_RC_RETURN;
    }
  }

  lcl_ref_dec(varname_v);
  lcl_ref_dec(list_v);
  lcl_program_free(body_p);
  *out = last ? last : lcl_string_new("");
  return LCL_RC_OK;
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

/* ============================================================================
 * List Commands
 * ============================================================================ */

/* list ?value ...? - construct a list from arguments */
int c_list(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  int i;
  (void)interp;

  list = lcl_list_new();
  if (!list) return LCL_RC_ERR;

  for (i = 0; i < argc; i++) {
    if (lcl_list_push(&list, argv[i]) != LCL_OK) {
      lcl_ref_dec(list);
      return LCL_RC_ERR;
    }
  }

  *out = list;
  return LCL_RC_OK;
}

/* lindex list ?index ...? - get element(s) from list by index */
int c_lindex(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  long idx;
  (void)interp;

  if (argc < 1) return LCL_RC_ERR;

  list = argv[0];

  /* No index - return the list itself */
  if (argc == 1) {
    *out = lcl_ref_inc(list);
    return LCL_RC_OK;
  }

  /* Single index - simple case */
  if (argc == 2) {
    if (list->type != LCL_LIST) {
      /* If not a list, treat as single-element and check index */
      if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
        return LCL_RC_ERR;
      }
      if (idx == 0) {
        *out = lcl_ref_inc(list);
        return LCL_RC_OK;
      }
      /* Out of bounds - return empty string (Tcl behavior) */
      *out = lcl_string_new("");
      return LCL_RC_OK;
    }

    if (lcl_value_to_int(argv[1], &idx) != LCL_OK) {
      return LCL_RC_ERR;
    }

    /* Handle negative index or end-based index */
    if (idx < 0) {
      *out = lcl_string_new("");
      return LCL_RC_OK;
    }

    if (lcl_list_get(list, (size_t)idx, out) != LCL_OK) {
      /* Out of bounds - return empty string (Tcl behavior) */
      *out = lcl_string_new("");
    }
    return LCL_RC_OK;
  }

  /* Multiple indices - nested indexing */
  {
    lcl_value *current = lcl_ref_inc(list);
    int i;

    for (i = 1; i < argc; i++) {
      lcl_value *next = NULL;

      if (current->type != LCL_LIST) {
        if (lcl_value_to_int(argv[i], &idx) != LCL_OK) {
          lcl_ref_dec(current);
          return LCL_RC_ERR;
        }
        if (idx == 0) {
          /* current is the element */
          continue;
        }
        lcl_ref_dec(current);
        *out = lcl_string_new("");
        return LCL_RC_OK;
      }

      if (lcl_value_to_int(argv[i], &idx) != LCL_OK) {
        lcl_ref_dec(current);
        return LCL_RC_ERR;
      }

      if (idx < 0 || lcl_list_get(current, (size_t)idx, &next) != LCL_OK) {
        lcl_ref_dec(current);
        *out = lcl_string_new("");
        return LCL_RC_OK;
      }

      lcl_ref_dec(current);
      current = next;
    }

    *out = current;
    return LCL_RC_OK;
  }
}

/* llength list - get list length */
int c_llength(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  size_t len;
  (void)interp;

  if (argc != 1) return LCL_RC_ERR;

  if (argv[0]->type != LCL_LIST) {
    /* Non-list is treated as single-element list */
    *out = lcl_int_new(1);
    return LCL_RC_OK;
  }

  len = lcl_list_len(argv[0]);
  *out = lcl_int_new((long)len);
  return LCL_RC_OK;
}

/* lrange list first last - extract a slice */
int c_lrange(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  lcl_value *result;
  long first, last;
  size_t len, i;
  (void)interp;

  if (argc != 3) return LCL_RC_ERR;

  list = argv[0];

  if (list->type != LCL_LIST) {
    /* Non-list treated as single-element list */
    if (lcl_value_to_int(argv[1], &first) != LCL_OK) return LCL_RC_ERR;
    if (lcl_value_to_int(argv[2], &last) != LCL_OK) return LCL_RC_ERR;

    result = lcl_list_new();
    if (!result) return LCL_RC_ERR;

    if (first <= 0 && last >= 0) {
      if (lcl_list_push(&result, list) != LCL_OK) {
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }
    }
    *out = result;
    return LCL_RC_OK;
  }

  len = lcl_list_len(list);

  if (lcl_value_to_int(argv[1], &first) != LCL_OK) return LCL_RC_ERR;
  if (lcl_value_to_int(argv[2], &last) != LCL_OK) return LCL_RC_ERR;

  /* Normalize indices */
  if (first < 0) first = 0;
  if (last < 0) last = -1;
  if ((size_t)last >= len) last = (long)len - 1;

  result = lcl_list_new();
  if (!result) return LCL_RC_ERR;

  for (i = (size_t)first; i <= (size_t)last && i < len; i++) {
    lcl_value *elem = NULL;
    if (lcl_list_get(list, i, &elem) == LCL_OK) {
      if (lcl_list_push(&result, elem) != LCL_OK) {
        lcl_ref_dec(elem);
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }
      lcl_ref_dec(elem);
    }
  }

  *out = result;
  return LCL_RC_OK;
}

/* concat ?list ...? - concatenate lists */
int c_concat(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  lcl_value *result;
  int i;
  (void)interp;

  result = lcl_list_new();
  if (!result) return LCL_RC_ERR;

  for (i = 0; i < argc; i++) {
    lcl_value *arg = argv[i];

    if (arg->type == LCL_LIST) {
      size_t j, len = lcl_list_len(arg);
      for (j = 0; j < len; j++) {
        lcl_value *elem = NULL;
        if (lcl_list_get(arg, j, &elem) == LCL_OK) {
          if (lcl_list_push(&result, elem) != LCL_OK) {
            lcl_ref_dec(elem);
            lcl_ref_dec(result);
            return LCL_RC_ERR;
          }
          lcl_ref_dec(elem);
        }
      }
    } else {
      /* Non-list - add as single element */
      if (lcl_list_push(&result, arg) != LCL_OK) {
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }
    }
  }

  *out = result;
  return LCL_RC_OK;
}

/* join list ?separator? - join list elements with separator (default space) */
int c_join(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  const char *sep = " ";
  size_t sep_len;
  size_t len, i;
  char *buf = NULL;
  size_t buf_len = 0, buf_cap = 0;
  (void)interp;

  if (argc < 1 || argc > 2) return LCL_RC_ERR;

  list = argv[0];
  if (argc == 2) {
    sep = lcl_value_to_string(argv[1]);
  }
  sep_len = strlen(sep);

  if (list->type != LCL_LIST) {
    /* Non-list - return string representation */
    *out = lcl_string_new(lcl_value_to_string(list));
    return LCL_RC_OK;
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *elem_str;
    size_t elem_len;

    if (lcl_list_get(list, i, &elem) != LCL_OK) continue;

    elem_str = lcl_value_to_string(elem);
    elem_len = strlen(elem_str);

    /* Add separator if not first element */
    if (i > 0 && sep_len > 0) {
      if (!buf_append(&buf, &buf_len, &buf_cap, sep, sep_len)) {
        lcl_ref_dec(elem);
        free(buf);
        return LCL_RC_ERR;
      }
    }

    if (!buf_append(&buf, &buf_len, &buf_cap, elem_str, elem_len)) {
      lcl_ref_dec(elem);
      free(buf);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(elem);
  }

  *out = lcl_string_new(buf ? buf : "");
  free(buf);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* split string ?splitChars? - split string into list (default split on each char) */
int c_split(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  const char *str;
  const char *split_chars = NULL;
  lcl_value *result;
  (void)interp;

  if (argc < 1 || argc > 2) return LCL_RC_ERR;

  str = lcl_value_to_string(argv[0]);
  if (argc == 2) {
    split_chars = lcl_value_to_string(argv[1]);
  }

  result = lcl_list_new();
  if (!result) return LCL_RC_ERR;

  if (!split_chars || *split_chars == '\0') {
    /* Split on each character */
    const char *p = str;
    while (*p) {
      char c[2] = { *p, '\0' };
      lcl_value *elem = lcl_string_new(c);
      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
        if (elem) lcl_ref_dec(elem);
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }
      lcl_ref_dec(elem);
      p++;
    }
  } else {
    /* Split on any of the split characters */
    const char *p = str;
    const char *start = str;

    while (*p) {
      if (strchr(split_chars, *p)) {
        /* Found a split character */
        size_t len = (size_t)(p - start);
        char *word = (char *)malloc(len + 1);
        lcl_value *elem;

        if (!word) {
          lcl_ref_dec(result);
          return LCL_RC_ERR;
        }

        memcpy(word, start, len);
        word[len] = '\0';
        elem = lcl_string_new(word);
        free(word);

        if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
          if (elem) lcl_ref_dec(elem);
          lcl_ref_dec(result);
          return LCL_RC_ERR;
        }
        lcl_ref_dec(elem);
        start = p + 1;
      }
      p++;
    }

    /* Add remaining part */
    {
      lcl_value *elem = lcl_string_new(start);
      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
        if (elem) lcl_ref_dec(elem);
        lcl_ref_dec(result);
        return LCL_RC_ERR;
      }
      lcl_ref_dec(elem);
    }
  }

  *out = result;
  return LCL_RC_OK;
}

/* lappend varName ?value ...? - append values to list variable */
int s_lappend(lcl_interp *interp, int argc, const lcl_word **args,
              lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *cell = NULL;
  lcl_value *list = NULL;
  int i;

  if (argc < 1) return LCL_RC_ERR;

  /* Get variable name */
  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Look up the variable - must be a cell */
  if (lcl_env_get_value(&interp->env, lcl_value_to_string(name_v), &cell)
      != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (cell->type != LCL_CELL) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  /* Get the current list from the cell */
  if (lcl_cell_get(cell, &list) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  /* If not a list, convert to single-element list */
  if (list->type != LCL_LIST) {
    lcl_value *new_list = lcl_list_new();
    if (!new_list) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(list);
      return LCL_RC_ERR;
    }
    if (lcl_list_push(&new_list, list) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(list);
      lcl_ref_dec(new_list);
      return LCL_RC_ERR;
    }
    lcl_ref_dec(list);
    list = new_list;
  }

  /* Append each value */
  for (i = 1; i < argc; i++) {
    lcl_value *val = NULL;

    if (lcl_eval_word(interp, args[i], &val) != LCL_RC_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(list);
      return LCL_RC_ERR;
    }

    if (lcl_list_push(&list, val) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(list);
      lcl_ref_dec(val);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(val);
  }

  /* Update the cell with the new list */
  if (lcl_cell_set(cell, list) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(list);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(cell);
  *out = list;  /* Return the updated list */
  return LCL_RC_OK;
}

/* lset varName index value - set list element at index */
int s_lset(lcl_interp *interp, int argc, const lcl_word **args,
           lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *idx_v = NULL;
  lcl_value *val_v = NULL;
  lcl_value *cell = NULL;
  lcl_value *list = NULL;
  long idx;

  if (argc != 3) return LCL_RC_ERR;

  /* Get variable name */
  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Get index */
  if (lcl_eval_word(interp, args[1], &idx_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(idx_v, &idx) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(idx_v);
    return LCL_RC_ERR;
  }
  lcl_ref_dec(idx_v);

  /* Get value */
  if (lcl_eval_word(interp, args[2], &val_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  /* Look up the variable - must be a cell */
  if (lcl_env_get_value(&interp->env, lcl_value_to_string(name_v), &cell)
      != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    return LCL_RC_ERR;
  }

  if (cell->type != LCL_CELL) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  /* Get the current list from the cell */
  if (lcl_cell_get(cell, &list) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  if (list->type != LCL_LIST) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(list);
    return LCL_RC_ERR;
  }

  /* Set the element */
  if (idx < 0 || lcl_list_set(&list, (size_t)idx, val_v) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(list);
    return LCL_RC_ERR;
  }

  /* Update the cell with the modified list */
  if (lcl_cell_set(cell, list) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(val_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(list);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(cell);
  lcl_ref_dec(val_v);
  *out = list;  /* Return the updated list */
  return LCL_RC_OK;
}

/* ============================================================================
 * Dict Commands (ensemble)
 * ============================================================================ */

/* dict create ?key value ...? - create a dict from key-value pairs */
static int dict_create(lcl_interp *interp, int argc, const lcl_word **args,
                       lcl_value **out) {
  lcl_value *dict;
  int i;

  if (argc % 2 != 0) {
    return LCL_RC_ERR;  /* Must have even number of args */
  }

  dict = lcl_dict_new();
  if (!dict) return LCL_RC_ERR;

  for (i = 0; i < argc; i += 2) {
    lcl_value *key_v = NULL;
    lcl_value *val_v = NULL;

    if (lcl_eval_word_to_str(interp, args[i], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    if (lcl_eval_word(interp, args[i + 1], &val_v) != LCL_RC_OK) {
      lcl_ref_dec(key_v);
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    if (lcl_dict_put(&dict, lcl_value_to_string(key_v), val_v) != LCL_OK) {
      lcl_ref_dec(key_v);
      lcl_ref_dec(val_v);
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(key_v);
    lcl_ref_dec(val_v);
  }

  *out = dict;
  return LCL_RC_OK;
}

/* dict get dictValue ?key ...? - get value by key(s) */
static int dict_get(lcl_interp *interp, int argc, const lcl_word **args,
                    lcl_value **out) {
  lcl_value *dict = NULL;
  int i;

  if (argc < 1) return LCL_RC_ERR;

  if (lcl_eval_word(interp, args[0], &dict) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* No keys - return the dict itself */
  if (argc == 1) {
    *out = dict;
    return LCL_RC_OK;
  }

  /* Navigate through nested keys */
  for (i = 1; i < argc; i++) {
    lcl_value *key_v = NULL;
    lcl_value *val = NULL;

    if (dict->type != LCL_DICT) {
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    if (lcl_eval_word_to_str(interp, args[i], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    if (lcl_dict_get(dict, lcl_value_to_string(key_v), &val) != LCL_OK) {
      lcl_ref_dec(key_v);
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    /* lcl_dict_get already returns with +1 refcount */
    lcl_ref_dec(key_v);
    lcl_ref_dec(dict);
    dict = val;
  }

  *out = dict;
  return LCL_RC_OK;
}

/* dict size dictValue - get number of entries */
static int dict_size(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_value *dict = NULL;
  size_t len;

  if (argc != 1) return LCL_RC_ERR;

  if (lcl_eval_word(interp, args[0], &dict) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (dict->type != LCL_DICT) {
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  len = lcl_dict_len(dict);
  lcl_ref_dec(dict);

  *out = lcl_int_new((long)len);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* dict keys dictValue - get all keys as a list */
static int dict_keys(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  lcl_value *dict = NULL;
  lcl_value *result;
  hash_iter it = {0};
  const char *k;
  lcl_value *v;

  if (argc != 1) return LCL_RC_ERR;

  if (lcl_eval_word(interp, args[0], &dict) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (dict->type != LCL_DICT) {
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  result = lcl_list_new();
  if (!result) {
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &k, &v)) {
    lcl_value *key_v = lcl_string_new(k);
    lcl_ref_dec(v);  /* hash_table_iterate returns +1 refcount */
    if (!key_v || lcl_list_push(&result, key_v) != LCL_OK) {
      if (key_v) lcl_ref_dec(key_v);
      lcl_ref_dec(result);
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }
    lcl_ref_dec(key_v);
  }

  lcl_ref_dec(dict);
  *out = result;
  return LCL_RC_OK;
}

/* dict values dictValue - get all values as a list */
static int dict_values(lcl_interp *interp, int argc, const lcl_word **args,
                       lcl_value **out) {
  lcl_value *dict = NULL;
  lcl_value *result;
  hash_iter it = {0};
  const char *k;
  lcl_value *v;

  if (argc != 1) return LCL_RC_ERR;

  if (lcl_eval_word(interp, args[0], &dict) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (dict->type != LCL_DICT) {
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  result = lcl_list_new();
  if (!result) {
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &k, &v)) {
    int push_rc = lcl_list_push(&result, v);
    lcl_ref_dec(v);  /* hash_table_iterate returns +1 refcount */
    if (push_rc != LCL_OK) {
      lcl_ref_dec(result);
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }
  }

  lcl_ref_dec(dict);
  *out = result;
  return LCL_RC_OK;
}

/* dict exists dictValue key ?key ...? - check if key exists */
static int dict_exists(lcl_interp *interp, int argc, const lcl_word **args,
                       lcl_value **out) {
  lcl_value *dict = NULL;
  int i;

  if (argc < 2) return LCL_RC_ERR;

  if (lcl_eval_word(interp, args[0], &dict) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Navigate through nested keys */
  for (i = 1; i < argc; i++) {
    lcl_value *key_v = NULL;
    lcl_value *val = NULL;

    if (dict->type != LCL_DICT) {
      lcl_ref_dec(dict);
      *out = lcl_int_new(0);
      return *out ? LCL_RC_OK : LCL_RC_ERR;
    }

    if (lcl_eval_word_to_str(interp, args[i], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    if (lcl_dict_get(dict, lcl_value_to_string(key_v), &val) != LCL_OK) {
      lcl_ref_dec(key_v);
      lcl_ref_dec(dict);
      *out = lcl_int_new(0);
      return *out ? LCL_RC_OK : LCL_RC_ERR;
    }

    /* lcl_dict_get already returns with +1 refcount */
    lcl_ref_dec(key_v);
    lcl_ref_dec(dict);
    dict = val;
  }

  lcl_ref_dec(dict);
  *out = lcl_int_new(1);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* dict set dictVariable key ?key ...? value - set value in dict variable */
static int dict_set(lcl_interp *interp, int argc, const lcl_word **args,
                    lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *cell = NULL;
  lcl_value *dict = NULL;
  lcl_value *val_v = NULL;
  lcl_value *key_v = NULL;

  if (argc < 3) return LCL_RC_ERR;

  /* Get variable name */
  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Look up the variable - must be a cell */
  if (lcl_env_get_value(&interp->env, lcl_value_to_string(name_v), &cell)
      != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (cell->type != LCL_CELL) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  /* Get the current dict from the cell */
  if (lcl_cell_get(cell, &dict) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  if (dict->type != LCL_DICT) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  /* Get the value (last argument) */
  if (lcl_eval_word(interp, args[argc - 1], &val_v) != LCL_RC_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  /* Simple case: single key */
  if (argc == 3) {
    if (lcl_eval_word_to_str(interp, args[1], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(dict);
      lcl_ref_dec(val_v);
      return LCL_RC_ERR;
    }

    if (lcl_dict_put(&dict, lcl_value_to_string(key_v), val_v) != LCL_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(dict);
      lcl_ref_dec(val_v);
      lcl_ref_dec(key_v);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(key_v);
    lcl_ref_dec(val_v);  /* dict_put incremented refcount */
  } else {
    /* Nested keys - navigate and set */
    lcl_value **path_dicts;
    lcl_value **path_keys;
    int path_len = argc - 2;  /* number of keys */
    int i;

    path_dicts = (lcl_value **)calloc((size_t)path_len, sizeof(*path_dicts));
    path_keys = (lcl_value **)calloc((size_t)path_len, sizeof(*path_keys));

    if (!path_dicts || !path_keys) {
      free(path_dicts);
      free(path_keys);
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(dict);
      lcl_ref_dec(val_v);
      return LCL_RC_ERR;
    }

    path_dicts[0] = dict;

    /* Navigate to nested dicts, creating as needed */
    for (i = 0; i < path_len - 1; i++) {
      lcl_value *nested = NULL;

      if (lcl_eval_word_to_str(interp, args[i + 1], &path_keys[i]) != LCL_RC_OK) {
        goto nested_cleanup;
      }

      if (lcl_dict_get(path_dicts[i], lcl_value_to_string(path_keys[i]), &nested) != LCL_OK) {
        /* Create new nested dict */
        nested = lcl_dict_new();
        if (!nested) goto nested_cleanup;
      } else {
        /* lcl_dict_get already returns with +1 refcount */
        if (nested->type != LCL_DICT) {
          lcl_ref_dec(nested);
          goto nested_cleanup;
        }
      }

      path_dicts[i + 1] = nested;
    }

    /* Get final key */
    if (lcl_eval_word_to_str(interp, args[path_len], &path_keys[path_len - 1]) != LCL_RC_OK) {
      goto nested_cleanup;
    }

    /* Set value in innermost dict */
    if (lcl_dict_put(&path_dicts[path_len - 1],
                     lcl_value_to_string(path_keys[path_len - 1]),
                     val_v) != LCL_OK) {
      goto nested_cleanup;
    }

    /* Propagate changes back up */
    for (i = path_len - 2; i >= 0; i--) {
      if (lcl_dict_put(&path_dicts[i],
                       lcl_value_to_string(path_keys[i]),
                       path_dicts[i + 1]) != LCL_OK) {
        goto nested_cleanup;
      }
    }

    dict = path_dicts[0];

    /* Cleanup path */
    for (i = 1; i < path_len; i++) {
      if (path_dicts[i]) lcl_ref_dec(path_dicts[i]);
    }
    for (i = 0; i < path_len; i++) {
      if (path_keys[i]) lcl_ref_dec(path_keys[i]);
    }
    free(path_dicts);
    free(path_keys);
    lcl_ref_dec(val_v);  /* dict_put incremented refcount */
    goto after_nested;

nested_cleanup:
    for (i = 0; i < path_len; i++) {
      if (path_dicts[i] && i > 0) lcl_ref_dec(path_dicts[i]);
      if (path_keys[i]) lcl_ref_dec(path_keys[i]);
    }
    free(path_dicts);
    free(path_keys);
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    lcl_ref_dec(val_v);
    return LCL_RC_ERR;
  }

after_nested:
  /* Update the cell with the modified dict */
  if (lcl_cell_set(cell, dict) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(cell);
  *out = dict;
  return LCL_RC_OK;
}

/* dict unset dictVariable key ?key ...? - remove key from dict variable */
static int dict_unset(lcl_interp *interp, int argc, const lcl_word **args,
                      lcl_value **out) {
  lcl_value *name_v = NULL;
  lcl_value *cell = NULL;
  lcl_value *dict = NULL;
  lcl_value *key_v = NULL;

  if (argc < 2) return LCL_RC_ERR;

  /* Get variable name */
  if (lcl_eval_word_to_str(interp, args[0], &name_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  /* Look up the variable - must be a cell */
  if (lcl_env_get_value(&interp->env, lcl_value_to_string(name_v), &cell)
      != LCL_OK) {
    lcl_ref_dec(name_v);
    return LCL_RC_ERR;
  }

  if (cell->type != LCL_CELL) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  /* Get the current dict from the cell */
  if (lcl_cell_get(cell, &dict) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    return LCL_RC_ERR;
  }

  if (dict->type != LCL_DICT) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  /* Simple case: single key */
  if (argc == 2) {
    if (lcl_eval_word_to_str(interp, args[1], &key_v) != LCL_RC_OK) {
      lcl_ref_dec(name_v);
      lcl_ref_dec(cell);
      lcl_ref_dec(dict);
      return LCL_RC_ERR;
    }

    /* Delete the key (ignore if not found) */
    lcl_dict_del(&dict, lcl_value_to_string(key_v));
    lcl_ref_dec(key_v);
  } else {
    /* Nested unset - not implemented for MVP */
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  /* Update the cell with the modified dict */
  if (lcl_cell_set(cell, dict) != LCL_OK) {
    lcl_ref_dec(name_v);
    lcl_ref_dec(cell);
    lcl_ref_dec(dict);
    return LCL_RC_ERR;
  }

  lcl_ref_dec(name_v);
  lcl_ref_dec(cell);
  *out = dict;
  return LCL_RC_OK;
}

/* dict - ensemble command dispatcher */
int s_dict(lcl_interp *interp, int argc, const lcl_word **args,
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

  if (strcmp(subcmd, "create") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_create(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "get") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_get(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "size") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_size(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "keys") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_keys(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "values") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_values(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "exists") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_exists(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "set") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_set(interp, argc - 1, args + 1, out);
  }

  if (strcmp(subcmd, "unset") == 0) {
    lcl_ref_dec(subcmd_v);
    return dict_unset(interp, argc - 1, args + 1, out);
  }

  /* Unknown subcommand */
  lcl_ref_dec(subcmd_v);
  return LCL_RC_ERR;
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

  /* Control flow */
  lcl_env_let_take(&interp->env, "if",       lcl_c_spec_new("if", s_if));
  lcl_env_let_take(&interp->env, "while",    lcl_c_spec_new("while", s_while));
  lcl_env_let_take(&interp->env, "for",      lcl_c_spec_new("for", s_for));
  lcl_env_let_take(&interp->env, "foreach",  lcl_c_spec_new("foreach", s_foreach));
  lcl_env_let_take(&interp->env, "break",    lcl_c_spec_new("break", s_break));
  lcl_env_let_take(&interp->env, "continue", lcl_c_spec_new("continue", s_continue));

  /* List commands */
  lcl_env_let_take(&interp->env, "list",    lcl_c_proc_new("list", c_list));
  lcl_env_let_take(&interp->env, "lindex",  lcl_c_proc_new("lindex", c_lindex));
  lcl_env_let_take(&interp->env, "llength", lcl_c_proc_new("llength", c_llength));
  lcl_env_let_take(&interp->env, "lrange",  lcl_c_proc_new("lrange", c_lrange));
  lcl_env_let_take(&interp->env, "concat",  lcl_c_proc_new("concat", c_concat));
  lcl_env_let_take(&interp->env, "join",    lcl_c_proc_new("join", c_join));
  lcl_env_let_take(&interp->env, "split",   lcl_c_proc_new("split", c_split));
  lcl_env_let_take(&interp->env, "lappend", lcl_c_spec_new("lappend", s_lappend));
  lcl_env_let_take(&interp->env, "lset",    lcl_c_spec_new("lset", s_lset));

  /* Dict command (ensemble) */
  lcl_env_let_take(&interp->env, "dict",    lcl_c_spec_new("dict", s_dict));
}
