#include <regex.h>
#include <stdlib.h>

#include <lcl.h>

#define REGEX_TYPE "regex_t"
#define REGEX_NS "regex"

struct lcl_regex {
  regex_t re;
};

struct lcl_regex *lcl_regex_new(void) {
  struct lcl_regex *r = malloc(sizeof(*r));

  if (!r) {
    return NULL;
  }

  return r;
}

void lcl_regex_free(struct lcl_regex *re) {
  if (!re) {
    return;
  }

  regfree(&re->re);
  free(re);
}

/* regex::regcomp pattern */
static int c_regcomp(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  struct lcl_regex *re = NULL;
  const char *pattern = NULL;
  int errcode;

  if (argc < 1) {
    lcl_set_error(interp, "regex::regcomp requires a pattern");
    return LCL_RC_ERR;
  }

  re = lcl_regex_new();

  if (!re) {
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &pattern) != LCL_OK) {
    free(re);
    return LCL_RC_ERR;
  }

  errcode = regcomp(&re->re, pattern, REG_EXTENDED | REG_NOSUB);

  if (errcode != 0) {
    free(re);
    lcl_set_error(interp, "invalid regex pattern");
    return LCL_RC_ERR;
  }

  *out = lcl_opaque_new(re, REGEX_TYPE, (lcl_finalizer)lcl_regex_free);

  return LCL_RC_OK;
}

/* regex::regexec regex string -> 1 if matched, 0 if not */
static int c_regexec(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  struct lcl_regex *re = NULL;
  int status;
  const char *str = NULL;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], REGEX_TYPE, (void **)&re) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  status = regexec(&re->re, str, (size_t)0, NULL, 0);

  *out = lcl_int_new(status == 0 ? 1 : 0);

  return LCL_RC_OK;
}

/* regex::match pattern string -> 1 if matched, 0 if not
   Convenience function that compiles and matches in one step. */
static int c_match(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  regex_t re;
  const char *pattern = NULL;
  const char *str = NULL;
  int errcode;
  int matched;

  if (argc < 2) {
    lcl_set_error(interp, "regex::match requires pattern and string");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &pattern) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  errcode = regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB);

  if (errcode != 0) {
    lcl_set_error(interp, "invalid regex pattern");
    return LCL_RC_ERR;
  }

  matched = (regexec(&re, str, 0, NULL, 0) == 0);
  regfree(&re);

  *out = lcl_int_new(matched ? 1 : 0);

  return LCL_RC_OK;
}

/* Note: regfree is handled automatically by the opaque value finalizer.
   No explicit free command is needed - regex values are freed when
   they go out of scope. */

/* Note: regerror is not exposed since errors are reported directly
   via the interpreter's error message when regcomp fails. */

void lcl_register_regex(lcl_interp *interp) {
  lcl_value *regex_ns = lcl_ns_new(REGEX_NS);
  lcl_define_take(interp, REGEX_NS, regex_ns);

  lcl_ns_def(regex_ns, "regcomp", lcl_c_proc_new("regex::regcomp", c_regcomp));
  lcl_ns_def(regex_ns, "regexec", lcl_c_proc_new("regex::regexec", c_regexec));
  lcl_ns_def(regex_ns, "match", lcl_c_proc_new("regex::match", c_match));
}
