#ifndef LCL_RANDOM_H
#define LCL_RANDOM_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the `xoshiro::` namespace (xoshiro128** streams). */
void lcl_register_random(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif
