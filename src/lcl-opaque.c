/*
 * lcl-opaque.c - Opaque values for C extension data
 *
 * Opaque values allow C extensions to wrap arbitrary C pointers
 * in LCL values with type safety and automatic cleanup.
 */

#include <stdlib.h>
#include <string.h>
#include "lcl-values.h"

/*
 * Create a new opaque value wrapping a C pointer.
 *
 * Parameters:
 *   ptr       - the C pointer to wrap (may be NULL)
 *   type_tag  - type identifier string for safety checks (copied)
 *   finalizer - cleanup function called when refcount hits 0 (may be NULL)
 *
 * Returns a new opaque value with refcount 1, or NULL on allocation failure.
 */
lcl_value *lcl_opaque_new(void *ptr, const char *type_tag,
                          lcl_finalizer finalizer) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));
  char *tag_copy = NULL;

  if (!v) return NULL;

  if (type_tag) {
    size_t len = strlen(type_tag);
    tag_copy = (char *)malloc(len + 1);
    if (!tag_copy) {
      free(v);
      return NULL;
    }
    memcpy(tag_copy, type_tag, len + 1);
  }

  v->type = LCL_OPAQUE;
  v->refc = 1;
  v->str_repr = NULL;
  v->as.opaque.ptr = ptr;
  v->as.opaque.type_tag = tag_copy;
  v->as.opaque.finalizer = finalizer;

  return v;
}

/*
 * Extract a C pointer from an opaque value with type checking.
 *
 * Parameters:
 *   v             - the value to extract from
 *   expected_type - expected type tag (NULL to skip type check)
 *   out           - receives the C pointer
 *
 * Returns LCL_OK on success, LCL_ERROR if:
 *   - v is NULL
 *   - v is not an opaque value
 *   - expected_type is non-NULL and doesn't match the value's type_tag
 */
lcl_result lcl_opaque_get(lcl_value *v, const char *expected_type, void **out) {
  if (!v || v->type != LCL_OPAQUE) {
    return LCL_ERROR;
  }

  if (expected_type) {
    const char *actual = v->as.opaque.type_tag;
    if (!actual || strcmp(actual, expected_type) != 0) {
      return LCL_ERROR;
    }
  }

  if (out) {
    *out = v->as.opaque.ptr;
  }

  return LCL_OK;
}

/*
 * Get the type tag of an opaque value.
 *
 * Returns the type tag string, or NULL if v is not an opaque value.
 */
const char *lcl_opaque_type(lcl_value *v) {
  if (!v || v->type != LCL_OPAQUE) {
    return NULL;
  }
  return v->as.opaque.type_tag;
}
