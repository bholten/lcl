#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>

#ifdef LCL_HAVE_CURL
#include <lcl-curl.h>
#endif

#ifdef LCL_HAVE_IO
#include <lcl-io.h>
#endif

#ifdef LCL_HAVE_POSIX
#include <lcl-posix.h>
#endif

#ifdef LCL_HAVE_JSON
#include <lcl-json.h>
#endif

#ifdef LCL_HAVE_CRYPTO
#include <lcl-crypto.h>
#endif

#ifdef LCL_HAVE_PROCESS
#include <lcl-process.h>
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

#ifdef LCL_HAVE_JS
#include <lcl-js.h>
#endif

#ifdef LCL_HAVE_DOM_LIB
#include "dom-lib-data.h"

static const lcl_embedded_lib dom_lib = {
    "lib/dom/src/Dom.lcl", lib_dom_src_Dom_lcl, sizeof(lib_dom_src_Dom_lcl)};
#endif

#ifdef LCL_HAVE_EXPECT
#include "expect-data.h"
#include <lcl-expect.h>

static const lcl_embedded_lib expect_convenience_lib = {
    "packages/lcl-expect/src/expect.lcl", packages_lcl_expect_src_expect_lcl,
    sizeof(packages_lcl_expect_src_expect_lcl)};
#endif

#ifdef LCL_HAVE_TEST
#include "test-framework-data.h"

static const lcl_embedded_lib test_framework_lib = {
    "lib/test/src/Test.lcl", lib_test_src_Test_lcl,
    sizeof(lib_test_src_Test_lcl)};
#endif

#ifdef LCL_HAVE_SH_LIB
#include "sh-lib-data.h"

static const lcl_embedded_lib sh_lib = {"lib/sh/src/Sh.lcl", lib_sh_src_Sh_lcl,
                                        sizeof(lib_sh_src_Sh_lcl)};
#endif

#ifdef LCL_HAVE_CURL_DSL_LIB
#include "curl-dsl-lib-data.h"

static const lcl_embedded_lib curl_dsl_lib = {
    "lib/curl-dsl/src/curl-dsl.lcl", lib_curl_dsl_src_curl_dsl_lcl,
    sizeof(lib_curl_dsl_src_curl_dsl_lcl)};
#endif

#ifdef LCL_HAVE_BENCH_LIB
#include "bench-lib-data.h"

static const lcl_embedded_lib bench_lib = {"lib/bench/src/Bench.lcl",
                                           lib_bench_src_Bench_lcl,
                                           sizeof(lib_bench_src_Bench_lcl)};
#endif

#ifdef LCL_HAVE_DOC_LIB
#include "doc-lib-data.h"

static const lcl_embedded_lib doc_lib = {
    "lib/doc/src/Doc.lcl", lib_doc_src_Doc_lcl, sizeof(lib_doc_src_Doc_lcl)};
#endif

static char *read_stdin(void) {
  size_t capacity = 4096;
  size_t len = 0;
  char *buf = (char *)malloc(capacity);
  if (!buf) {
    return NULL;
  }

  while (!feof(stdin)) {
    size_t n = fread(buf + len, 1, capacity - len - 1, stdin);
    len += n;

    if (len + 1 >= capacity) {
      char *new_buf;
      capacity *= 2;
      new_buf = (char *)realloc(buf, capacity);

      if (!new_buf) {
        free(buf);
        return NULL;
      }

      buf = new_buf;
    }
  }
  buf[len] = '\0';
  return buf;
}

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s [options] <script.lcl> [args...]\n", prog);
  fprintf(stderr, "       %s -c <code>      Execute code directly\n", prog);
  fprintf(stderr, "       %s -              Read script from stdin\n", prog);
  fprintf(stderr, "       %s --version, -v  Print version and exit\n", prog);
}

static void print_version(void) {
  printf("lcl %s\n", lcl_version());
}

static const char *trace_filter;
static int trace_depth;

static int trace_wants(const char *name) {
  const char *p = trace_filter;
  size_t n = strlen(name);

  if (!p || strcmp(p, "1") == 0 || strcmp(p, "*") == 0) {
    return 1;
  }

  while (*p) {
    const char *end = strchr(p, ',');
    size_t seg = end ? (size_t)(end - p) : strlen(p);

    if (seg == n && strncmp(p, name, n) == 0) {
      return 1;
    }

    if (!end) {
      break;
    }

    p = end + 1;
  }

  return 0;
}

static void trace_hook(lcl_interp *interp, lcl_value *proc, const char *name,
                       int argc, lcl_value **argv, int entering,
                       void *userdata) {
  int i;
  (void)interp;
  (void)proc;
  (void)userdata;

  if (!trace_wants(name)) {
    return;
  }

  if (entering) {
    fprintf(stderr, "%*s> %s", trace_depth * 2, "", name);

    for (i = 0; i < argc; i++) {
      const char *a = lcl_value_to_string(argv[i]);

      if (a && strlen(a) > 60) {
        fprintf(stderr, " %.57s...", a);
      } else {
        fprintf(stderr, " %s", a ? a : "?");
      }
    }

    fputc('\n', stderr);
    trace_depth++;
  } else {
    if (trace_depth > 0) {
      trace_depth--;
    }

    fprintf(stderr, "%*s< %s\n", trace_depth * 2, "", name);
  }
}

