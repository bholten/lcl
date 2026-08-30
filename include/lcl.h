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

#include "lcl-int.h"
#include "lcl-version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Types
 * ============================================================================
 */

typedef struct lcl_interp lcl_interp;
typedef struct lcl_value lcl_value;
typedef struct lcl_frame lcl_frame;

/* ============================================================================
 * Result Types
 * ============================================================================
 */

/*
 * Two result enums, two questions.  Both spell success as 0, but they
 * are distinct types and are never mixed:
 *
 *   lcl_result       "did this operation succeed?"
 *                    Returned by value accessors and mutators
 *                    (lcl_value_to_int, lcl_list_get, lcl_dict_put,
 *                    lcl_ns_def, lcl_define, lcl_opaque_get, ...) and by
 *                    registration calls.  Compare against LCL_OK.
 *
 *   lcl_return_code  "what should the evaluator do next?"
 *                    Returned by everything that runs Lcl code
 *                    (lcl_eval_string, lcl_eval_file, lcl_call_proc,
 *                    lcl_eval_word) and by the C procedures you write
 *                    (lcl_c_proc_fn / lcl_c_spec_fn).  Compare against
 *                    LCL_RC_OK; return LCL_RC_OK / LCL_RC_ERR from procs.
 *                    LCL_RC_RETURN/BREAK/CONTINUE are control flow, and
 *                    only special forms that run bodies ever see them.
 *
 * Rule of thumb: if the function takes an `lcl_interp *` and may run
 * user code, it speaks lcl_return_code; otherwise lcl_result.
 * Predicates (lcl_is_callable, ...) return plain int 0/1.
 */

/* Result for simple operations */
typedef enum { LCL_OK = 0, LCL_ERROR = 1 } lcl_result;

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
 * ============================================================================
 */

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

/*
 * Set application-specific user data on the interpreter.
 * This pointer can be retrieved later via lcl_interp_get_user_data().
 * Useful for passing context to C procedures without global state.
 *
 * The interpreter does NOT take ownership of the data; the caller is
 * responsible for managing its lifetime and freeing it after the
 * interpreter is freed.
 */
void lcl_interp_set_user_data(lcl_interp *interp, void *data);

/*
 * Get the application-specific user data from the interpreter.
 * Returns NULL if no user data was set.
 */
void *lcl_interp_get_user_data(lcl_interp *interp);

/*
 * Register a directory as a module search root for `require`.
 *
 * A bare `require` path argument (one that does not begin with "/",
 * "./", or "../") is looked up under each registered root, in
 * registration order; the first root containing the file wins. If no
 * roots are registered, bare paths resolve relative to the process
 * working directory (legacy behavior).
 *
 * `dir` is copied; the caller keeps ownership of the argument. Paths
 * beginning with "./" or "../" always resolve relative to the file
 * that contains the `require`, and absolute paths are used as-is --
 * neither consults the search roots.
 */
void lcl_add_require_root(lcl_interp *interp, const char *dir);

/*
 * Install a module-identity hook for `require`.
 *
 * Lcl module paths are lexical names: `require` resolves its argument
 * to a cleaned lexical path ('/'-separated; '.', '..', and repeated
 * separators normalized by string rules alone, symlinks not
 * consulted) and by default uses that path as the module's identity --
 * the key for the require cache and for dependency-cycle detection.
 * Two lexical spellings that reach the same file through symlinks are
 * therefore two modules.
 *
 * A host that wants stronger identity (e.g. physical-file
 * deduplication via realpath on POSIX) installs a key function. It
 * receives the resolved lexical path and returns a malloc'd key,
 * which the core takes ownership of and frees; returning NULL falls
 * back to the lexical path. The hook affects identity ONLY: file
 * opening, nested relative-require resolution, and diagnostics always
 * use the lexical path. Note the trade: deduplicating by physical
 * identity makes which lexical spelling evaluates (and thus the base
 * for the module's own relative requires) dependent on load order.
 *
 * Pass fn = NULL to restore the default. Installing a hook does not
 * invalidate cache entries keyed under a previous identity scheme;
 * install before evaluating scripts.
 */
typedef char *(*lcl_module_key_fn)(const char *lexical_path, void *userdata);
void lcl_set_module_key_fn(lcl_interp *interp, lcl_module_key_fn fn,
                           void *userdata);

