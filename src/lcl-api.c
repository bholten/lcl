#include <stdio.h>
#include <stdlib.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"
#include "lcl-version.h"
#include "str-compat.h"

const char *lcl_version(void) {
  return LCL_VERSION_STRING;
}

typedef struct {
  const char *name;
  const unsigned char *data;
  size_t len;
} lcl_embedded_lib;

static char *api_read_file(const char *path) {
  FILE *f;
  long len;
  char *buf;
  size_t nread;

  f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }

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

lcl_type lcl_value_type_of(const lcl_value *value) {
  /* Bugfix: NULL-safe — embedders may pass an `out` from a failed
     call. */
  return value ? value->type : LCL_STRING;
}

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

  rc = lcl_eval_string_file(interp, src, path, out);
  free(src);

  return rc;
}

const char *lcl_interp_error_file(lcl_interp *interp) {
  if (!interp) {
    return NULL;
  }
  return interp->err_file;
}

int lcl_interp_error_line(lcl_interp *interp) {
  if (!interp) {
    return 0;
  }
  return interp->err_line;
}

const char *lcl_interp_error_msg(lcl_interp *interp) {
  if (!interp) {
    return NULL;
  }
  return interp->err_msg;
}

void lcl_set_error(lcl_interp *interp, const char *msg) {
  if (!interp) {
    return;
  }

  LCL_ERR_CLEAR(interp);
  interp->err_file = interp->cur_file ? strdup(interp->cur_file) : NULL;
  interp->err_file_owned = interp->cur_file ? 1 : 0;
  interp->err_line = interp->cur_line;
  interp->err_msg = msg;
  /* Bugfix #52: `msg` is borrowed (caller-owned static or literal),
   * so make ownership explicit. LCL_ERR_CLEAR above already zeroes
   * the flag, but re-asserting here keeps the invariant local to
   * this function — robust against future reorderings of CLEAR or
   * additions of an early-return path. */
  interp->err_msg_owned = 0;
}

void lcl_clear_error(lcl_interp *interp) {
  if (!interp) {
    return;
  }

  LCL_ERR_CLEAR(interp);
  interp->err_file = NULL;
  interp->err_line = 0;
}

lcl_result lcl_define(lcl_interp *interp, const char *name, lcl_value *value) {
  if (!interp || !name || !value) {
    return LCL_ERROR;
  }

  return lcl_env_let(&interp->env, name, value);
}

lcl_result lcl_define_take(lcl_interp *interp, const char *name,
                           lcl_value *value) {
  if (!interp || !name || !value) {
    return LCL_ERROR;
  }

  return lcl_env_let_take(&interp->env, name, value);
}

lcl_result lcl_get(lcl_interp *interp, const char *name, lcl_value **out) {
  if (!interp || !name || !out) {
    return LCL_ERROR;
  }

  return lcl_env_get_value(interp, name, out);
}

lcl_result lcl_register_proc(lcl_interp *interp, const char *name,
                             lcl_c_proc_fn fn) {
  lcl_value *proc;

  if (!interp || !name || !fn) {
    return LCL_ERROR;
  }

  proc = lcl_c_proc_new(name, fn);

  if (!proc) {
    return LCL_ERROR;
  }

  return lcl_env_let_take(&interp->env, name, proc);
}

lcl_result lcl_register_spec(lcl_interp *interp, const char *name,
                             lcl_c_spec_fn fn) {
  lcl_value *spec;

  if (!interp || !name || !fn) {
    return LCL_ERROR;
  }

  spec = lcl_c_spec_new(name, fn);

  if (!spec) {
    return LCL_ERROR;
  }

  return lcl_env_let_take(&interp->env, name, spec);
}

int lcl_is_callable(lcl_value *value) {
  if (!value) {
    return 0;
  }

  return value->type == LCL_PROC || value->type == LCL_CPROC;
}

lcl_return_code lcl_call_proc(lcl_interp *interp, lcl_value *proc, int argc,
                              lcl_value **argv, lcl_value **out) {
  lcl_return_code rc;
  lcl_value *dummy = NULL;

  if (!interp || !proc) {
    return LCL_RC_ERR;
  }

  if (!out) {
    out = &dummy;
  }

  if (proc->type == LCL_CPROC) {
    if (proc->as.c_proc.fn->kind == LCL_CK_SPECIAL) {
      return LCL_RC_ERR;
    }
    rc = proc->as.c_proc.fn->fn.proc(interp, argc, argv, out);
  } else if (proc->type == LCL_PROC) {
    rc = lcl_call_user_proc(interp, proc, proc->as.procedure.proc, argc, argv,
                            out);
    if (rc == LCL_RC_RETURN) {
      rc = LCL_RC_OK;
    }
  } else {
    return LCL_RC_ERR;
  }

  if (out == &dummy && dummy) {
    lcl_ref_dec(dummy);
  }

  return rc;
}

int lcl_register_embedded_lib(lcl_interp *interp, const lcl_embedded_lib *lib) {
  lcl_value *result = NULL;
  int rc;

  if (!interp || !lib || !lib->data) {
    return -1;
  }

  rc = lcl_eval_bytes_file(interp, (const char *)lib->data, lib->len,
                           lib->name ? lib->name : "<embedded>", &result);

  if (rc != LCL_RC_OK) {
    const char *err_file = lcl_interp_error_file(interp);
    const char *err_msg = lcl_interp_error_msg(interp);
    fprintf(stderr, "Lcl embedded lib '%s' error at %s:%d",
            lib->name ? lib->name : "<unknown>",
            err_file ? err_file : "<unknown>", lcl_interp_error_line(interp));

    if (err_msg) {
      fprintf(stderr, ": %s", err_msg);
    }

    fprintf(stderr, "\n");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  return rc == LCL_RC_OK ? 0 : -1;
}