static int exit_requested;
static int exit_status;

static lcl_return_code c_exit(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lcl_int code = 0;
  (void)out;

  if (argc > 1) {
    lcl_set_error(interp, "exit: expected at most 1 argument");
    return LCL_RC_ERR;
  }

  if (argc == 1 && lcl_value_to_int(argv[0], &code) != LCL_OK) {
    lcl_set_error(interp, "exit: expected integer status");
    return LCL_RC_ERR;
  }

  exit_requested = 1;
  exit_status = (int)(code & 0xff);
  lcl_interp_abort(interp);
  lcl_set_error(interp, "exit");
  return LCL_RC_ERR;
}

/* `argv`: the arguments after the script (or after the -c code). */
static void define_argv(lcl_interp *interp, int argc, char **argv, int from) {
  lcl_value *list = lcl_list_new();
  int i;

  if (!list) {
    return;
  }

  for (i = from; i < argc; i++) {
    lcl_value *s = lcl_string_new(argv[i]);

    if (s) {
      lcl_list_push(&list, s);
      lcl_ref_dec(s);
    }
  }

  lcl_define_take(interp, "argv", list);
}

static lcl_interp *create_interp(void) {
  lcl_interp *interp = lcl_interp_new();
  if (!interp) {
    return NULL;
  }

  lcl_register_core(interp);
  lcl_register_proc(interp, "exit", c_exit);

  trace_filter = getenv("LCL_TRACE");

  if (trace_filter && trace_filter[0] != '\0' &&
      strcmp(trace_filter, "0") != 0) {
    lcl_set_call_hook(interp, trace_hook, NULL);
  }

#ifdef LCL_HAVE_CURL
  lcl_register_curl(interp);
#endif

#ifdef LCL_HAVE_IO
  lcl_register_io(interp);
#endif

#ifdef LCL_HAVE_POSIX
  lcl_register_posix(interp);
#endif

#ifdef LCL_HAVE_JSON
  lcl_register_json(interp);
#endif

#ifdef LCL_HAVE_CRYPTO
  lcl_register_crypto(interp);
#endif

#ifdef LCL_HAVE_TEST
  if (lcl_register_embedded_lib(interp, &test_framework_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load test framework\n");
  }
#endif

#ifdef LCL_HAVE_PROCESS
  lcl_register_process(interp);
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

#ifdef LCL_HAVE_JS
  lcl_register_js(interp);
#endif

#ifdef LCL_HAVE_DOM_LIB
  if (lcl_register_embedded_lib(interp, &dom_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load dom library\n");
  }
#endif

#ifdef LCL_HAVE_EXPECT
  lcl_register_expect(interp);
  if (lcl_register_embedded_lib(interp, &expect_convenience_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load expect convenience library\n");
  }
#endif

#ifdef LCL_HAVE_SH_LIB
  if (lcl_register_embedded_lib(interp, &sh_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load sh library\n");
  }
#endif

#ifdef LCL_HAVE_CURL_DSL_LIB
  if (lcl_register_embedded_lib(interp, &curl_dsl_lib) != LCL_OK) {
    fprintf(stderr, "Warning: Failed to load curl-dsl library\n");
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

int main(int argc, char **argv) {
  lcl_interp *interp;
  lcl_value *result = NULL;
  lcl_return_code rc;

  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
    print_version();
    return 0;
  }

  interp = create_interp();

  if (!interp) {
    fprintf(stderr, "Failed to create interpreter\n");
    return 1;
  }

  if (strcmp(argv[1], "-c") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Error: -c requires a code argument\n");
      print_usage(argv[0]);
      lcl_interp_free(interp);
      return 1;
    }

    define_argv(interp, argc, argv, 3);
    rc = lcl_eval_string(interp, argv[2], &result);
  }

  else if (strcmp(argv[1], "-") == 0) {
    char *src = read_stdin();

    if (!src) {
      fprintf(stderr, "Error: Failed to read stdin\n");
      lcl_interp_free(interp);
      return 1;
    }

    define_argv(interp, argc, argv, 2);
    rc = lcl_eval_string(interp, src, &result);
    free(src);
  } else {
    define_argv(interp, argc, argv, 2);
    rc = lcl_eval_file(interp, argv[1], &result);
  }

  if (rc != LCL_RC_OK && !exit_requested) {
    const char *err_file = lcl_interp_error_file(interp);
    const char *err_msg = lcl_interp_error_msg(interp);

    if (!err_msg && rc == LCL_RC_BREAK) {
      err_msg = "break invoked outside a loop";
    } else if (!err_msg && rc == LCL_RC_CONTINUE) {
      err_msg = "continue invoked outside a loop";
    }

    fprintf(stderr, "Error at %s:%d", err_file ? err_file : "<unknown>",
            lcl_interp_error_line(interp));

    if (err_msg) {
      fprintf(stderr, ": %s", err_msg);
    }

    fprintf(stderr, "\n");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_interp_free(interp);

  if (exit_requested) {
    return exit_status;
  }

  return rc == LCL_RC_OK ? 0 : 1;
}
