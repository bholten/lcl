#ifndef LCL_COMPILE_H
#define LCL_COMPILE_H

#include <stdlib.h>
#include "hash-table.h"
#include "lcl-lex.h"
#include "str-compat.h"

/* Forward declarations */
typedef struct lcl_interp lcl_interp;
typedef struct lcl_frame lcl_frame;

typedef enum { LCL_OK, LCL_ERROR } lcl_result;

typedef enum {
  LCL_RC_OK = 0,
  LCL_RC_ERR,
  LCL_RC_RETURN,
  LCL_RC_BREAK,
  LCL_RC_CONTINUE,
  LCL_RC_TAILCALL
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

typedef struct {
  lcl_value **argv;     /* Arguments for pending tail call (each refcounted) */
  int argc;             /* Number of arguments */
  int valid;            /* 1 if a tail call is pending */
} lcl_tailcall;

struct lcl_interp {
  lcl_env env;
  lcl_value  *last;
  const char *cur_file;
  int cur_line;
  const char *err_msg;
  int err_msg_owned;
  const char *err_file;
  int err_file_owned;
  int err_line;
  int depth;
  int max_depth;
  void *user_data;
  lcl_tailcall pending_tail;
  int in_tail_position;
};

#define LCL_ERR_CLEAR(interp) do { \
  if ((interp)->err_msg_owned && (interp)->err_msg) { \
    free((void *)(interp)->err_msg); \
  } \
  if ((interp)->err_file_owned && (interp)->err_file) { \
    free((void *)(interp)->err_file); \
  } \
  (interp)->err_msg = NULL; \
  (interp)->err_msg_owned = 0; \
  (interp)->err_file = NULL; \
  (interp)->err_file_owned = 0; \
} while(0)

#define LCL_ERR(interp) do { \
  LCL_ERR_CLEAR(interp); \
  (interp)->err_file = (interp)->cur_file ? strdup((interp)->cur_file) : NULL; \
  (interp)->err_file_owned = (interp)->cur_file ? 1 : 0; \
  (interp)->err_line = (interp)->cur_line; \
} while(0)

#define LCL_ERR_MSG(interp, msg) do { \
  LCL_ERR_CLEAR(interp); \
  (interp)->err_file = (interp)->cur_file ? strdup((interp)->cur_file) : NULL; \
  (interp)->err_file_owned = (interp)->cur_file ? 1 : 0; \
  (interp)->err_line = (interp)->cur_line; \
  (interp)->err_msg = (msg); \
} while(0)

#define LCL_ERR_MSG_DUP(interp, msg) do { \
  LCL_ERR_CLEAR(interp); \
  (interp)->err_file = (interp)->cur_file ? strdup((interp)->cur_file) : NULL; \
  (interp)->err_file_owned = (interp)->cur_file ? 1 : 0; \
  (interp)->err_line = (interp)->cur_line; \
  (interp)->err_msg = strdup(msg); \
  (interp)->err_msg_owned = 1; \
} while(0)

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
  char *name;           /* Variable name (owned, must be freed) */
  int is_cell;          /* 1 if cell (mutable), 0 if immutable value */
  lcl_value *value;     /* The captured cell or value (refcounted) */
} lcl_upvalue;

typedef struct {
  char *self_name;      /* Name for self-reference (NULL if anonymous) */
  lcl_upvalue *upvals;  /* Array of captured upvalues */
  int nupvals;          /* Number of upvalues */
  lcl_value *params;    /* Parameter names (list) */
  lcl_program *body;    /* Compiled body */
  int capture_ns;       /* Whether to capture current namespace */
  lcl_value *captured_ns; /* Captured namespace (if capture_ns) */
} lcl_proc;

/* Build upvalues by capturing referenced variables from current environment.
 * params_list: list of parameter names (to exclude from capture)
 * self_name: name for self-reference (to exclude from capture), or NULL
 * upvals_out: receives the upvalues array (may be NULL if no captures needed)
 * nout: receives the count
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error (undefined variable). */
int lcl_build_upvalues(lcl_interp *interp, const lcl_program *body,
                       lcl_value *params_list, const char *self_name,
                       lcl_upvalue **upvals_out, int *nout);

#endif
