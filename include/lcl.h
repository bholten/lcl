/*
 * LCL - Lexical Command Language
 *
 * A Tcl-like scripting language with lexical scoping.
 *
 * This is the public API for embedding LCL into C/C++ applications.
 *
 * Basic usage:
 *   lcl_interp *interp = lcl_interp_new();
 *   lcl_register_core(interp);
 *
 *   lcl_value *result = NULL;
 *   int rc = lcl_eval_string(interp, "puts {Hello, World!}", &result);
 *
 *   if (result) lcl_ref_dec(result);
 *   lcl_interp_free(interp);
 */

#ifndef LCL_H
#define LCL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Types
 * ============================================================================ */

typedef struct lcl_interp lcl_interp;
typedef struct lcl_value lcl_value;
typedef struct lcl_frame lcl_frame;

/* ============================================================================
 * Result Types
 * ============================================================================ */

/* Result for simple operations */
typedef enum {
  LCL_OK = 0,
  LCL_ERROR = 1
} lcl_result;

/* Return code for evaluation (includes control flow) */
typedef enum {
  LCL_RC_OK = 0,
  LCL_RC_ERR,
  LCL_RC_RETURN,
  LCL_RC_BREAK,
  LCL_RC_CONTINUE
} lcl_return_code;

/* ============================================================================
 * Interpreter Lifecycle
 * ============================================================================ */

/*
 * Create a new LCL interpreter.
 * Returns NULL on failure.
 * The interpreter must be freed with lcl_interp_free().
 */
lcl_interp *lcl_interp_new(void);

/*
 * Free an interpreter and all associated resources.
 */
void lcl_interp_free(lcl_interp *interp);

/*
 * Register the core standard library commands:
 *   puts, +, let, ref, var, set!, return, lambda, proc,
 *   eval, load, subst, namespace, ns, ns::def, get
 */
void lcl_register_core(lcl_interp *interp);

/* ============================================================================
 * Evaluation
 * ============================================================================ */

/*
 * Evaluate a string of LCL code.
 *
 * Parameters:
 *   interp - the interpreter
 *   src    - the source code string
 *   out    - receives the result value (caller must lcl_ref_dec it)
 *
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error.
 * On error, use lcl_interp_error_file/line for location info.
 */
int lcl_eval_string(lcl_interp *interp, const char *src, lcl_value **out);

/*
 * Evaluate an LCL file.
 *
 * Parameters:
 *   interp - the interpreter
 *   path   - path to the .lcl file
 *   out    - receives the result value (caller must lcl_ref_dec it)
 *
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error.
 */
int lcl_eval_file(lcl_interp *interp, const char *path, lcl_value **out);

/* ============================================================================
 * Error Information
 * ============================================================================ */

/*
 * Get the file where an error occurred.
 * Returns NULL if no error or unknown location.
 */
const char *lcl_interp_error_file(lcl_interp *interp);

/*
 * Get the line number where an error occurred.
 * Returns 0 if no error or unknown location.
 */
int lcl_interp_error_line(lcl_interp *interp);

/* ============================================================================
 * Reference Counting
 *
 * All lcl_value pointers are reference counted. When you receive a value
 * from an lcl_* function (via an out parameter), you own a reference and
 * must call lcl_ref_dec when done. Use lcl_ref_inc to create additional
 * references.
 * ============================================================================ */

/*
 * Increment the reference count of a value.
 * Returns the same pointer for convenience.
 */
lcl_value *lcl_ref_inc(lcl_value *value);

/*
 * Decrement the reference count of a value.
 * Frees the value when the count reaches zero.
 */
void lcl_ref_dec(lcl_value *value);

/* ============================================================================
 * Value Creation
 *
 * All lcl_*_new functions return a value with refcount 1.
 * Returns NULL on allocation failure.
 * ============================================================================ */

/*
 * Create a new string value.
 */
lcl_value *lcl_string_new(const char *str);

/*
 * Create a new integer value.
 */
lcl_value *lcl_int_new(long n);

/*
 * Create a new float value.
 */
lcl_value *lcl_float_new(float f);

/*
 * Create a new empty list.
 */
lcl_value *lcl_list_new(void);

/*
 * Create a new namespace.
 */
lcl_value *lcl_ns_new(const char *name);

/* ============================================================================
 * Value Access
 * ============================================================================ */

/*
 * Get the string representation of any value.
 * The returned string is owned by the value; do not free it.
 */
const char *lcl_value_to_string(lcl_value *value);

/*
 * Convert a value to an integer.
 * Returns LCL_OK on success, LCL_ERROR if the value cannot be converted.
 */
lcl_result lcl_value_to_int(lcl_value *value, long *out);

/*
 * Convert a value to a float.
 * Returns LCL_OK on success, LCL_ERROR if the value cannot be converted.
 */
lcl_result lcl_value_to_float(lcl_value *value, float *out);

/* ============================================================================
 * List Operations
 * ============================================================================ */

/*
 * Get the length of a list.
 */
size_t lcl_list_len(const lcl_value *list);

