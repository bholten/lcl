#ifndef LCL_JSON_H
#define LCL_JSON_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register JSON commands with the interpreter.
 * Creates a "json" namespace with:
 *   json::encode value   - Convert LCL value to JSON string
 *   json::decode string  - Parse JSON string to LCL value
 */
void lcl_register_json(lcl_interp *interp);

#ifdef __cplusplus
}
#endif
  
#endif
