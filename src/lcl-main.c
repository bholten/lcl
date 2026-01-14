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

#ifdef LCL_HAVE_TEST
#include "test-framework-data.h"

int lcl_register_test_framework(lcl_interp *interp) {
  lcl_value *result = NULL;
  int rc;
  char *src;

  src = (char *)malloc(lib_Test_lcl_len + 1);
  if (!src) {
    return -1;
  }

  memcpy(src, lib_Test_lcl, lib_Test_lcl_len);
  src[lib_Test_lcl_len] = '\0';

  rc = lcl_eval_string(interp, src, &result);

  if (rc != LCL_RC_OK) {
    const char *err_file = lcl_interp_error_file(interp);
    const char *err_msg = lcl_interp_error_msg(interp);
    fprintf(stderr, "Test framework error at %s:%d",
            err_file ? err_file : "<unknown>", lcl_interp_error_line(interp));
    if (err_msg) {
      fprintf(stderr, ": %s", err_msg);
    }
    fprintf(stderr, "\n");
  }

  free(src);

  if (result) {
    lcl_ref_dec(result);
  }

  return rc == LCL_RC_OK ? 0 : -1;
}
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
      capacity *= 2;
      buf = (char *)realloc(buf, capacity);
      if (!buf) {
        return NULL;
      }
    }
  }
  buf[len] = '\0';
  return buf;
}

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s [options] <script.lcl> [args...]\n", prog);
  fprintf(stderr, "       %s -c <code>    Execute code directly\n", prog);
  fprintf(stderr, "       %s -            Read script from stdin\n", prog);
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
  if (lcl_register_test_framework(interp) != 0) {
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
