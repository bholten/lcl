#ifndef LCL_NAME_H
#define LCL_NAME_H

#include <stddef.h>

int lcl_name_is_start(int c);
int lcl_name_is_char(int c);

/* Reference-name continuation set: `lcl_name_is_char` plus '-', '?',
 * '!'. Braced substitution accepts these anywhere after a segment
 * start; the bare `$name` form stays on `lcl_name_is_char` (POSIX
 * shell shape) by design. */
int lcl_name_is_ref_char(int c);

/* Validate a substitution reference (the contents of `${...}`).
 * Returns NULL when `s[0..n)` is a valid qualname, else a static
 * diagnostic message. */
const char *lcl_name_check_ref(const char *s, size_t n);

/* Behavior-preserving helpers for the definition-form guards and the
 * namespace path validator. Deliberately grammar-blind about segment
 * *content* (declaration names admit spellings like `+` that the
 * reference grammar does not); they centralize only the '::'
 * structure questions. */
int lcl_name_has_sep(const char *s);
int lcl_name_has_empty_seg(const char *s);

#endif
