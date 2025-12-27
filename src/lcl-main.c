#include <stdio.h>
#include <stdlib.h>

#include "lcl-compile.h"
#include "lcl-values.h"
#include "lcl-eval.h"

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

  rc = lcl_eval_file(interp, argv[1], &result);

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "Error at %s:%d\n",
            interp->err_file ? interp->err_file : "<unknown>",
            interp->err_line);
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_interp_free(interp);

  return rc == LCL_RC_OK ? 0 : 1;
}