/*
 * Install a module-source hook: where `require` and `load` get their
 * text from.
 *
 * By default both read files. With a hook installed they read nothing
 * themselves: every candidate path `require` resolves (see
 * lcl_add_require_root) and every `load` path is offered to the hook
 * instead, which returns the module's source as a malloc'd,
 * NUL-terminated buffer the core takes ownership of (optionally
 * writing its length to *len), or NULL for "no such module". The
 * first candidate the hook answers is the module; its lexical path
 * remains the module's name for diagnostics, for nested relative
 * requires, and for the identity hook. A NULL answer normally makes
 * `require` try the next candidate and finally report every path it
 * tried; a hook that wants to explain a failure (a network error, a
 * forbidden path) calls lcl_set_error first, and that message is
 * reported instead.
 *
 * This is how a host without a filesystem -- a browser page, an
 * application with modules in a bundle or a database -- serves Lcl
 * modules. The hook runs with the interpreter mid-evaluation: it must
 * not evaluate code on this interp or free it.
 *
 * Pass fn = NULL to restore file reading. Modules already in the
 * require cache stay cached.
 */
typedef char *(*lcl_module_source_fn)(lcl_interp *interp, const char *path,
                                      size_t *len, void *userdata);
void lcl_set_module_source_fn(lcl_interp *interp, lcl_module_source_fn fn,
                              void *userdata);

/*
 * Install a step hook: a host callback invoked from the evaluator
 * every `interval` commands.
 *
 * The hook returning nonzero aborts evaluation: the current
 * evaluation fails with the error "evaluation aborted by host", and
 * the abort is sticky -- it propagates through `catch` (a script
 * cannot trap it) and keeps failing until control returns to the
 * host. The next top-level evaluation starts fresh, with the abort
 * cleared and the command countdown reset to `interval`.
 *
 * This is the budget/watchdog mechanism for untrusted or
 * possibly-non-terminating scripts (`while {1} {}`): a hook that
 * unconditionally returns 1 turns `interval` into a hard per-eval
 * command budget; a timing hook can instead abort on a wall-clock
 * deadline, yield to a UI, or poll for user interruption.
 *
 * Counting is per command dispatched, uniformly across loop
 * iterations, proc calls, subcommands, and `eval`. The hook runs
 * with the interpreter mid-evaluation: it must not evaluate code on
 * this interp or free it; reading state and returning is safe.
 *
 * Pass fn = NULL to remove the hook. `interval` 0 is treated as 1
 * (every command).
 */
typedef int (*lcl_step_fn)(lcl_interp *interp, void *userdata);

/*
 * Request the same sticky abort from inside a C procedure: the
 * current top-level evaluation fails at the next command with
 * "evaluation aborted by host", `catch` cannot trap it, and the next
 * top-level evaluation starts fresh. The calling procedure should
 * return LCL_RC_ERR itself (with whatever message it likes) so that
 * the abort is immediate rather than one command late. This is how a
 * host implements `exit`: record the status, abort, and read the
 * status back once evaluation has unwound.
 */
void lcl_interp_abort(lcl_interp *interp);

void lcl_set_step_hook(lcl_interp *interp, lcl_step_fn fn, void *userdata,
                       unsigned long interval);

/*
 * Install a call hook: a host callback invoked when a user-defined
 * procedure (proc/lambda, not a C builtin) is entered and again when
 * it exits, whether normally or by error.
 *
 *   proc     - the procedure value being called
 *   name     - the name it was invoked by (or its own name, or
 *              "<lambda>"), valid for the duration of the call
 *   argc/argv- the evaluated arguments, valid on both events
 *   entering - 1 on entry, 0 on exit
 *
 * Tail self-calls (which re-enter the body without a new C frame)
 * report as a single call. The hook runs with the interpreter
 * mid-evaluation: it must not evaluate code on this interp or free
 * it; reading state (lcl_value_to_string on arguments, timers,
 * counters) is safe. Profilers and tracers are the intended users;
 * there is no cost when no hook is installed.
 *
 * Pass fn = NULL to remove the hook. lcl_get_call_hook reads the
 * current hook so a temporary installer can restore it.
 */
typedef void (*lcl_call_fn)(lcl_interp *interp, lcl_value *proc,
                            const char *name, int argc, lcl_value **argv,
                            int entering, void *userdata);

void lcl_set_call_hook(lcl_interp *interp, lcl_call_fn fn, void *userdata);
void lcl_get_call_hook(lcl_interp *interp, lcl_call_fn *fn_out,
                       void **userdata_out);

