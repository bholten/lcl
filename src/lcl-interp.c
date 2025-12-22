#include <memory.h>

#include "lcl-compile.h"
#include "lcl-values.h"

#define MAX_DEPTH 1024

lcl_interp *lcl_interp_new(void) {
  lcl_interp *interp = (lcl_interp *)calloc(1, sizeof(*interp));
  lcl_env *env = NULL;
  
  if (!interp) return NULL;

  env = lcl_env_new();

  if (!env) {
    free(interp);
    return NULL;
  }

  interp->env = *env;
  free(env);  /* free the struct, contents now owned by interp->env */
  interp->last = NULL;
  interp->err_msg = NULL;
  interp->err_file = NULL;
  interp->err_line = 0;
  interp->depth = 0;
  interp->max_depth = MAX_DEPTH;

  return interp;
}

void lcl_interp_free(lcl_interp *interp) {
  if (!interp) return;

  lcl_ref_dec(interp->last);
  lcl_ref_dec(interp->err_msg);

  /* Clear frame contents first to break circular references
   * (procs in frame have closures that reference the frame) */
  lcl_frame_clear(interp->env.frame);
  lcl_frame_ref_dec(interp->env.frame);

  lcl_ref_dec(interp->env.current_ns);
  lcl_ref_dec(interp->env.global_ns);

  free(interp);
}
