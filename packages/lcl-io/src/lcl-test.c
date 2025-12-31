/*
 * lcl-io test runner
 * A minimal main that registers the io extension and runs lcl scripts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>
#include "lcl-io.h"

#ifdef LCL_HAVE_TEST
#include "test-framework-data.h"

static int lcl_register_test_framework(lcl_interp *interp) {
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
  free(src);

  if (result) {
    lcl_ref_dec(result);
  }

  return rc == LCL_RC_OK ? 0 : -1;
}
#endif

int main(int argc, char **argv) {
  lcl_interp *interp;
  lcl_value *result = NULL;
  int rc;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <script.lcl>\n", argv[0]);
    return 1;
  }

  interp = lcl_interp_new();
  if (!interp) {
    fprintf(stderr, "Failed to create interpreter\n");
    return 1;
  }

  lcl_register_core(interp);
  lcl_register_io(interp);

#ifdef LCL_HAVE_TEST
  lcl_register_test_framework(interp);
#endif

  rc = lcl_eval_file(interp, argv[1], &result);

  if (rc != LCL_RC_OK) {
    const char *err_file = lcl_interp_error_file(interp);
    const char *err_msg = lcl_interp_error_msg(interp);
    fprintf(stderr, "Error at %s:%d",
            err_file ? err_file : "<unknown>",
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
