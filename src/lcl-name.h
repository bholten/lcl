#ifndef LCL_NAME_H
#define LCL_NAME_H

#include <stddef.h>

typedef enum {
  LCL_NAME_INVALID = 0,
  LCL_NAME_SIMPLE,
  LCL_NAME_QUALIFIED
} lcl_name_kind;

int lcl_name_is_start(int c);
int lcl_name_is_char(int c);

lcl_name_kind lcl_name_classify_n(const char *s, size_t n);
lcl_name_kind lcl_name_classify(const char *s);

/* Validate a substitution reference (the contents of `${...}`).
 * Returns NULL when `s[0..n)` is a valid qualname, else a static
 * diagnostic message. */
const char *lcl_name_check_ref(const char *s, size_t n);

/* Behavior-preserving helpers for the definition-form guards and the
 * namespace path validator. Deliberately grammar-blind about segment
 * *content* (declaration names admit spellings like `my-proc` that
 * the substitution grammar does not); they centralize only the '::'
 * structure questions. */
int lcl_name_has_sep(const char *s);
int lcl_name_has_empty_seg(const char *s);

#endif
