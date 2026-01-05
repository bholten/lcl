#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"

#ifdef LCL_HAVE_CURL
#include <lcl-curl.h>
#endif

#ifdef LCL_HAVE_IO
#include <lcl-io.h>
#endif

#ifdef LCL_HAVE_JSON
void lcl_register_json(lcl_interp *interp);
#endif

#ifdef LCL_HAVE_CRYPTO
void lcl_register_crypto(lcl_interp *interp);
#endif

#ifdef LCL_HAVE_PROCESS
void lcl_register_process(lcl_interp *interp);
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
    fprintf(stderr, "Test framework error at %s:%d",
            interp->err_file ? interp->err_file : "<unknown>",
            interp->err_line);
    if (interp->err_msg) {
      fprintf(stderr, ": %s", interp->err_msg);
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

void lcl_register_core(lcl_interp *interp);
int lcl_eval_file(lcl_interp *interp, const char *filepath, lcl_value **out);
int lcl_eval_string(lcl_interp *interp, const char *src, lcl_value **out);

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
    fprintf(stderr, "Error at %s:%d",
            interp->err_file ? interp->err_file : "<unknown>",
            interp->err_line);
    if (interp->err_msg) {
      fprintf(stderr, ": %s", interp->err_msg);
    }
    fprintf(stderr, "\n");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_interp_free(interp);

  return rc == LCL_RC_OK ? 0 : 1;
}
