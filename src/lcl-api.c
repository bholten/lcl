/*
 * LCL Public API Implementation
 *
 * This file implements the public API functions declared in include/lcl.h
 */

#include <stdio.h>
#include <stdlib.h>

#include "lcl-compile.h"
#include "lcl-values.h"
#include "lcl-eval.h"

/* Helper: read entire file into a malloc'd string */
static char *api_read_file(const char *path) {
  FILE *f;
  long len;
  char *buf;
  size_t nread;

  f = fopen(path, "rb");
  if (!f) return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  len = ftell(f);
  if (len < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  nread = fread(buf, 1, (size_t)len, f);
  fclose(f);

  if ((long)nread != len) {
    free(buf);
    return NULL;
  }

  buf[len] = '\0';
  return buf;
}

/* ============================================================================
 * Evaluation
 * ============================================================================ */

int lcl_eval_file(lcl_interp *interp, const char *path, lcl_value **out) {
  char *src;
  int rc;

  if (!interp || !path) {
    return LCL_RC_ERR;
  }

  src = api_read_file(path);
  if (!src) {
    return LCL_RC_ERR;
  }

  rc = lcl_eval_string(interp, src, out);
  free(src);

  return rc;
}

/* ============================================================================
 * Error Information
 * ============================================================================ */

const char *lcl_interp_error_file(lcl_interp *interp) {
  if (!interp) return NULL;
  return interp->err_file;
}

int lcl_interp_error_line(lcl_interp *interp) {
  if (!interp) return 0;
  return interp->err_line;
}

/* ============================================================================
 * Variable/Definition Access
 * ============================================================================ */

lcl_result lcl_define(lcl_interp *interp, const char *name, lcl_value *value) {
  if (!interp || !name || !value) return LCL_ERROR;
  return lcl_env_let(&interp->env, name, value);
}

lcl_result lcl_define_take(lcl_interp *interp, const char *name, lcl_value *value) {
  if (!interp || !name || !value) return LCL_ERROR;
  return lcl_env_let_take(&interp->env, name, value);
}

lcl_result lcl_get(lcl_interp *interp, const char *name, lcl_value **out) {
  if (!interp || !name || !out) return LCL_ERROR;
  return lcl_env_get_value(&interp->env, name, out);
}

/* ============================================================================
 * Extending LCL with C Functions
 * ============================================================================ */

lcl_result lcl_register_proc(lcl_interp *interp, const char *name, lcl_c_proc_fn fn) {
  lcl_value *proc;

  if (!interp || !name || !fn) return LCL_ERROR;

  proc = lcl_c_proc_new(name, fn);
  if (!proc) return LCL_ERROR;

  return lcl_env_let_take(&interp->env, name, proc);
}

lcl_result lcl_register_spec(lcl_interp *interp, const char *name, lcl_c_spec_fn fn) {
  lcl_value *spec;

  if (!interp || !name || !fn) return LCL_ERROR;

  spec = lcl_c_spec_new(name, fn);
  if (!spec) return LCL_ERROR;

  return lcl_env_let_take(&interp->env, name, spec);
}