/*
 * Get an item from a list by index.
 * Returns LCL_OK on success, LCL_ERROR if index out of bounds.
 * The returned value has +1 refcount.
 */
lcl_result lcl_list_get(const lcl_value *list, size_t i, lcl_value **out);

/*
 * Push a value onto the end of a list.
 * Note: list_io is a pointer to a pointer because the list may be reallocated.
 */
lcl_result lcl_list_push(lcl_value **list_io, lcl_value *value);

/* ============================================================================
 * Dictionary Operations
 * ============================================================================ */

/*
 * Get the number of pairs of a dictary.
 */
size_t lcl_dict_len(const lcl_value *dict);

/*
 * Get an item from a dictionary by key.
 */
lcl_result lcl_dict_get(const lcl_value *dict, const char *key, lcl_value **out);;

/*
 * Puts a value into a dictionary with key.
 */
lcl_result lcl_dict_put(lcl_value **dict_io, const char *key, lcl_value *value);

/*
 * Deletes a value into a dictionary with key.
 */
lcl_result lcl_dict_del(lcl_value **dict_io, const char *key);

/* ============================================================================
 * Namespace Operations
 * ============================================================================ */

lcl_value *lcl_ns_new(const char *qname);
lcl_result lcl_ns_def(lcl_value *ns, const char *name, lcl_value *value);
lcl_result lcl_ns_get(lcl_value *ns, const char *name, lcl_value **out);
  
/* ============================================================================
 * Variable/Definition Access
 * ============================================================================ */

/*
 * Define a value in the interpreter's current scope.
 * The value's refcount is incremented.
 */
lcl_result lcl_define(lcl_interp *interp, const char *name, lcl_value *value);

/*
 * Define a value in the interpreter's current scope (takes ownership).
 * The value's refcount is NOT incremented (caller's ref is transferred).
 */
lcl_result lcl_define_take(lcl_interp *interp, const char *name, lcl_value *value);

/*
 * Get a value from the interpreter by name.
 * Supports qualified names like "ns::name".
 * The returned value has +1 refcount.
 */
lcl_result lcl_get(lcl_interp *interp, const char *name, lcl_value **out);

/* ============================================================================
 * Extending LCL with C Functions
 *
 * There are two types of C functions you can register:
 *
 * 1. Normal procedures (lcl_c_proc_fn): Arguments are pre-evaluated to values.
 *    Use this for most commands like "puts", "+", "len", etc.
 *
 * 2. Special forms (lcl_c_spec_fn): Arguments are passed as raw, unevaluated
 *    words. Use this for control structures that need to control evaluation,
 *    like "if", "while", "lambda", etc. Most extensions won't need this.
 * ============================================================================ */

/*
 * Function signature for normal C procedures.
 *
 * Parameters:
 *   interp - the interpreter
 *   argc   - number of arguments
 *   argv   - array of argument values (already evaluated)
 *   out    - set this to the return value (with +1 refcount)
 *
 * Return LCL_RC_OK on success, LCL_RC_ERR on error.
 */
typedef int (*lcl_c_proc_fn)(lcl_interp *interp,
                             int argc,
                             lcl_value **argv,
                             lcl_value **out);

/*
 * Create a C procedure value.
 * The returned value can be registered with lcl_define().
 */
lcl_value *lcl_c_proc_new(const char *name, lcl_c_proc_fn fn);

/*
 * Register a C procedure in the interpreter's global scope.
 * This is the primary way to extend LCL with C functions.
 *
 * Example:
 *   int my_add(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
 *       long a, b;
 *       if (argc != 2) return LCL_RC_ERR;
 *       lcl_value_to_int(argv[0], &a);
 *       lcl_value_to_int(argv[1], &b);
 *       *out = lcl_int_new(a + b);
 *       return LCL_RC_OK;
 *   }
 *
 *   lcl_register_proc(interp, "my-add", my_add);
 */
lcl_result lcl_register_proc(lcl_interp *interp, const char *name, lcl_c_proc_fn fn);

/* ============================================================================
 * Special Forms (Advanced)
 *
 * Special forms receive unevaluated arguments and control their own evaluation.
 * This is an advanced feature for implementing control structures.
 * Most C extensions should use lcl_register_proc() instead.
 * ============================================================================ */

/* Opaque type for unevaluated words (used by special forms) */
typedef struct lcl_word lcl_word;

/*
 * Function signature for special forms.
 *
 * Parameters:
 *   interp - the interpreter
 *   argc   - number of unevaluated arguments
 *   args   - array of unevaluated words
 *   out    - set this to the return value (with +1 refcount)
 *
 * Special forms must evaluate their arguments manually using lcl_eval_word().
 */
typedef int (*lcl_c_spec_fn)(lcl_interp *interp,
                              int argc,
                              const lcl_word **args,
                              lcl_value **out);

/*
 * Create a special form value.
 */
lcl_value *lcl_c_spec_new(const char *name, lcl_c_spec_fn fn);

/*
 * Register a special form in the interpreter's global scope.
 */
lcl_result lcl_register_spec(lcl_interp *interp, const char *name, lcl_c_spec_fn fn);

/*
 * Evaluate an unevaluated word to a value.
 * Used by special forms to selectively evaluate their arguments.
 */