/*
 * Process-wide allocation counters, always on (an increment each).
 * `values_allocated` / `values_freed` count lcl_value nodes over the
 * life of the process; their difference is the number currently live,
 * which makes leak checks trivial. `list_clones` and `dict_clones`
 * count copy-on-write copies of shared containers -- a number that
 * grows with the size of your data is the signature of an
 * accumulation loop that should use the in-place `!` forms. Exposed
 * to scripts as `Interp::stats`.
 */
typedef struct lcl_stats {
  unsigned long values_allocated;
  unsigned long values_freed;
  unsigned long list_clones;
  unsigned long dict_clones;
} lcl_stats;

void lcl_get_stats(lcl_stats *out);

/* ============================================================================
 * Evaluation
 * ============================================================================
 */

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
 *
 * A `break` or `continue` that escapes the top level of the script
 * comes back as LCL_RC_BREAK / LCL_RC_CONTINUE with no error message
 * set; *out then holds the (empty) control payload and must still be
 * released. Hosts that want an error there map it themselves.
 */
lcl_return_code lcl_eval_string(lcl_interp *interp, const char *src,
                                lcl_value **out);

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
lcl_return_code lcl_eval_file(lcl_interp *interp, const char *path,
                              lcl_value **out);

/*
 * Evaluate LCL code from a byte buffer (not null-terminated).
 *
 * This is the preferred API for evaluating embedded code from xxd -i output
 * as it avoids unnecessary copying and null-termination.
 *
 * Parameters:
 *   interp - the interpreter
 *   src    - pointer to the source code bytes
 *   len    - length of the source code in bytes
 *   out    - receives the result value (caller must lcl_ref_dec it)
 *
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error.
 */
lcl_return_code lcl_eval_bytes(lcl_interp *interp, const char *src, size_t len,
                               lcl_value **out);

/*
 * Evaluate source text under a name of the host's choosing.
 *
 * `file` names the source for diagnostics and provenance: runtime
 * errors report `file:line`, and procs defined by the text answer
 * `Proc::origin` with it. Use it for text that is not a file on disk
 * but still *is* something -- an editor buffer ("*scratch*"), a REPL
 * entry ("<repl:42>"), an embedded script -- so that definitions from
 * different sources stay
 * distinguishable. lcl_eval_string/lcl_eval_bytes are these with a
 * placeholder name ("<string>" / "<bytes>").
 *
 * `file` is copied into the compiled program; the caller keeps
 * ownership of the argument.
 */
lcl_return_code lcl_eval_string_file(lcl_interp *interp, const char *src,
                                     const char *file, lcl_value **out);
lcl_return_code lcl_eval_bytes_file(lcl_interp *interp, const char *src,
                                    size_t len, const char *file,
                                    lcl_value **out);

/* ============================================================================
 * Embedded Libraries
 *
 * For embedding LCL libraries as xxd -i data in C applications.
 * ============================================================================
 */

/*
 * Descriptor for an embedded LCL library (e.g., from xxd -i output).
 */
typedef struct {
  const char *name;          /* Library name for error messages */
  const unsigned char *data; /* xxd -i output (library source) */
  size_t len;                /* Length of data in bytes */
} lcl_embedded_lib;

/*
 * Register an embedded LCL library.
 *
 * This evaluates the library source in the interpreter's global scope,
 * making its definitions available. Zero-copy for xxd -i data.
 *
 * Parameters:
 *   interp - the interpreter
 *   lib    - embedded library descriptor
 *
 * Returns LCL_OK on success, LCL_ERROR on error (prints error to stderr).
 */
lcl_result lcl_register_embedded_lib(lcl_interp *interp,
                                     const lcl_embedded_lib *lib);

/* ============================================================================
 * Error Information
 * ============================================================================
 */

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

/*
 * Get the error message.
 * Returns NULL if no error message was set.
 */
const char *lcl_interp_error_msg(lcl_interp *interp);

/*
 * Set an error message for the current evaluation position.
 * Use this in C extensions to provide meaningful error messages.
 *
 * The message is COPIED: `msg` may live in a stack buffer, be freed,
 * or be overwritten as soon as this call returns. (If the copy fails
 * for lack of memory, the error is recorded as "out of memory".)
 * There is deliberately no printf-style variant in the core API --
 * format into your own buffer, then call this.
 *
 * Example:
 *   char buf[128];
 *   if (argc != 2) {
 *     sprintf(buf, "%s: expected 2 arguments, got %d", "my-add", argc);
 *     lcl_set_error(interp, buf);
 *     return LCL_RC_ERR;
 *   }
 */
