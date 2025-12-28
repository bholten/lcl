#include <stdio.h>
#include <lcl.h>

#include "lcl-curl.h"

int main(int argc, const char **argv) {
  lcl_interp *interp;
  int rc;
  lcl_value *result = NULL;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <script.lcl>\n", argv[0]);
    return 1;
  }

  {
    int i;
    for (i = 0; i < argc; i++) {
      printf("argv[%d] = %s\n", i, argv[i]);
    }
  }


  interp = lcl_interp_new();

  if (!interp) {
    return 1;
  }

  lcl_register_core(interp);
  lcl_register_curl(interp);

  rc = lcl_eval_file(interp, argv[1], &result);

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "Error at %s:%d\n",
            lcl_interp_error_file(interp) ? lcl_interp_error_file(interp)
                                          : "<null>",
            lcl_interp_error_line(interp));
  }

  lcl_interp_free(interp);

  return rc == LCL_RC_OK ? 0 : 1;
}