lcl_return_code lcl_eval_word(lcl_interp *interp, const lcl_word *word, lcl_value **out);

/* ============================================================================
 * Calling LCL Procedures from C
 *
 * These functions allow C code to call LCL procedures. This is essential
 * for implementing C callbacks that need to invoke user-provided LCL
 * procedures (e.g., CURL write callbacks, event handlers, iterators).
 *
 * Example (CURL write callback):
 *
 *   size_t curl_write_wrapper(char *ptr, size_t size, size_t n, void *userdata) {
 *       struct curl_ctx *ctx = userdata;
 *       lcl_value *args[1];
 *       lcl_value *result = NULL;
 *       size_t bytes = size * n;
 *       char *data = malloc(bytes + 1);
 *       memcpy(data, ptr, bytes);
 *       data[bytes] = '\0';
 *
 *       args[0] = lcl_string_new(data);
 *       free(data);
 *       lcl_call_proc(ctx->interp, ctx->write_callback, 1, args, &result);
 *       if (result) lcl_ref_dec(result);
 *       lcl_ref_dec(args[0]);
 *       return bytes;
 *   }
 * ============================================================================ */

/*
 * Check if a value is callable (user procedure or C procedure).
 * Returns 1 if callable, 0 otherwise.
 */
int lcl_is_callable(lcl_value *value);

/*
 * Call an LCL procedure from C code.
 *
 * Parameters:
 *   interp - the interpreter
 *   proc   - an LCL procedure value (LCL_PROC or LCL_CPROC)
 *   argc   - number of arguments
 *   argv   - array of argument values (caller retains ownership)
 *   out    - receives the return value (with +1 refcount), may be NULL
 *
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error.
 * Note: LCL_RC_RETURN from the procedure is converted to LCL_RC_OK.
 *
 * The caller is responsible for:
 *   - Creating argument values (lcl_string_new, etc.)
 *   - Decrementing argument refcounts after the call
 *   - Decrementing the result refcount when done
 */
lcl_return_code lcl_call_proc(lcl_interp *interp,
                               lcl_value *proc,
                               int argc,
                               lcl_value **argv,
                               lcl_value **out);

/* ============================================================================
 * Opaque Values (C Extension Data)
 *
 * Opaque values allow C extensions to wrap arbitrary C pointers in LCL values
 * with type safety and automatic cleanup via finalizers.
 *
 * Example usage (e.g., wrapping curl):
 *
 *   void curl_ctx_free(void *ptr) {
 *       struct curl_context *ctx = ptr;
 *       curl_easy_cleanup(ctx->curl);
 *       free(ctx);
 *   }
 *
 *   int c_curl_new(..., lcl_value **out) {
 *       struct curl_context *ctx = calloc(1, sizeof(*ctx));
 *       ctx->curl = curl_easy_init();
 *       *out = lcl_opaque_new(ctx, "curl_context", curl_ctx_free);
 *       return LCL_RC_OK;
 *   }
 *
 *   int c_curl_set_url(...) {
 *       struct curl_context *ctx;
 *       if (lcl_opaque_get(argv[0], "curl_context", (void**)&ctx) != LCL_OK) {
 *           return LCL_RC_ERR;  // type mismatch
 *       }
 *       curl_easy_setopt(ctx->curl, CURLOPT_URL, ...);
 *       ...
 *   }
 * ============================================================================ */

/*
 * Finalizer function type - called when opaque value refcount reaches 0.
 */
typedef void (*lcl_finalizer)(void *ptr);

/*
 * Create a new opaque value wrapping a C pointer.
 *
 * Parameters:
 *   ptr       - the C pointer to wrap (may be NULL)
 *   type_tag  - type identifier for safety checks (e.g., "curl_context")
 *   finalizer - cleanup function called when refcount hits 0 (may be NULL)
 *
 * Returns a new value with refcount 1, or NULL on allocation failure.
 * The type_tag string is copied internally.
 */
lcl_value *lcl_opaque_new(void *ptr, const char *type_tag, lcl_finalizer finalizer);

/*
 * Extract a C pointer from an opaque value with type checking.
 *
 * Parameters:
 *   value         - the value to extract from
 *   expected_type - expected type tag (NULL to skip type check)
 *   out           - receives the C pointer
 *
 * Returns LCL_OK on success, LCL_ERROR if value is not an opaque
 * or if expected_type doesn't match the value's type_tag.
 */
lcl_result lcl_opaque_get(lcl_value *value, const char *expected_type, void **out);

/*
 * Get the type tag of an opaque value.
 * Returns NULL if value is not an opaque.
 */
const char *lcl_opaque_type(lcl_value *value);

/* ============================================================================
 * Frame Reference Counting (Advanced)
 *
 * Frames are used internally for lexical scoping. You typically don't need
 * these unless implementing advanced features like closures from C.
 * ============================================================================ */

lcl_frame *lcl_frame_ref_inc(lcl_frame *f);
void lcl_frame_ref_dec(lcl_frame *f);

#ifdef __cplusplus
}
#endif

#endif /* LCL_H */