void lcl_set_error(lcl_interp *interp, const char *msg);

/*
 * Clear any error state (typically called before evaluation).
 */
void lcl_clear_error(lcl_interp *interp);

/* ============================================================================
 * Reference Counting
 *
 * All lcl_value pointers are reference counted. When you receive a value
 * from an lcl_* function (via an out parameter), you own a reference and
 * must call lcl_ref_dec when done. Use lcl_ref_inc to create additional
 * references.
 *
 * Ownership at a glance
 * ---------------------
 * Three rules cover the whole API; the table lists the instances.
 *
 *   1. `lcl_*_new` and `**out` parameters hand you +1: you must
 *      lcl_ref_dec it.
 *   2. Functions that take an `lcl_value *` argument BORROW it and leave
 *      your reference untouched -- containers take their own +1 -- unless
 *      the name ends in `_take`, which CONSUMES your reference (even on
 *      failure).
 *   3. `const char *` results are borrowed from the value; never free
 *      them, and don't use them after the value's last lcl_ref_dec.
 *
 *   Returns +1 to caller     lcl_string_new, lcl_int_new, lcl_float_new,
 *                            lcl_list_new, lcl_dict_new, lcl_ns_new,
 *                            lcl_c_proc_new, lcl_c_spec_new,
 *                            lcl_opaque_new, lcl_ref_inc (same pointer)
 *                            and every `**out`: lcl_eval_*, lcl_call_proc,
 *                            lcl_eval_word, lcl_list_get, lcl_dict_get,
 *                            lcl_dict_keys, lcl_cell_get, lcl_ns_get,
 *                            lcl_get
 *   Borrowed value           lcl_list_peek, lcl_dict_peek, lcl_cell_peek,
 *                            lcl_ns_peek: the container's own reference,
 *                            refcount untouched. Valid until the container
 *                            is mutated or released; lcl_ref_inc it to keep
 *                            it longer. Never lcl_ref_dec a peeked value.
 *   Borrows the argument     lcl_list_push, lcl_dict_put, lcl_ns_def,
 *                            lcl_define, lcl_call_proc (argv),
 *                            lcl_value_to_*, lcl_opaque_get
 *   Consumes the argument    lcl_define_take, lcl_ns_def_take, lcl_ref_dec
 *   Borrowed string          lcl_value_to_string, lcl_value_get_string,
 *                            lcl_value_to_cstring, lcl_opaque_type,
 *                            lcl_interp_error_msg / _file
 *   Copied on entry          lcl_string_new (str), lcl_set_error (msg),
 *                            lcl_opaque_new (type_tag), dict keys, names
 *
 * The consuming variants exist for the register-and-forget idiom:
 *   lcl_ns_def_take(ns, "push", lcl_c_proc_new("List::push", c_push));
 * ============================================================================
 */

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
 * Value introspection
 *
 * All lcl_value pointers store a value of a specific LCL_TYPE.
 * ============================================================================
 */

typedef enum lcl_type {
  LCL_STRING,
  LCL_INT,
  LCL_FLOAT,
  LCL_LIST,
  LCL_DICT,
  LCL_CELL,
  LCL_PROC,
  LCL_CPROC,
  LCL_NAMESPACE,
  LCL_OPAQUE
} lcl_type;

lcl_type lcl_value_type_of(const lcl_value *value);

/* ============================================================================
 * Value Creation
 *
 * All lcl_*_new functions return a value with refcount 1.
 * Returns NULL on allocation failure.
 * ============================================================================
 */

/*
 * Create a new string value.
 *
 * `str == NULL` is accepted and treated as the empty string; the
 * returned value is a regular STRING that stringifies to "".
 */
lcl_value *lcl_string_new(const char *str);

/*
 * Create a new integer value.
 */
lcl_value *lcl_int_new(lcl_int n);

/*
 * Create a new float value.
 */
lcl_value *lcl_float_new(double f);

/*
 * Create a new empty list.
 */
lcl_value *lcl_list_new(void);

/* ============================================================================
 * Value Access
 * ============================================================================
 */

