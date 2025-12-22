#include <stdio.h>
#include <stdlib.h>

#include "lcl-compile.h"
#include "lcl-values.h"
#include "lcl-eval.h"

void lcl_register_core(lcl_interp *interp);

static char *read_file(const char *path) {
  FILE *f;
  long len;
  char *buf;

  f = fopen(path, "rb");

  if (!f) {
    fprintf(stderr, "Could not open file: %s\n", path);
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);

  buf = (char *)malloc((size_t)len + 1);

  if (!buf) {
    fclose(f);
    return NULL;
  }

  fread(buf, 1, (size_t)len, f);
  buf[len] = '\0';
  fclose(f);

  return buf;
}

int main(int argc, char **argv) {
  lcl_interp *interp;
  char *src;
  lcl_value *result = NULL;
  int rc;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <script.lcl>\n", argv[0]);
    return 1;
  }

  src = read_file(argv[1]);

  if (!src) {
    return 1;
  }

  interp = lcl_interp_new();

  if (!interp) {
    fprintf(stderr, "Failed to create interpreter\n");
    free(src);
    return 1;
  }

  lcl_register_core(interp);

  rc = lcl_eval_string(interp, src, &result);

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "Error at %s:%d\n",
            interp->err_file ? interp->err_file : "<unknown>",
            interp->err_line);
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_interp_free(interp);
  free(src);

  return rc == LCL_RC_OK ? 0 : 1;
}
