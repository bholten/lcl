#ifndef LCL_PATH_H
#define LCL_PATH_H

/* Lexical module-path operations.
 *
 * Lcl module paths are lexical names: '/' separates components, and
 * '.', '..', and repeated separators are normalized by string rules
 * alone. The filesystem is never consulted — symlinks are not
 * resolved, and the platform's own path syntax (drive letters,
 * backslashes) is not interpreted. Hosts bridge Lcl paths to their
 * filesystem through ordinary file operations (fopen) and may supply
 * stronger module identity via lcl_set_module_key_fn.
 *
 * All functions return a malloc'd string (caller frees), or NULL on
 * out of memory. */

/* Normalize a path lexically:
 *   ""          -> "."
 *   "."         -> "."
 *   "a/."       -> "a"
 *   "a//b"      -> "a/b"
 *   "a/b/.."    -> "a"
 *   "a/../../b" -> "../b"   (relative paths keep excess "..")
 *   "/a/.."     -> "/"
 *   "/../../a"  -> "/a"     (".." cannot climb above a lexical root)
 *   "a/b/"      -> "a/b"    (no trailing separator) */
char *lcl_path_clean(const char *path);

/* Join `rel` onto `base` and clean the result. A rooted `rel`
 * (leading '/') ignores `base`, as does an empty `base`. */
char *lcl_path_join(const char *base, const char *rel);

/* Lexical parent directory: no '/' -> ".", root-only -> "/". Does not
 * clean its input. */
char *lcl_path_dirname(const char *path);

#endif