/*
 * Get the string representation of any value.
 *
 * This is the RENDERER: it is total, and intended for output-shaped
 * contexts (printing, diagnostics, concatenation, dict keys). Host
 * commands whose operand must already BE a string should use
 * lcl_value_get_string() instead, so numbers/lists/procs are not
 * silently rendered where text was required.
 *
 * Returns a borrowed pointer owned by the value; do not free it.
 *
 * Returns NULL if `value` is NULL, or if stringification required an
 * allocation that failed (out of memory). Callers that cannot
 * tolerate NULL should use lcl_value_to_cstring(), which converts
 * NULL into an interpreter error.
 */
const char *lcl_value_to_string(lcl_value *value);

/*
 * Domain-strict string getter: succeeds only when `value` is an
 * actual string (LCL_STRING tag). On success returns LCL_OK and
 * writes the borrowed content pointer to *out. Returns LCL_ERROR for
 * NULL input or any other type -- no rendering is performed. The
 * counterpart of lcl_value_to_int/_to_float for the string domain;
 * scripts render explicitly with String::from.
 */
lcl_result lcl_value_get_string(lcl_value *value, const char **out);

/*
 * Convert a value to a borrowed C string, raising an interpreter
 * error on failure. On success returns LCL_OK and writes the borrowed
 * pointer to *out. On NULL input or stringification OOM, sets the
 * interp error to "out of memory" and returns LCL_ERROR; *out is left
 * untouched.
 */
lcl_result lcl_value_to_cstring(lcl_interp *interp, lcl_value *value,
                                const char **out);

/*
 * Convert a value to an integer.
 * Returns LCL_OK on success, LCL_ERROR if the value cannot be converted.
 */
lcl_result lcl_value_to_int(lcl_value *value, lcl_int *out);

/*
 * Render `n` in decimal into `buf` (at least LCL_INT_STRLEN bytes);
 * returns the length written, NUL excluded. The one integer
 * formatter in the tree: printf has no portable conversion for
 * lcl_int under C89.
 */
size_t lcl_int_format(char *buf, lcl_int n);

/*
 * Convert a value to a float.
 * Returns LCL_OK on success, LCL_ERROR if the value cannot be converted.
 */
lcl_result lcl_value_to_float(lcl_value *value, double *out);

/* ============================================================================
 * List Operations
 * ============================================================================
 */

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
 * Borrow an item from a list by index: the list's own reference,
 * refcount untouched. Returns NULL if `list` is not a list or `i` is
 * out of bounds. Valid until the list is mutated or released; do not
 * lcl_ref_dec it. Prefer this over lcl_list_get when you only read
 * the element during the call.
 */
lcl_value *lcl_list_peek(const lcl_value *list, size_t i);

/*
 * Push a value onto the end of a list. Does not take ownership of
 * `value`; the list takes its own +1 reference, leaving the caller's
 * reference unchanged.
 *
 * Note: list_io is a pointer to a pointer because the list may be
 * reallocated or cloned (copy-on-write) when it has refc > 1.
 */
lcl_result lcl_list_push(lcl_value **list_io, lcl_value *value);

/* ============================================================================
 * Dictionary Operations
 * ============================================================================
 */

/*
 * Create a new empty dictionary.
 */
lcl_value *lcl_dict_new(void);

/*
 * Get the number of pairs of a dictionary.
 */
size_t lcl_dict_len(const lcl_value *dict);

/*
 * Get an item from a dictionary by key.
 * Returns LCL_OK on success, LCL_ERROR if the key is absent.
 * The returned value has +1 refcount.
 */
lcl_result lcl_dict_get(const lcl_value *dict, const char *key,
                        lcl_value **out);

/*
 * Borrow an item from a dictionary by key: the dictionary's own
 * reference, refcount untouched. Returns NULL if `dict` is not a
 * dictionary or the key is absent. Valid until the dictionary is
 * mutated or released; do not lcl_ref_dec it.
 */
lcl_value *lcl_dict_peek(const lcl_value *dict, const char *key);

/*
 * Put a value into a dictionary under `key`. Does not take ownership
 * of `value`; the dictionary takes its own +1 reference, leaving the
 * caller's reference unchanged.
 * Note: dict_io is a pointer to a pointer because the dict may be
 * cloned (copy-on-write) when it has refc > 1.
 */
lcl_result lcl_dict_put(lcl_value **dict_io, const char *key, lcl_value *value);

