#ifndef LCL_JS_H
#define LCL_JS_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Registers the `Js::` namespace: the JavaScript host engine, for
 * Emscripten builds only (browser or node). The other half of the
 * binding is src/lcl-js-library.js, linked with --js-library; the
 * lcl_js CMake target carries that link option for its consumers.
 */
void lcl_register_js(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif
