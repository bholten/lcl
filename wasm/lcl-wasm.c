/*
 * lcl-wasm.c -- the WebAssembly host.
 *
 * The browser counterpart of lcl-main.c: it wires the core, the
 * portable packages and the embedded pure-Lcl libraries into an
 * interpreter and exposes a small, flat `lclw_` surface that the
 * JavaScript wrapper (wasm/lcl.mjs) drives through cwrap/ccall.
 *
 * Everything JavaScript needs and the public C API cannot give it
 * directly lives here: out-parameters folded into return values,
 * `_take` variants so JS never juggles two references, and the host
 * procedure trampoline. The file is plain C89 on purpose -- no
 * emscripten.h, no EM_JS -- so it compiles under the same flags as
 * the rest of the tree; the export list is on the link line.
 *
 * Ownership follows include/lcl.h: a returned lcl_value carries +1
 * unless the comment says it is borrowed; a `_take` parameter is
 * consumed even on failure.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl-version.h>
#include <lcl.h>

#ifdef LCL_HAVE_IO
#include <lcl-io.h>
#endif

#ifdef LCL_HAVE_JSON
#include <lcl-json.h>
#endif

#ifdef LCL_HAVE_REGEX
#include <lcl-regex.h>
#endif

#ifdef LCL_HAVE_TIME
#include <lcl-time.h>
#endif

#ifdef LCL_HAVE_MATH
#include <lcl-math.h>
#endif

#ifdef LCL_HAVE_RANDOM
#include <lcl-random.h>
#endif

#ifdef LCL_HAVE_TEST
#include "test-framework-data.h"

static const lcl_embedded_lib test_framework_lib = {
    "lib/test/src/Test.lcl", lib_test_src_Test_lcl,
    sizeof(lib_test_src_Test_lcl)};
#endif

#ifdef LCL_HAVE_DOC_LIB
#include "doc-lib-data.h"

static const lcl_embedded_lib doc_lib = {
    "lib/doc/src/Doc.lcl", lib_doc_src_Doc_lcl, sizeof(lib_doc_src_Doc_lcl)};
#endif

#ifdef LCL_HAVE_BENCH_LIB
#include "bench-lib-data.h"

static const lcl_embedded_lib bench_lib = {"lib/bench/src/Bench.lcl",
                                           lib_bench_src_Bench_lcl,
                                           sizeof(lib_bench_src_Bench_lcl)};
#endif

typedef int (*lclw_host_fn)(lcl_interp *interp, long id, lcl_value *args,
                            lcl_value **out);

typedef int (*lclw_step_fn)(lcl_interp *interp);

struct lclw_host {
  lclw_host_fn host_fn;
  lclw_step_fn step_fn;
};

static struct lclw_host *host_of(lcl_interp *interp) {
  return (struct lclw_host *)lcl_interp_get_user_data(interp);
}

/* `::Wasm::_host id args`: the one C procedure every host proc forwards to. */
static lcl_return_code c_host(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct lclw_host *host = host_of(interp);
  long id;

  if (argc != 2 || lcl_value_to_int(argv[0], &id) != LCL_OK) {
    lcl_set_error(interp, "Wasm::_host: expected an integer id and an "
                          "argument list");
    return LCL_RC_ERR;
  }

  if (!host || !host->host_fn) {
    lcl_set_error(interp, "Wasm::_host: no host function installed");
    return LCL_RC_ERR;
  }

  *out = NULL;

  if (host->host_fn(interp, id, argv[1], out) != 0) {
    if (*out) {
      lcl_ref_dec(*out);
      *out = NULL;
    }

    if (!lcl_interp_error_msg(interp)) {
      lcl_set_error(interp, "host procedure failed");
    }

    return LCL_RC_ERR;
  }

  if (!*out) {
    *out = lcl_string_new("");

    if (!*out) {
      lcl_set_error(interp, "out of memory");
      return LCL_RC_ERR;
    }
  }

  return LCL_RC_OK;
}