/*
 * Delete the entry for `key`. Returns LCL_OK on success,
 * LCL_ERROR if the key is absent.
 * Note: dict_io is a pointer to a pointer because the dict may be
 * cloned (copy-on-write) when it has refc > 1.
 */
lcl_result lcl_dict_del(lcl_value **dict_io, const char *key);

/*
 * Get all keys from a dictionary as a freshly-built list.
 * The returned list has +1 refcount.
 */
lcl_result lcl_dict_keys(const lcl_value *dict, lcl_value **out);

/* ============================================================================
 * Cell Operations
 * ============================================================================
 */

/*
 * Get the contents of a cell (mutable reference).
 * Returns the value with +1 refcount.
 */
lcl_result lcl_cell_get(lcl_value *cell, lcl_value **out);

/*
 * Borrow the contents of a cell: the cell's own reference, refcount
 * untouched. Returns NULL if `cell` is not a cell or has been
 * cleared.  Valid until the cell is set or released; do not
 * lcl_ref_dec it.
 */
lcl_value *lcl_cell_peek(const lcl_value *cell);

/* ============================================================================
 * Namespace Operations
 * ============================================================================
 */

/*
 * Create a new empty namespace whose qualified name is `qname`
 * (e.g. "raylib"). Bind it with lcl_define() to make it reachable
 * from scripts as `raylib::member`.
 */
lcl_value *lcl_ns_new(const char *qname);

/*
 * Bind `value` as member `name` of namespace `ns`, replacing any
 * existing member. Does not take ownership of `value`; the namespace
 * takes its own +1 reference. Returns LCL_ERROR if `ns` is not a
 * namespace or on allocation failure.
 */
lcl_result lcl_ns_def(lcl_value *ns, const char *name, lcl_value *value);

/*
 * As lcl_ns_def, but CONSUMES the caller's reference to `value` -- on
 * success and on failure alike. This is the one-liner for registering
 * freshly created procs: lcl_ns_def_take(ns, "open",
 * lcl_c_proc_new("Io::open", c_open));
 */
lcl_result lcl_ns_def_take(lcl_value *ns, const char *name, lcl_value *value);

/*
 * Look up member `name` in namespace `ns`.  Returns LCL_OK and writes
 * the value (+1 refcount) to *out, or LCL_ERROR if `ns` is not a
 * namespace or the member is absent.  Cells are returned as cells
 * (not dereferenced).
 */
lcl_result lcl_ns_get(lcl_value *ns, const char *name, lcl_value **out);

/*
 * Borrow member `name` of namespace `ns`: the namespace's own
 * reference, refcount untouched. Returns NULL if `ns` is not a
 * namespace or the member is absent. Cells are returned as cells.
 * Valid until the namespace is mutated or released; do not
 * lcl_ref_dec it.
 */
lcl_value *lcl_ns_peek(const lcl_value *ns, const char *name);

/* ============================================================================
 * Variable/Definition Access
 * ============================================================================
 */

/*
 * Define a value in the interpreter's current scope.
 * The value's refcount is incremented.
 */
lcl_result lcl_define(lcl_interp *interp, const char *name, lcl_value *value);

/*
 * Define a value in the interpreter's current scope (takes ownership).
 * The value's refcount is NOT incremented (caller's ref is transferred).
 */
lcl_result lcl_define_take(lcl_interp *interp, const char *name,
                           lcl_value *value);

/*
 * Get a value from the interpreter by name.
 * Supports qualified names like "ns::name".
 * The returned value has +1 refcount.
 *
 * Resolution is exactly what script code at the call site would see:
 * lexical from the current scope. A C procedure invoked from inside a
 * proc body therefore does not see interpreter-root bindings by bare
 * name -- proc bodies are closed over what they capture, not over the
 * global frame. To reach a root binding regardless of where you are
 * called from, use the `::`-rooted spelling: "::Process::read".
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
 * ============================================================================
 */

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
typedef lcl_return_code (*lcl_c_proc_fn)(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out);

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
 *   lcl_return_code my_add(lcl_interp *interp, int argc, lcl_value **argv,
 *                          lcl_value **out) {
 *     lcl_int a, b;
 *     if (argc != 2) {
 *       lcl_set_error(interp, "my-add: expected 2 arguments");
 *       return LCL_RC_ERR;
 *     }
 *     if (lcl_value_to_int(argv[0], &a) != LCL_OK ||
 *         lcl_value_to_int(argv[1], &b) != LCL_OK) {
 *       lcl_set_error(interp, "my-add: expected integers");
 *       return LCL_RC_ERR;
 *     }
 *     *out = lcl_int_new(a + b);
 *     return LCL_RC_OK;
 *   }
 *
 *   lcl_register_proc(interp, "my-add", my_add);
 */
