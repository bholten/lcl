#ifndef LCL_COMPILE_H
#define LCL_COMPILE_H

#include "hash-table.h"
#include "lcl-lex.h"
#include "str-compat.h"
#include <stdlib.h>

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
  int owns_locals;
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
lcl_result lcl_env_get_value(lcl_interp *interp, const char *key,
                             lcl_value **out);
lcl_result lcl_env_get_command(lcl_interp *interp, const char *key,
                               lcl_value **out);
lcl_result lcl_env_var(lcl_env *env, const char *name, lcl_value *value);
lcl_result lcl_env_set_bang(lcl_env *eng, const char *name, lcl_value *value);

lcl_result lcl_def_target_push(lcl_interp *interp, lcl_frame *parent,
                               const char *name);
lcl_value *lcl_def_target_pop(lcl_interp *interp);
lcl_result lcl_def_target_bind(lcl_interp *interp, const char *name,
                               lcl_value *value);
lcl_result lcl_def_target_var(lcl_interp *interp, const char *name,
                              lcl_value *value);

typedef struct {
  lcl_value **argv;
  int argc;
  int valid;
} lcl_tailcall;

typedef struct {
  lcl_value *exports;
  lcl_frame *overlay;
  /* When the def_target was pushed by `namespace foo { ... }`, this
   * holds a strdup'd copy of "foo" (or the full qualified path for
   * `namespace a::b::c { ... }`). Qualified-name lookup uses this to
   * resolve self-references like `$foo::X` while the body is still
   * executing — see lcl_env_get_value's def_stack fallback. NULL for
   * anonymous targets (e.g. `namespace { ... }` with no name) so the
   * lookup never matches them. */
  char *name;
} lcl_def_target;

#define LCL_DEF_STACK_MAX 16

struct lcl_interp {
  lcl_env env;
  lcl_value *last;
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
  lcl_value *current_proc;
  lcl_def_target def_stack[LCL_DEF_STACK_MAX];
  int def_depth;
  /* `isolate` raises def_floor to the current def_depth so that
   * `let`/`var`/`proc`/etc. inside the body do not write through to
   * any enclosing namespace builder. Pushes from a nested `namespace`
   * inside the body still allocate above the floor, so the call sees
   * `def_depth > def_floor` and writes to its own target. Saved and
   * restored across the isolate body. */
  int def_floor;
  int in_subcmd;
  unsigned long gensym_counter;
  lcl_value *require_cache;
};

#define LCL_ERR_CLEAR(interp)                                                  \
  do {                                                                         \
    if ((interp)->err_msg_owned && (interp)->err_msg) {                        \
      free((void *)(interp)->err_msg);                                         \
    }                                                                          \
    if ((interp)->err_file_owned && (interp)->err_file) {                      \
      free((void *)(interp)->err_file);                                        \
    }                                                                          \
    (interp)->err_msg = NULL;                                                  \
    (interp)->err_msg_owned = 0;                                               \
    (interp)->err_file = NULL;                                                 \
    (interp)->err_file_owned = 0;                                              \
  } while (0)

#define LCL_ERR(interp)                                                        \
  do {                                                                         \
    LCL_ERR_CLEAR(interp);                                                     \
    (interp)->err_file =                                                       \
        (interp)->cur_file ? strdup((interp)->cur_file) : NULL;                \
    (interp)->err_file_owned = (interp)->cur_file ? 1 : 0;                     \
    (interp)->err_line = (interp)->cur_line;                                   \
  } while (0)

#define LCL_ERR_MSG(interp, msg)                                               \
  do {                                                                         \
    LCL_ERR_CLEAR(interp);                                                     \
    (interp)->err_file =                                                       \
        (interp)->cur_file ? strdup((interp)->cur_file) : NULL;                \
    (interp)->err_file_owned = (interp)->cur_file ? 1 : 0;                     \
    (interp)->err_line = (interp)->cur_line;                                   \
    (interp)->err_msg = (msg);                                                 \
    (interp)->err_msg_owned = 0;                                               \
  } while (0)

#define LCL_ERR_MSG_DUP(interp, msg)                                           \
  do {                                                                         \
    LCL_ERR_CLEAR(interp);                                                     \
    (interp)->err_file =                                                       \
        (interp)->cur_file ? strdup((interp)->cur_file) : NULL;                \
    (interp)->err_file_owned = (interp)->cur_file ? 1 : 0;                     \
    (interp)->err_line = (interp)->cur_line;                                   \
    (interp)->err_msg = strdup(msg);                                           \
    (interp)->err_msg_owned = 1;                                               \
  } while (0)

lcl_interp *lcl_interp_new(void);
void lcl_interp_free(lcl_interp *interp);

typedef int (*lcl_c_proc_fn)(lcl_interp *, int argc, lcl_value **argv,
                             lcl_value **out);

typedef int (*lcl_c_spec_fn)(lcl_interp *, int argc, const lcl_word **args,
                             lcl_value **out);

typedef enum { LCL_CK_PROC, LCL_CK_SPECIAL } lcl_c_kind;

typedef struct {
  lcl_c_kind kind;
  const char *name;
  union {
    lcl_c_proc_fn proc;
    lcl_c_spec_fn spec;
  } fn;
} lcl_c_func;

typedef struct {
  char *name;
  int is_cell;
  lcl_value *value;
} lcl_upvalue;

typedef struct {
  char *name;
  lcl_program *def_prog;
} lcl_param;

typedef struct {
  lcl_param *params;
  int n_required;
  int n_optional;
  char *rest_name;
} lcl_param_spec;

void lcl_param_spec_free(lcl_param_spec *pspec);

typedef struct {
  char *self_name;
  lcl_upvalue *upvals;
  int nupvals;
  lcl_param_spec pspec;
  lcl_program *body;
  int is_macro;
} lcl_proc;

int lcl_build_upvalues(lcl_interp *interp, const lcl_program *body,
                       const lcl_param_spec *pspec, const char *self_name,
                       lcl_upvalue **upvals_out, int *nout);

#endif