static int step_thunk(lcl_interp *interp, void *userdata) {
  struct lclw_host *host = (struct lclw_host *)userdata;

  return host->step_fn ? host->step_fn(interp) : 0;
}

const char *lclw_version(void) {
  return LCL_VERSION_STRING;
}

lcl_interp *lclw_new(void) {
  lcl_interp *interp = lcl_interp_new();
  struct lclw_host *host;
  lcl_value *ns;

  if (!interp) {
    return NULL;
  }

  host = (struct lclw_host *)calloc(1, sizeof *host);

  if (!host) {
    lcl_interp_free(interp);
    return NULL;
  }

  lcl_interp_set_user_data(interp, host);
  lcl_register_core(interp);

  ns = lcl_ns_new("Wasm");

  if (!ns ||
      lcl_ns_def_take(ns, "_host", lcl_c_proc_new("_host", c_host)) != LCL_OK ||
      lcl_define_take(interp, "Wasm", ns) != LCL_OK) {

    if (ns) {
      lcl_ref_dec(ns);
    }

    lcl_interp_free(interp);
    free(host);

    return NULL;
  }

#ifdef LCL_HAVE_IO
  lcl_register_io(interp);
#endif
#ifdef LCL_HAVE_JSON
  lcl_register_json(interp);
#endif
#ifdef LCL_HAVE_REGEX
  lcl_register_regex(interp);
#endif
#ifdef LCL_HAVE_TIME
  lcl_register_time(interp);
#endif
#ifdef LCL_HAVE_MATH
  lcl_register_math(interp);
#endif
#ifdef LCL_HAVE_RANDOM
  lcl_register_random(interp);
#endif
#ifdef LCL_HAVE_TEST
  if (lcl_register_embedded_lib(interp, &test_framework_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load test framework\n");
  }
#endif
#ifdef LCL_HAVE_DOC_LIB
  if (lcl_register_embedded_lib(interp, &doc_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load doc library\n");
  }
#endif
#ifdef LCL_HAVE_BENCH_LIB
  if (lcl_register_embedded_lib(interp, &bench_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load bench library\n");
  }
#endif

  return interp;
}

void lclw_free(lcl_interp *interp) {
  struct lclw_host *host;

  if (!interp) {
    return;
  }

  host = host_of(interp);
  lcl_interp_free(interp);
  free(host);
}

void lclw_set_host_fn(lcl_interp *interp, lclw_host_fn fn) {
  host_of(interp)->host_fn = fn;
}

void lclw_set_step_hook(lcl_interp *interp, lclw_step_fn fn,
                        unsigned long interval) {
  struct lclw_host *host = host_of(interp);

  host->step_fn = fn;
  lcl_set_step_hook(interp, fn ? step_thunk : NULL, host, interval);
}

void lclw_abort(lcl_interp *interp) {
  lcl_interp_abort(interp);
}

/* Evaluate `src` as file `file`; the result (+1) or NULL on error. */
lcl_value *lclw_eval(lcl_interp *interp, const char *src, const char *file) {
  lcl_value *result = NULL;
  lcl_return_code rc = lcl_eval_string_file(interp, src, file, &result);

  if (rc == LCL_RC_OK) {
    return result ? result : lcl_string_new("");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  if (!lcl_interp_error_msg(interp)) {
    lcl_set_error(interp, rc == LCL_RC_BREAK ? "break invoked outside a loop"
                          : rc == LCL_RC_CONTINUE
                              ? "continue invoked outside a loop"
                              : "evaluation failed");
  }

  return NULL;
}

/* Call `proc` with the elements of `args` (borrowed); result (+1) or NULL. */
lcl_value *lclw_call(lcl_interp *interp, lcl_value *proc, lcl_value *args) {
  lcl_value *result = NULL;
  lcl_value **argv = NULL;
  size_t n = lcl_list_len(args);
  size_t i;
  lcl_return_code rc;

  if (!lcl_is_callable(proc)) {
    lcl_set_error(interp, "value is not callable");
    return NULL;
  }

  if (n > 0) {
    argv = (lcl_value **)calloc(n, sizeof *argv);

    if (!argv) {
      lcl_set_error(interp, "out of memory");
      return NULL;
    }

    for (i = 0; i < n; i++) {
      argv[i] = lcl_list_peek(args, i);
    }
  }

  rc = lcl_call_proc(interp, proc, (int)n, argv, &result);
  free(argv);

  if (rc != LCL_RC_OK) {
    if (result) {
      lcl_ref_dec(result);
    }

    return NULL;
  }

  return result ? result : lcl_string_new("");
}

/*
 * Define `name` as a procedure that forwards its arguments to host
 * function `id`. The proc is declared in Lcl so the name goes through
 * the ordinary declaration grammar (qualified names need their
 * namespace to exist). Returns 0, or -1 with the error set.
 */
int lclw_define_host_proc(lcl_interp *interp, const char *name, long id) {
  char buf[512];
  lcl_value *result = NULL;
  lcl_return_code rc;

  if (strlen(name) > 256 || strpbrk(name, " \t\n{}[]$\"") != NULL) {
    lcl_set_error(interp, "host procedure name is not a valid identifier");
    return -1;
  }

  sprintf(buf, "proc %s {*args} { ::Wasm::_host %ld $args }", name, id);
  rc = lcl_eval_string_file(interp, buf, "<host>", &result);

  if (result) {
    lcl_ref_dec(result);
  }

  return rc == LCL_RC_OK ? 0 : -1;
}

int lclw_define_take(lcl_interp *interp, const char *name, lcl_value *value) {
  return lcl_define_take(interp, name, value) == LCL_OK ? 0 : -1;
}

/* Resolve `name` from the root scope; +1 or NULL with the error set. */
lcl_value *lclw_get(lcl_interp *interp, const char *name) {
  lcl_value *out = NULL;

  if (lcl_get(interp, name, &out) != LCL_OK) {
    if (!lcl_interp_error_msg(interp)) {
      lcl_set_error(interp, "no such variable");
    }
    return NULL;
  }
  return out;
}

/* ---- value helpers: out-parameters folded away for cwrap ------------- */

int lclw_is_callable(lcl_value *value) {
  return lcl_is_callable(value);
}

/* Numeric payload of an int or float value as a double (0.0 otherwise). */
double lclw_number_of(lcl_value *value) {
  long i;
  double f;

  /* Dispatch on the tag: lcl_value_to_int would truncate a float. */
  if (lcl_value_type_of(value) == LCL_FLOAT) {
    return lcl_value_to_float(value, &f) == LCL_OK ? f : 0.0;
  }

  return lcl_value_to_int(value, &i) == LCL_OK ? (double)i : 0.0;
}

/* Append `value` (consumed) to `list` (consumed); the resulting list (+1). */
lcl_value *lclw_list_push_take(lcl_value *list, lcl_value *value) {
  lcl_result r = lcl_list_push(&list, value);

  lcl_ref_dec(value);

  if (r != LCL_OK) {
    lcl_ref_dec(list);
    return NULL;
  }

  return list;
}

/* Store `value` (consumed) under `key` in `dict` (consumed); the dict (+1). */
lcl_value *lclw_dict_put_take(lcl_value *dict, const char *key,
                              lcl_value *value) {
  lcl_result r = lcl_dict_put(&dict, key, value);

  lcl_ref_dec(value);

  if (r != LCL_OK) {
    lcl_ref_dec(dict);
    return NULL;
  }

  return dict;
}

/* The keys of `dict` as a fresh list (+1), or NULL. */
lcl_value *lclw_dict_keys(lcl_value *dict) {
  lcl_value *keys = NULL;

  if (lcl_dict_keys(dict, &keys) != LCL_OK) {
    return NULL;
  }

  return keys;
}