lcl_result lcl_register_proc(lcl_interp *interp, const char *name,
                             lcl_c_proc_fn fn);

/* ============================================================================
 * Special Forms (Advanced)
 *
 * Special forms receive unevaluated arguments and control their own evaluation.
 * This is an advanced feature for implementing control structures.
 * Most C extensions should use lcl_register_proc() instead.
 * ============================================================================
 */

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
typedef lcl_return_code (*lcl_c_spec_fn)(lcl_interp *interp, int argc,
                                         const lcl_word **args,
                                         lcl_value **out);

/*
 * Create a special form value.
 */
lcl_value *lcl_c_spec_new(const char *name, lcl_c_spec_fn fn);

/*
 * Register a special form in the interpreter's global scope.
 */
lcl_result lcl_register_spec(lcl_interp *interp, const char *name,
                             lcl_c_spec_fn fn);

/*
 * Evaluate an unevaluated word to a value.
 * Used by special forms to selectively evaluate their arguments.
 */
lcl_return_code lcl_eval_word(lcl_interp *interp, const lcl_word *word,
                              lcl_value **out);

/* ============================================================================
 * Calling LCL Procedures from C
 *
 * These functions allow C code to call LCL procedures. This is essential
 * for implementing C callbacks that need to invoke user-provided LCL
 * procedures (e.g., CURL write callbacks, event handlers, iterators).
 *
 * Example (CURL write callback):
 *
 *   size_t curl_write_wrapper(char *ptr, size_t size, size_t n, void *userdata)
 * { struct curl_ctx *ctx = userdata; lcl_value *args[1]; lcl_value *result =
 * NULL; size_t bytes = size * n; char *data = malloc(bytes + 1); memcpy(data,
 * ptr, bytes); data[bytes] = '\0';
 *
 *       args[0] = lcl_string_new(data);
 *       free(data);
 *       lcl_call_proc(ctx->interp, ctx->write_callback, 1, args, &result);
 *       if (result) lcl_ref_dec(result);
 *       lcl_ref_dec(args[0]);
 *       return bytes;
 *   }
 * ============================================================================
 */

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
 * Returns LCL_RC_OK on success, LCL_RC_ERR on error; *out is NULL on
 * error. LCL_RC_RETURN from the procedure is converted to LCL_RC_OK.
 * A `break` or `continue` escaping the procedure body is an error
 * ("break invoked outside a loop"): there is no loop of the caller's
 * for it to reach.
 *
 * The caller is responsible for:
 *   - Creating argument values (lcl_string_new, etc.)
 *   - Decrementing argument refcounts after the call
 *   - Decrementing the result refcount when done
 */
lcl_return_code lcl_call_proc(lcl_interp *interp, lcl_value *proc, int argc,
                              lcl_value **argv, lcl_value **out);

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
 *   lcl_return_code c_curl_new(..., lcl_value **out) {
 *       struct curl_context *ctx = calloc(1, sizeof(*ctx));
 *       ctx->curl = curl_easy_init();
 *       *out = lcl_opaque_new(ctx, "curl_context", curl_ctx_free);
 *       return LCL_RC_OK;
 *   }
 *
 *   lcl_return_code c_curl_set_url(...) {
 *       struct curl_context *ctx;
 *       if (lcl_opaque_get(argv[0], "curl_context", (void**)&ctx) != LCL_OK) {
 *           return LCL_RC_ERR;  // type mismatch
 *       }
 *       curl_easy_setopt(ctx->curl, CURLOPT_URL, ...);
 *       ...
 *   }
 * ============================================================================
 */

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
lcl_value *lcl_opaque_new(void *ptr, const char *type_tag,
                          lcl_finalizer finalizer);

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
lcl_result lcl_opaque_get(lcl_value *value, const char *expected_type,
                          void **out);

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
 * ============================================================================
 */

lcl_frame *lcl_frame_ref_inc(lcl_frame *f);
void lcl_frame_ref_dec(lcl_frame *f);

#ifdef __cplusplus
}
#endif

#endif /* LCL_H */
