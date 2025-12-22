#ifndef LCL_COMPILE_H
#define LCL_COMPILE_H

#include "hash-table.h"
#include "lcl-lex.h"

/* Forward declarations */
typedef struct lcl_interp lcl_interp;
typedef struct lcl_frame lcl_frame;

typedef enum { LCL_OK, LCL_ERROR } lcl_result;

typedef enum {
  LCL_RC_OK = 0,
  LCL_RC_ERR,
  LCL_RC_RETURN,
  LCL_RC_BREAK,
  LCL_RC_CONTINUE
} lcl_return_code;

typedef struct {
  lcl_return_code code;
  lcl_value *value;
} lcl_result_value;

struct lcl_frame {
  struct lcl_frame *parent;
  hash_table *locals;
  int refc;
  int owns_locals;  /* 0 if locals is borrowed (e.g., from a namespace) */
};

lcl_frame *lcl_frame_new(lcl_frame *parent);
lcl_frame *lcl_frame_new_ns(lcl_frame *parent, hash_table *ns_locals);
void lcl_frame_free(lcl_frame *f);
lcl_frame *lcl_frame_ref_inc(lcl_frame *f);
void lcl_frame_ref_dec(lcl_frame *f);
void lcl_frame_clear(lcl_frame *f);
int lcl_frame_get_binding(lcl_frame *f, const char *name, lcl_value **out);

typedef struct lcl_env {
  lcl_frame *frame;
  lcl_value *current_ns;
  lcl_value *global_ns;
} lcl_env;

lcl_env *lcl_env_new(void);
void lcl_env_free(lcl_env *env);

lcl_result lcl_env_let_take(lcl_env *env, const char *name, lcl_value *value);
lcl_result lcl_env_let(lcl_env *env, const char *name, lcl_value *value);
lcl_result lcl_env_get_value(lcl_env *env, const char *key, lcl_value **out);
lcl_result lcl_env_get_command(lcl_env *env, const char *key, lcl_value **out);
lcl_result lcl_env_var(lcl_env *env, const char *name, lcl_value *value);
lcl_result lcl_env_set_bang(lcl_env *eng, const char *name, lcl_value *value);

struct lcl_interp {
  lcl_env env;
  lcl_value  *last;
  lcl_value  *err_msg;
  const char *err_file;
  int err_line;
  int depth;
  int max_depth;
};

lcl_interp *lcl_interp_new(void);
void lcl_interp_free(lcl_interp *interp);

typedef int (*lcl_c_proc_fn)(lcl_interp *,
                             int argc,
                             lcl_value **argv,
                             lcl_value **out);

typedef int (*lcl_c_spec_fn)(lcl_interp *,
                             int argc,
                             const lcl_word **args,
                             lcl_value **out);

typedef enum {
  LCL_CK_PROC,
  LCL_CK_SPECIAL
} lcl_c_kind;

typedef struct {
  lcl_c_kind kind;
  const char *name;  
  union {
    lcl_c_proc_fn proc;
    lcl_c_spec_fn spec;
  } fn;
} lcl_c_func;

typedef struct {
  lcl_frame *closure;
  lcl_value *params;
  lcl_program *body;
  int capture_ns;
  lcl_value *captured_ns;
} lcl_proc;

#endif
