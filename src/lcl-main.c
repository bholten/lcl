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

static const lcl_embedded_lib sh_lib = {"lib/sh/src/sh.lcl", lib_sh_src_sh_lcl,
                                        sizeof(lib_sh_src_sh_lcl)};
#endif

#ifdef LCL_HAVE_CURL_DSL_LIB
#include "curl-dsl-lib-data.h"

static const lcl_embedded_lib curl_dsl_lib = {
    "lib/curl-dsl/src/curl-dsl.lcl", lib_curl_dsl_src_curl_dsl_lcl,
    sizeof(lib_curl_dsl_src_curl_dsl_lcl)};
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

static lcl_interp *create_interp(void) {
  lcl_interp *interp = lcl_interp_new();
  if (!interp) {
    return NULL;
  }

  lcl_register_core(interp);

#ifdef LCL_HAVE_CURL
  lcl_register_curl(interp);
#endif

#ifdef LCL_HAVE_IO
  lcl_register_io(interp);
#endif

#ifdef LCL_HAVE_JSON
  lcl_register_json(interp);
#endif

#ifdef LCL_HAVE_CRYPTO
  lcl_register_crypto(interp);
#endif

#ifdef LCL_HAVE_TEST
  if (lcl_register_embedded_lib(interp, &test_framework_lib) != 0) {
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

#ifdef LCL_HAVE_EXPECT
  lcl_register_expect(interp);
  if (lcl_register_embedded_lib(interp, &expect_convenience_lib) != 0) {
    fprintf(stderr, "Warning: Failed to load expect convenience library\n");
  }
#endif

#ifdef LCL_HAVE_SH_LIB
  if (lcl_register_embedded_lib(interp, &sh_lib) != 0) {
    fprintf(stderr, "Warning: Failed to load sh library\n");
  }
#endif

#ifdef LCL_HAVE_CURL_DSL_LIB
  if (lcl_register_embedded_lib(interp, &curl_dsl_lib) != 0) {
    fprintf(stderr, "Warning: Failed to load curl-dsl library\n");
  }
#endif

  return interp;
}

int main(int argc, char **argv) {
  lcl_interp *interp;
  lcl_value *result = NULL;
  int rc;

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

    rc = lcl_eval_string(interp, argv[2], &result);
  }

  else if (strcmp(argv[1], "-") == 0) {
    char *src = read_stdin();

    if (!src) {
      fprintf(stderr, "Error: Failed to read stdin\n");
      lcl_interp_free(interp);
      return 1;
    }

    rc = lcl_eval_string(interp, src, &result);
    free(src);
  } else {
    rc = lcl_eval_file(interp, argv[1], &result);
  }

  if (rc != LCL_RC_OK) {
    const char *err_file = lcl_interp_error_file(interp);
    const char *err_msg = lcl_interp_error_msg(interp);
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

  return rc == LCL_RC_OK ? 0 : 1;
}
