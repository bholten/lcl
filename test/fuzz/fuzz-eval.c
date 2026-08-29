/* libFuzzer target: full pipeline — compile + evaluate.
 *
 * Core stdlib only (no Io::/Posix:: packages), with three
 * overrides so the fuzzer stays hermetic:
 *   - puts    -> muted (no stdout spam)
 *   - load    -> error (no filesystem reads)
 *   - require -> error (no filesystem reads)
 *
 * Infinite loops (`while {1} {}`) are reachable by construction; a
 * step-hook command budget turns them into a deterministic
 * clean error so the fuzzer can mutate loop bodies freely. Keep a
 * small -timeout as a backstop for non-eval hangs.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* Hard per-input command budget: the hook aborts unconditionally the
 * first time it fires, so the interval IS the budget. Large enough
 * that legitimate corpus inputs never come near it. */
#define FUZZ_STEP_BUDGET 100000

static int budget_abort(lcl_interp *interp, void *userdata) {
  (void)interp;
  (void)userdata;
  return 1;
}

static lcl_return_code muted_puts(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

static lcl_return_code disabled_spec(lcl_interp *interp, int argc,
                                     const lcl_word **args, lcl_value **out) {
  (void)argc;
  (void)args;
  (void)out;
  lcl_set_error(interp, "disabled under fuzzing");
  return LCL_RC_ERR;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  lcl_interp *interp;
  lcl_value *result = NULL;
  char *src;

  /* lcl_eval_string takes NUL-terminated source; embedded NUL
   * bytes truncate here (the compile target covers them). */
  src = malloc(size + 1);

  if (src == NULL) {
    return 0;
  }

  memcpy(src, data, size);
  src[size] = '\0';

  interp = lcl_interp_new();

  if (interp != NULL) {
    lcl_register_core(interp);
    lcl_register_proc(interp, "puts", muted_puts);
    lcl_register_spec(interp, "load", disabled_spec);
    lcl_register_spec(interp, "require", disabled_spec);
    lcl_set_step_hook(interp, budget_abort, NULL, FUZZ_STEP_BUDGET);

    lcl_eval_string(interp, src, &result);
    lcl_ref_dec(result);
    lcl_interp_free(interp);
  }

  free(src);
  return 0;
}
