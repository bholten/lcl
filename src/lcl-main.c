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

  free(src);

  if (result) {
    lcl_ref_dec(result);
  }

  return rc == LCL_RC_OK ? 0 : -1;
}
#endif

void lcl_register_core(lcl_interp *interp);
int lcl_eval_file(lcl_interp *interp, const char *filepath, lcl_value **out);

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

#ifdef LCL_HAVE_CURL
  lcl_register_curl(interp);
#endif

#ifdef LCL_HAVE_IO
  lcl_register_io(interp);
#endif

#ifdef LCL_HAVE_TEST
  lcl_register_test_framework(interp);
#endif

  rc = lcl_eval_file(interp, argv[1], &result);

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
