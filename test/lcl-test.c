#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash-table.h"
#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"
#include "lcl-stdlib.h"

/* ---------------------------------------------------------------------------
 * OOM injection (linked via -Wl,--wrap=calloc,--wrap=strndup).
 * Set the counter to N to make the (N+1)th call return NULL; subsequent
 * calls work normally. Set to -1 (default) to disable.
 * --------------------------------------------------------------------------- */

extern void *__real_calloc(size_t nmemb, size_t size);
extern char *__real_strndup(const char *s, size_t n);
extern char *__real_strdup(const char *s);
extern int   __real_lcl_program_push_command(lcl_program *p, lcl_command *src);
extern int   __real_hash_table_put(hash_table *ht, const char *key,
                                   lcl_value *value);

static int oom_calloc_fail_at  = -1;  /* -1: disabled */
static int oom_strndup_fail_at = -1;
static int oom_strdup_fail_at  = -1;
static int oom_push_command_fail_at = -1;
static int oom_hash_put_fail_at = -1;

/* When set, fail the next strdup whose argument equals this string exactly.
 * Used to target a specific strdup call in noisy paths without counting. */
static const char *oom_strdup_fail_match = NULL;

void *__wrap_calloc(size_t nmemb, size_t size) {
  if (oom_calloc_fail_at == 0) {
    oom_calloc_fail_at = -1;
    return NULL;
  }

  if (oom_calloc_fail_at > 0) {
    oom_calloc_fail_at--;
  }

  return __real_calloc(nmemb, size);
}

char *__wrap_strndup(const char *s, size_t n) {
  if (oom_strndup_fail_at == 0) {
    oom_strndup_fail_at = -1;
    return NULL;
  }

  if (oom_strndup_fail_at > 0) {
    oom_strndup_fail_at--;
  }

  return __real_strndup(s, n);
}

char *__wrap_strdup(const char *s) {
  if (oom_strdup_fail_match && s && strcmp(s, oom_strdup_fail_match) == 0) {
    oom_strdup_fail_match = NULL;
    return NULL;
  }

  if (oom_strdup_fail_at == 0) {
    oom_strdup_fail_at = -1;
    return NULL;
  }

  if (oom_strdup_fail_at > 0) {
    oom_strdup_fail_at--;
  }

  return __real_strdup(s);
}

int __wrap_lcl_program_push_command(lcl_program *p, lcl_command *src) {
  if (oom_push_command_fail_at == 0) {
    oom_push_command_fail_at = -1;
    return 0;
  }

  if (oom_push_command_fail_at > 0) {
    oom_push_command_fail_at--;
  }

  return __real_lcl_program_push_command(p, src);
}

int __wrap_hash_table_put(hash_table *ht, const char *key, lcl_value *value) {
  if (oom_hash_put_fail_at == 0) {
    oom_hash_put_fail_at = -1;
    return 0;
  }

  if (oom_hash_put_fail_at > 0) {
    oom_hash_put_fail_at--;
  }

  return __real_hash_table_put(ht, key, value);
}

/**
   Testing
**/

typedef struct {
  char  *s;
  size_t len, cap;
} SB;

static void sb_init(SB *b) {
  b->s = NULL;
  b->len = 0;
  b->cap = 0;
}

static int sb_reserve(SB *b, size_t need) {
  size_t cap = b->cap ? b->cap : 64;
  char *p;

  while (cap < need) {
    cap <<= 1u;
  }

  if (cap == b->cap) return 1;

  p = (char*)realloc(b->s, cap);

  if (!p) return 0;

  b->s = p; b->cap = cap;

  return 1;
}

static int sb_putc(SB *b, int c) {
  if (!sb_reserve(b, b->len + 2)) return 0;

  b->s[b->len++] = (char)c;
  b->s[b->len] = '\0';

  return 1;
}

static int sb_puts(SB *b, const char *s) {
  size_t n = strlen(s);

  if (!sb_reserve(b, b->len + n + 1)) return 0;

  memcpy(b->s + b->len, s, n + 1);
  b->len += n;

  return 1;
}

static void sb_free(SB *b) {
  free(b->s); b->s = NULL; b->len = b->cap = 0;
}

/** Dump IL **/

static int dump_esc_lit(SB *b, const char *s, size_t n) {
  size_t i;
  if (!sb_puts(b, "\"")) return 0;

  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];

    if (c == '\\' || c == '\"') {
      if (!sb_putc(b, '\\')) {
        return 0;
      }

      if (!sb_putc(b, c)) {
        return 0;
      }
    } else if (c < 32 || c == 127) {
      char tmp[5];
      sprintf(tmp, "\\x%02X", (unsigned)c);

      if (!sb_puts(b, tmp)) return 0;
    } else {
      if (!sb_putc(b, c)) return 0;
    }
  }

  return sb_puts(b, "\"");
}

static int dump_program_rec(const lcl_program *P, SB *b, int depth);

static int dump_word(const lcl_word *w, SB *b, int depth) {
  int i;
  for (i = 0; i < w->np; i++) {
    const lcl_word_piece *pc = &w->wp[i];

    if (!sb_puts(b, "[")) return 0;

    if (pc->kind == LCL_WP_LIT) {
      if (!sb_puts(b, "lit:")) return 0;
      if (!dump_esc_lit(b, pc->as.lit.s, pc->as.lit.n)) return 0;
    } else if (pc->kind == LCL_WP_VAR) {
      if (!sb_puts(b, "var:")) return 0;
      if (!dump_esc_lit(b, pc->as.var.name, strlen(pc->as.var.name))) return 0;
    } else { /* LCL_WP_SUBCMD */
      if (!sb_puts(b, "sub:{")) return 0;
      if (!dump_program_rec(pc->as.sub.program, b, depth + 1)) return 0;
      if (!sb_puts(b, "}")) return 0;
    }

    if (!sb_puts(b, "]")) return 0;
    if (i+1 < w->np) { if (!sb_puts(b, " ")) return 0; }
  }
  return 1;
}

static int dump_command(const lcl_command *c, SB *b, int depth) {
  int i;
  if (!sb_puts(b, "cmd(")) return 0;
  { char tmp[32]; sprintf(tmp, "line=%d", c->line); if (!sb_puts(b, tmp)) return 0; }
  if (!sb_puts(b, "):")) return 0;

  for (i = 0; i < c->argc; i++) {
    if (!sb_puts(b, " ")) return 0;
    if (!dump_word(&c->w[i], b, depth)) return 0;
    if (i+1 < c->argc) { /* optional extra space between words if you want */ }
  }
  return 1;
}

static int dump_program_rec(const lcl_program *P, SB *b, int depth) {
  int i;

  for (i = 0; i < P->ncmd; i++) {
    if (i) {
      if (!sb_puts(b, " ; ")) {
        return 0;
      }
    }

    if (!dump_command(&P->cmd[i], b, depth)) {
      return 0;
    }
  }

  return 1;
}

static int lcl_program_dump(const lcl_program *P, char **out_str) {
  SB b;
  sb_init(&b);
  if (!P) return 0;

  if (!dump_program_rec(P, &b, 0)) {
    sb_free(&b);
    return 0;
  }

  *out_str = b.s;

  return 1;
}

/** Signatures **/
int  lcl_program_dump(const lcl_program *P, char **out_str);
lcl_program *lcl_compile(const char *src, const char *file);
void lcl_program_free(lcl_program *P);

#define ASSERT_TRUE(cond) do { if (!(cond)) { \
  printf("    assert failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
  return 0; } } while(0)

#define ASSERT_STREQ(a,b) do { if (((a)==NULL && (b)!=NULL) || ((a)!=NULL && (b)==NULL) || strcmp((a),(b))!=0) { \
  printf("    assert failed: strings not equal\n    A: %s\n    B: %s\n", (a)?(a):"(null)", (b)?(b):"(null)"); \
  return 0; } } while(0)

static int compile_and_dump(const char *src, const char *expect) {
  lcl_program *P = lcl_program_compile(src, "test.lcl");
  char *got = NULL;
  int ok;

  if (!P && expect == NULL) return 1;

  ASSERT_TRUE(P != NULL);
  ASSERT_TRUE(lcl_program_dump(P, &got));

  ok = (strcmp(got, expect) == 0);

  if (!ok) {
    printf("    got:    %s\n", got);
    printf("    expect: %s\n", expect);
  }
  
  free(got);
  lcl_program_free(P);
  return ok;
}

static int test_simple_words(void) {
  const char *src =
    "set x 10 ; puts $x\n";
  const char *exp =
    "cmd(line=1): [lit:\"set\"] [lit:\"x\"] [lit:\"10\"] ; "
    "cmd(line=1): [lit:\"puts\"] [var:\"x\"]";
  return compile_and_dump(src, exp);
}

static int test_comments(void) {
  const char *src =
    ";; leading comment\n"
    "set a 1  ;; trailing comment on same line\n"
    "puts $a\n";
  const char *exp =
    "cmd(line=2): [lit:\"set\"] [lit:\"a\"] [lit:\"1\"] ; "
    "cmd(line=3): [lit:\"puts\"] [var:\"a\"]";
  return compile_and_dump(src, exp);
}

static int test_braces_literal(void) {
  const char *src =
    "set s {a $b [c] \\x41}\n";
  const char *exp =
    "cmd(line=1): [lit:\"set\"] [lit:\"s\"] [lit:\"a $b [c] \\\\x41\"]";
  return compile_and_dump(src, exp);
}

static int test_quotes_and_subst(void) {
  const char *src =
    "set s \"a $b [echo hi]\"\n";
  const char *exp =
    "cmd(line=1): [lit:\"set\"] [lit:\"s\"] [lit:\"a \"] [var:\"b\"] [lit:\" \"] [sub:{cmd(line=1): [lit:\"echo\"] [lit:\"hi\"]}]";
  return compile_and_dump(src, exp);
}

static int test_nested_subcmd(void) {
  const char *src = "set x [f [g 1] 2]\n";
  const char *exp =
    "cmd(line=1): [lit:\"set\"] [lit:\"x\"] [sub:{cmd(line=1): [lit:\"f\"] [sub:{cmd(line=1): [lit:\"g\"] [lit:\"1\"]}] [lit:\"2\"]}]";
  return compile_and_dump(src, exp);
}

static int test_unmatched_brace_error(void) {
  const char *src = "set a {oops\n";
  /* expect compile failure → NULL program */
  return compile_and_dump(src, NULL);
}

static int test_brace_backslash_balance(void) {
  /* {foo \} bar} should compile: backslash escapes the } for balancing */
  const char *src = "puts {foo \\} bar}\n";
  const char *exp =
    "cmd(line=1): [lit:\"puts\"] [lit:\"foo \\\\} bar\"]";
  return compile_and_dump(src, exp);
}

static int test_hex_escape_2digits(void) {
  /* \x41 -> 'A' (2 hex digits); \x4 -> literal \x4 (not enough digits) */
  const char *src = "puts \"\\x41\\x4\"\n";
  const char *exp =
    "cmd(line=1): [lit:\"puts\"] [lit:\"A\"] [lit:\"\\\\x\"] [lit:\"4\"]";
  return compile_and_dump(src, exp);
}

static int test_unknown_escape_literal(void) {
  /* \q -> literal \q (unknown escape preserves backslash) */
  const char *src = "puts \"\\q\"\n";
  const char *exp =
    "cmd(line=1): [lit:\"puts\"] [lit:\"\\\\q\"]";
  return compile_and_dump(src, exp);
}

static int test_octal_escape(void) {
  /* \101 -> 'A' (octal 101 = 65 = 0x41) */
  const char *src = "puts \"\\101\"\n";
  const char *exp =
    "cmd(line=1): [lit:\"puts\"] [lit:\"A\"]";
  return compile_and_dump(src, exp);
}

static int test_quoted_close_paren_in_list(void) {
  /* Quoted ")" inside () inside [] must not misbalance the parens.
   * (bar ")" baz) desugars to [list bar ")" baz] */
  const char *src = "puts [concat (bar \")\" baz)]\n";
  const char *exp =
    "cmd(line=1): [lit:\"puts\"] [sub:{cmd(line=1): [lit:\"concat\"] "
    "[sub:{cmd(line=1): [lit:\"list\"] [lit:\"bar\"] [lit:\")\"] [lit:\"baz\"]}]}]";
  return compile_and_dump(src, exp);
}

static int test_comment_in_list_literal(void) {
  /* ;; comment inside () should be stripped — "comment" must not appear */
  const char *src = "puts (foo ;; comment\nbar)\n";
  const char *exp =
    "cmd(line=1): [lit:\"puts\"] [sub:{cmd(line=1): [lit:\"list\"] "
    "[lit:\"foo\"] [lit:\"bar\"]}]";
  return compile_and_dump(src, exp);
}

/* ---------------------------------------------------------------------------
 * Issue #22 — COW clone failure causes NULL deref in dict/list mutation.
 *
 * Reproduction:
 *   1. Build a container with refc > 1 (shared).
 *   2. Force the clone allocator (calloc inside lcl_dict_new/lcl_list_new)
 *      to fail on the next call.
 *   3. Call the mutating op (put/del/push/set).
 *
 * Buggy behavior: lcl_dict_clone_shallow returns NULL; the caller does
 *   `lcl_ref_dec(dict); *io = dict = NULL; free(dict->str_repr);` — NULL deref.
 *   The test process crashes (SEGV or ASan abort).
 *
 * Fixed behavior: the mutator returns LCL_ERROR with *io unchanged.
 * --------------------------------------------------------------------------- */

static int test_issue22_dict_put_clone_oom(void) {
  lcl_value *dict, *original;
  lcl_value *val = lcl_string_new("v");
  lcl_result rc;

  dict = lcl_dict_new();
  ASSERT_TRUE(dict != NULL);

  /* Share the dict so the mutator must clone. */
  original = lcl_ref_inc(dict);

  /* Next calloc fails (used by lcl_dict_new inside clone_shallow). */
  oom_calloc_fail_at = 0;

  rc = lcl_dict_put(&dict, "k", val);

  ASSERT_TRUE(rc == LCL_ERROR);
  ASSERT_TRUE(dict != NULL);              /* *io must NOT have been NULL'd */
  ASSERT_TRUE(dict == original);          /* still the original */

  lcl_ref_dec(val);
  lcl_ref_dec(dict);
  lcl_ref_dec(original);
  return 1;
}

static int test_issue22_dict_del_clone_oom(void) {
  lcl_value *dict, *original;
  lcl_value *val = lcl_string_new("v");
  lcl_result rc;

  dict = lcl_dict_new();
  ASSERT_TRUE(dict != NULL);
  ASSERT_TRUE(lcl_dict_put(&dict, "k", val) == LCL_OK);
  lcl_ref_dec(val);

  original = lcl_ref_inc(dict);
  oom_calloc_fail_at = 0;

  rc = lcl_dict_del(&dict, "k");

  ASSERT_TRUE(rc == LCL_ERROR);
  ASSERT_TRUE(dict != NULL);
  ASSERT_TRUE(dict == original);

  lcl_ref_dec(dict);
  lcl_ref_dec(original);
  return 1;
}

static int test_issue22_list_push_clone_oom(void) {
  lcl_value *list, *original;
  lcl_value *val = lcl_string_new("v");
  lcl_result rc;

  list = lcl_list_new();
  ASSERT_TRUE(list != NULL);

  original = lcl_ref_inc(list);
  oom_calloc_fail_at = 0;

  rc = lcl_list_push(&list, val);

  ASSERT_TRUE(rc == LCL_ERROR);
  ASSERT_TRUE(list != NULL);
  ASSERT_TRUE(list == original);

  lcl_ref_dec(val);
  lcl_ref_dec(list);
  lcl_ref_dec(original);
  return 1;
}

static int test_issue22_list_set_clone_oom(void) {
  lcl_value *list, *original;
  lcl_value *initial = lcl_string_new("a");
  lcl_value *replacement = lcl_string_new("b");
  lcl_result rc;

  list = lcl_list_new();
  ASSERT_TRUE(list != NULL);
  ASSERT_TRUE(lcl_list_push(&list, initial) == LCL_OK);
  lcl_ref_dec(initial);

  original = lcl_ref_inc(list);
  oom_calloc_fail_at = 0;

  rc = lcl_list_set(&list, 0, replacement);

  ASSERT_TRUE(rc == LCL_ERROR);
  ASSERT_TRUE(list != NULL);
  ASSERT_TRUE(list == original);

  lcl_ref_dec(replacement);
  lcl_ref_dec(list);
  lcl_ref_dec(original);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Issue #35 — Public API functions inconsistently NULL-deref on NULL value
 * arguments. Embedders routinely pass NULL (e.g. an `out` from a failed
 * call); sibling functions (e.g. `lcl_list_*`, `lcl_define`, `lcl_get`)
 * already guard. This pins the contract for the remaining functions.
 * --------------------------------------------------------------------------- */

static int test_issue35_public_api_null_safety(void) {
  long iv = 0;
  double fv = 0.0;
  lcl_value *out = NULL;
  lcl_value *null_dict = NULL;

  /* No-return-value functions — must simply not crash. */
  (void)lcl_value_type_of(NULL);
  (void)lcl_dict_len(NULL);

  /* Functions with an `lcl_result` must return LCL_ERROR cleanly. */
  ASSERT_TRUE(lcl_dict_get(NULL, "k", &out) == LCL_ERROR);
  ASSERT_TRUE(lcl_dict_put(&null_dict, "k", NULL) == LCL_ERROR);
  ASSERT_TRUE(lcl_dict_del(&null_dict, "k") == LCL_ERROR);
  ASSERT_TRUE(lcl_dict_keys(NULL, &out) == LCL_ERROR);

  ASSERT_TRUE(lcl_value_to_int(NULL, &iv) == LCL_ERROR);
  ASSERT_TRUE(lcl_value_to_float(NULL, &fv) == LCL_ERROR);

  return 1;
}

/* ---------------------------------------------------------------------------
 * `lcl_value_to_string` returning "" was indistinguishable from
 * an OOM during reify or a NULL input. The fix tightens the contract:
 * NULL input or stringification OOM return NULL; genuine empty strings
 * still return "". `lcl_value_to_cstring` is the error-surfacing helper:
 * it converts a NULL return into an interp "out of memory" error.
 *
 * Companion hardening to `lcl_string_new`: `str == NULL` is treated as
 * the empty string. A STRING value's `str_repr` is its content, with no
 * lazy-reify path to recover it; allowing `str_repr == NULL` for a
 * STRING would make every downstream stringify look like an OOM. Callers
 * like `io::getenv` of an unset variable used to pass NULL into this
 * constructor and silently get "" — the new behavior is identical from
 * the caller's perspective but no longer poisons the value.
 * --------------------------------------------------------------------------- */
static int test_issue15_to_string_null_contract(void) {
  extern lcl_interp *lcl_test_interp;
  extern const char *lcl_interp_error_msg(lcl_interp *interp);
  lcl_value *v;
  const char *out;
  const char *err;

  /* (a) NULL input → NULL (was: "" pre-fix). */
  ASSERT_TRUE(lcl_value_to_string(NULL) == NULL);

  /* (b) cstring helper on NULL input: returns LCL_ERROR, sets interp
   *     error, leaves *out untouched. */
  lcl_clear_error(lcl_test_interp);
  out = (const char *)0x1; /* sentinel */
  ASSERT_TRUE(lcl_value_to_cstring(lcl_test_interp, NULL, &out) == LCL_ERROR);
  ASSERT_TRUE(out == (const char *)0x1);
  err = lcl_interp_error_msg(lcl_test_interp);
  ASSERT_TRUE(err != NULL && strstr(err, "out of memory") != NULL);
  lcl_clear_error(lcl_test_interp);

  /* (c) Genuine empty string returns "" — not NULL. */
  v = lcl_string_new("");
  ASSERT_TRUE(v != NULL);
  out = lcl_value_to_string(v);
  ASSERT_TRUE(out != NULL);
  ASSERT_STREQ(out, "");

  /* (d) Successful cstring call returns LCL_OK and writes *out. */
  lcl_ref_dec(v);
  v = lcl_string_new("hello");
  ASSERT_TRUE(v != NULL);
  out = NULL;
  ASSERT_TRUE(lcl_value_to_cstring(lcl_test_interp, v, &out) == LCL_OK);
  ASSERT_TRUE(out != NULL);
  ASSERT_STREQ(out, "hello");
  lcl_ref_dec(v);

  /* (e) `lcl_string_new(NULL)` produces a valid empty string, not a
   *     poison value. Regression for io::getenv-class bugs that pass
   *     a possibly-NULL C string into the constructor. */
  v = lcl_string_new(NULL);
  ASSERT_TRUE(v != NULL);
  ASSERT_TRUE(v->type == LCL_STRING);
  ASSERT_TRUE(v->str_repr != NULL);
  out = lcl_value_to_string(v);
  ASSERT_TRUE(out != NULL);
  ASSERT_STREQ(out, "");
  out = NULL;
  ASSERT_TRUE(lcl_value_to_cstring(lcl_test_interp, v, &out) == LCL_OK);
  ASSERT_STREQ(out, "");
  lcl_ref_dec(v);

  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Issue #59 — `lcl_eval_program` borrowed `pr->file` into `interp->cur_file`
 * without restoring on exit. When the caller then freed the program, any
 * later `LCL_ERR_MSG` (which strdups `cur_file`) read freed memory. Fixed
 * by save+restore around the eval. This test deliberately mimics the
 * pattern: compile a fresh program, eval it, free it, then trigger an
 * error and rely on ASan to catch the UAF if the fix regressed.
 * --------------------------------------------------------------------------- */
static int test_issue59_eval_program_cur_file_uaf(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_program *prog;
  lcl_value *result = NULL;
  int rc;

  /* Start from a known cur_file. */
  lcl_test_interp->cur_file = NULL;

  prog = lcl_program_compile("+ 1 1", "i59.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  ASSERT_TRUE(rc == LCL_RC_OK);
  if (result) lcl_ref_dec(result);

  /* Post-fix: cur_file must be restored to its pre-eval value (NULL),
   * not still pointing at prog->file. */
  ASSERT_TRUE(lcl_test_interp->cur_file == NULL);

  lcl_program_free(prog);

  /* If cur_file still pointed into the freed program, this strdup
   * would be a use-after-free that ASan catches. */
  LCL_ERR_MSG(lcl_test_interp, "post-free probe");

  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Issue #33 — `c_catch` doesn't NULL-check `strdup` of its result/error
 * var-name arguments. On OOM, the strdup of `result_var` returns NULL and
 * the function silently completes without binding the user's variable —
 * a confusing failure where downstream `$result_var` lookups error as
 * "unbound name" with no indication of the actual cause. The fix: detect
 * the strdup failure and return LCL_RC_ERR with a clear message.
 * --------------------------------------------------------------------------- */

static int test_issue33_catch_strdup_oom(void) {
  /* The lcl-test binary uses one global interp set up by main(). Compile
   * a fresh program here and target the specific strdup of the result-var
   * name with `oom_strdup_fail_match`. */
  extern lcl_interp *lcl_test_interp;  /* set by main */
  lcl_program *prog;
  lcl_value *result = NULL;
  int rc;

  prog = lcl_program_compile("catch { + 1 1 } __i33_resvar", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  oom_strdup_fail_match = "__i33_resvar";
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  oom_strdup_fail_match = NULL;

  /* After fix: catch must propagate the OOM as an error.
   * Before fix: silently succeeds, leaves __i33_resvar unbound, returns 0. */
  ASSERT_TRUE(rc == LCL_RC_ERR);

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_program_free(prog);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Issue #27 — FNV-1a uses a corrupted offset basis and a not-portably-wide
 * integer type. The canonical 64-bit FNV-1a offset basis is
 * 0xCBF29CE484222325 (14695981039346656037); the buggy code dropped a
 * digit (1469598103934665603). This test computes the hash of "foo"
 * against the standard FNV-1a expected value 0xDCB27518FED9D577.
 * --------------------------------------------------------------------------- */

static int test_issue27_fnv1a_known_vector(void) {
  /* Standard FNV-1a 64-bit hashes (https://datatracker.ietf.org/doc/html/draft-eastlake-fnv) */
  ASSERT_TRUE(lcl_hash_fnv1a("foo") == 0xDCB27518FED9D577ULL);
  ASSERT_TRUE(lcl_hash_fnv1a("a")   == 0xAF63DC4C8601EC8CULL);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Issue #26 — hash_table_put over-increments `used` on tombstone reuse.
 *
 * Insert→delete→insert of the same key should leave used == len == 1.
 * Buggy code unconditionally does `used++` regardless of whether the
 * chosen slot was H_EMPTY or H_TOMB, so the second insert produces
 * used == 2 (the tombstone already counted toward used).
 * --------------------------------------------------------------------------- */

static int test_issue26_hash_used_tombstone_reuse(void) {
  hash_table *ht = hash_table_new();
  lcl_value *v = lcl_string_new("x");

  ASSERT_TRUE(ht != NULL);
  ASSERT_TRUE(v != NULL);

  ASSERT_TRUE(hash_table_put(ht, "a", v));
  ASSERT_TRUE(ht->used == 1);
  ASSERT_TRUE(ht->len == 1);

  ASSERT_TRUE(hash_table_delete(ht, "a"));
  ASSERT_TRUE(ht->used == 1);  /* tomb still counted */
  ASSERT_TRUE(ht->len == 0);

  /* Reuse the tombstone slot. */
  ASSERT_TRUE(hash_table_put(ht, "a", v));
  ASSERT_TRUE(ht->used == 1);  /* MUST NOT double-count */
  ASSERT_TRUE(ht->len == 1);

  hash_table_free(ht);
  lcl_ref_dec(v);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Issue #24 — Scanner $name branch dereferences NULL strndup result.
 *
 * Reproduction: compile a script containing `$name` (the non-`::`-suffix
 * branch in lcl-scan.c) with strndup forced to return NULL.
 *
 * Buggy behavior: lcl_word_add_var(NULL) → strlen(NULL) → segfault.
 * Fixed behavior: lcl_program_compile returns NULL cleanly.
 * --------------------------------------------------------------------------- */

/* ISSUES #47 — Param/upvalue binding ignores OOM. We invoke a lambda
 * whose body references `+`, so `+` becomes an upvalue. The upvalue-bind
 * loop in lcl_call_user_proc runs BEFORE the param-bind loop, so the
 * first hash_table_put after we set the counter is the upval bind for
 * `+`.  Pre-fix: the failure is swallowed; body looks up `+` in the
 * parent frame chain (falls through to the global ns), so it appears to
 * succeed — eval returns OK with the binding silently missing. That's
 * the exact "silent drop" the issue describes. Post-fix: the failure
 * propagates as LCL_RC_ERR.  */
static int test_issue47_param_bind_oom(void) {
  extern lcl_interp *lcl_test_interp;
  extern const char *lcl_interp_error_msg(lcl_interp *interp);
  lcl_program *prog;
  lcl_value *result = NULL;
  const char *err;
  int rc;

  /* Use lambda (no self_name) so the FIRST hash_table_put inside
   * lcl_call_user_proc is the param-bind via lcl_env_let. */
  prog = lcl_program_compile("[lambda {x} { + $x 1 }] 10", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  lcl_clear_error(lcl_test_interp);
  oom_hash_put_fail_at = 0;

  rc = lcl_eval_program(lcl_test_interp, prog, &result);

  oom_hash_put_fail_at = -1;
  err = lcl_interp_error_msg(lcl_test_interp);

  ASSERT_TRUE(rc == LCL_RC_ERR);
  /* Post-fix: error mentions parameter / out-of-memory.
   * Pre-fix: error mentions "undefined" (from body trying to read $x). */
  ASSERT_TRUE(err != NULL);
  ASSERT_TRUE(strstr(err, "undefined") == NULL);

  if (result) lcl_ref_dec(result);
  lcl_program_free(prog);
  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* ISSUE #45 — `skip_balanced` recurses on alternating `(`/`[`. A
 * crafted `[([([(... )])])]` of depth N exhausts the C stack. We build
 * the worst case at depth 300 and expect `lcl_program_compile` to
 * return NULL cleanly (parse error). Pre-fix: ASan SEGV from stack
 * overflow. */
static int test_issue45_skip_balanced_depth_limit(void) {
  enum { DEPTH = 20000 };
  size_t total = (size_t)DEPTH * 2 * 2;  /* DEPTH "([" + DEPTH "])" */
  char *src = (char *)malloc(total + 1);
  size_t i;
  lcl_program *prog;

  ASSERT_TRUE(src != NULL);
  for (i = 0; i < DEPTH; i++) {
    src[i * 2]     = '[';
    src[i * 2 + 1] = '(';
  }
  for (i = 0; i < DEPTH; i++) {
    src[DEPTH * 2 + i * 2]     = ')';
    src[DEPTH * 2 + i * 2 + 1] = ']';
  }
  src[total] = '\0';

  prog = lcl_program_compile(src, "test.lcl");

  /* Either NULL (parse error from depth limit) or non-NULL (somehow
   * succeeded). What we MUST NOT see is a crash. */
  ASSERT_TRUE(prog == NULL);

  if (prog) lcl_program_free(prog);
  free(src);
  return 1;
}

/* ISSUE #42 — `lcl_dict_clone_shallow` ignores `hash_table_put` return.
 * On OOM the clone is silently truncated. We force the first put inside
 * the clone iteration to fail, trigger COW on a shared dict, and verify:
 * (a) lcl_dict_put returns LCL_ERROR, (b) the caller's dict pointer is
 * unchanged, and (c) LSan sees no leak from the partial clone. */
static int test_issue42_dict_clone_put_oom(void) {
  lcl_value *d;
  lcl_value *shared;
  lcl_result rc;
  lcl_value *kept;

  d = lcl_dict_new();
  ASSERT_TRUE(d != NULL);
  {
    lcl_value *v1 = lcl_string_new("v1");
    lcl_value *v2 = lcl_string_new("v2");
    ASSERT_TRUE(lcl_dict_put(&d, "k1", v1) == LCL_OK);
    ASSERT_TRUE(lcl_dict_put(&d, "k2", v2) == LCL_OK);
    lcl_ref_dec(v1);  /* dict_put didn't take ownership */
    lcl_ref_dec(v2);
  }

  /* Bump refc to force COW on the next put. */
  shared = lcl_ref_inc(d);

  /* Fail the first hash_table_put after this point — it will be the
   * first iterator-put inside lcl_dict_clone_shallow. */
  oom_hash_put_fail_at = 0;

  {
    lcl_value *v3 = lcl_string_new("v3");
    rc = lcl_dict_put(&d, "k3", v3);
    lcl_ref_dec(v3);  /* dict_put doesn't take ownership */
  }

  oom_hash_put_fail_at = -1;

  ASSERT_TRUE(rc == LCL_ERROR);
  /* On clone failure, dict_io must NOT have been swapped. */
  ASSERT_TRUE(d == shared);

  /* Original dict still intact: "k1" and "k2" still resolvable. */
  ASSERT_TRUE(lcl_dict_get(d, "k1", &kept) == LCL_OK);
  lcl_ref_dec(kept);
  ASSERT_TRUE(lcl_dict_get(d, "k2", &kept) == LCL_OK);
  lcl_ref_dec(kept);

  lcl_ref_dec(shared);
  lcl_ref_dec(d);
  return 1;
}

/* ISSUE #41 — `s_namespace`'s re-entry pre-population loop does not
 * check the return of `hash_table_put` / `lcl_dict_put`. On OOM the
 * overlay and exports drift apart and we proceed into the body with a
 * silently-inconsistent builder. Setup creates `__i41_ns` with one
 * binding; re-entry triggers the pre-pop loop, where we force the first
 * hash_table_put to fail and verify s_namespace bails with LCL_RC_ERR
 * (and no LSan leak). */
static int test_issue41_namespace_reentry_oom(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_program *setup;
  lcl_program *reentry;
  lcl_value *result = NULL;
  int rc;

  setup = lcl_program_compile("namespace __i41_ns { let x 1 }", "test.lcl");
  ASSERT_TRUE(setup != NULL);
  rc = lcl_eval_program(lcl_test_interp, setup, &result);
  ASSERT_TRUE(rc == LCL_RC_OK);
  if (result) { lcl_ref_dec(result); result = NULL; }
  lcl_program_free(setup);

  reentry = lcl_program_compile("namespace __i41_ns { let y 2 }", "test.lcl");
  ASSERT_TRUE(reentry != NULL);

  /* The first hash_table_put after this point is the pre-pop loop's
   * hash_table_put(overlay->locals, "x", ...) in s_namespace. */
  oom_hash_put_fail_at = 0;

  rc = lcl_eval_program(lcl_test_interp, reentry, &result);

  /* Disable any residual counter (defensive — should be -1 already). */
  oom_hash_put_fail_at = -1;

  ASSERT_TRUE(rc == LCL_RC_ERR);

  if (result) lcl_ref_dec(result);
  lcl_program_free(reentry);
  return 1;
}

/* ISSUE #40 — `s_namespace` discards the +1 ref returned by
 * `lcl_def_target_pop` on the max-recursion-depth error branch, leaking
 * the exports dict it owned. We trigger the branch by setting
 * `max_depth = depth + 1` so the first `namespace` nested call hits the
 * cap. LSan catches the leaked dict pre-fix; clean post-fix.
 *
 * (LCL's command is bare `namespace name { body }` — no `eval` keyword,
 * see project-lcl-namespace-no-eval.) */
static int test_issue40_namespace_max_depth_leak(void) {
  extern lcl_interp *lcl_test_interp;
  int saved_max_depth = lcl_test_interp->max_depth;
  int saved_depth = lcl_test_interp->depth;
  lcl_program *prog;
  lcl_value *result = NULL;
  int rc;

  lcl_test_interp->max_depth = saved_depth + 1;

  prog = lcl_program_compile("namespace __i40_ns { let x 1 }", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);

  lcl_test_interp->max_depth = saved_max_depth;

  ASSERT_TRUE(rc == LCL_RC_ERR);

  if (result) lcl_ref_dec(result);
  lcl_program_free(prog);
  return 1;
}

/* ISSUE #39 — s_import dereferences `as.cell.inner` without NULL-check.
 * lcl_frame_clear / lcl_frame_free's cycle-breaker NULLs cell.inner for
 * lambda-captured cells; if `import` is then called on such a cell, the
 * type-check (line ~2608) segfaults on NULL. We simulate the cleared-cell
 * state directly (`lcl_cell_new(NULL)`) and verify `import` errors cleanly. */
static int test_issue39_import_cleared_cell(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *cell;
  lcl_program *prog;
  lcl_value *result = NULL;
  int rc;

  cell = lcl_cell_new(NULL);
  ASSERT_TRUE(cell != NULL);
  ASSERT_TRUE(cell->as.cell.inner == NULL);

  /* Bind the broken cell so `import __i39_broken` resolves to it. */
  ASSERT_TRUE(lcl_env_let(&lcl_test_interp->env, "__i39_broken", cell)
              == LCL_OK);
  lcl_ref_dec(cell);

  prog = lcl_program_compile("import __i39_broken", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  /* Pre-fix: segfault. Post-fix: clean LCL_RC_ERR. */
  ASSERT_TRUE(rc == LCL_RC_ERR);

  if (result) lcl_ref_dec(result);
  lcl_program_free(prog);
  return 1;
}

/* ISSUE #58 — `lcl_eval_word` returned LCL_RC_OK with `*out == NULL` when
 * `$name` resolved to a cell whose `inner` was NULLed by the cycle-breaker.
 * That violated the "OK ⇒ non-NULL out" contract every caller relied on, and
 * silently propagated NULL through `$cleared` references. The fix is
 * defense-in-depth: `lcl_cell_get` rejects NULL inner, and `lcl_eval_word`
 * sets a "use of cleared cell" error message before propagating. */
static int test_issue58_cleared_cell_returns_error(void) {
  extern lcl_interp *lcl_test_interp;
  extern const char *lcl_interp_error_msg(lcl_interp *interp);
  lcl_value *cell;
  lcl_program *prog;
  lcl_value *result = NULL;
  const char *err;
  int rc;

  cell = lcl_cell_new(NULL);
  ASSERT_TRUE(cell != NULL);
  ASSERT_TRUE(cell->as.cell.inner == NULL);

  ASSERT_TRUE(lcl_env_let(&lcl_test_interp->env, "__i58_broken", cell)
              == LCL_OK);
  lcl_ref_dec(cell);

  /* Single-piece var ref — exercises lcl_eval_word's WP_VAR case. */
  prog = lcl_program_compile("$__i58_broken", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  /* Pre-fix: rc == LCL_RC_OK, result == NULL.
   * Post-fix: rc == LCL_RC_ERR, message set. */
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(result == NULL);
  err = lcl_interp_error_msg(lcl_test_interp);
  ASSERT_TRUE(err != NULL && strstr(err, "cleared cell") != NULL);

  lcl_program_free(prog);

  /* Multi-piece concatenation — exercises lcl_eval_word_to_str. */
  lcl_clear_error(lcl_test_interp);
  prog = lcl_program_compile("let x \"prefix=$__i58_broken\"", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  result = NULL;
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  err = lcl_interp_error_msg(lcl_test_interp);
  ASSERT_TRUE(err != NULL && strstr(err, "cleared cell") != NULL);

  if (result) lcl_ref_dec(result);
  lcl_program_free(prog);
  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* ISSUE #37 — lcl_program_push_command failure leaks the local `cmd`'s
 * words and pieces. We inject a forced failure on the 2nd push so the 1st
 * command lands in `p->cmd` (freed by lcl_program_free) and the 2nd is
 * dropped from the failure path. Pre-fix, LSan reports a leak from the
 * 2nd cmd's word/piece allocations; post-fix, clean.
 */
static int test_issue37_push_command_leak(void) {
  /* Two commands. The 2nd has substantive words to make the leak observable. */
  const char *src = "first\nsecond foo bar baz\n";
  lcl_program *prog;

  /* Counter = 1 → 1st push succeeds, 2nd fails. */
  oom_push_command_fail_at = 1;

  prog = lcl_program_compile(src, "test.lcl");

  /* Must fail cleanly — and ASan/LSan must see no leaked allocations
   * from the dropped 2nd command. */
  ASSERT_TRUE(prog == NULL);
  ASSERT_TRUE(oom_push_command_fail_at == -1);
  return 1;
}

static int test_issue24_scanner_strndup_oom(void) {
  const char *src = "puts $foo\n";
  lcl_program *prog;

  /* The first strndup in the scan path is the one in the $name branch
   * (lcl-scan.c:486). Force it to fail. */
  oom_strndup_fail_at = 0;

  prog = lcl_program_compile(src, "test.lcl");

  /* Must fail cleanly — not crash. */
  ASSERT_TRUE(prog == NULL);
  return 1;
}

int run_test(void) {
  int total = 0;
  int passed = 0;

#define RUN(tfn) do{                                                    \
    int ok;                                                             \
    total++;                                                            \
    printf("TEST %s ...\n", #tfn);                                      \
    ok = tfn();                                                         \
    if (ok) { passed++; printf("  ok\n"); } else { printf("  FAIL\n"); } \
}while(0)

  RUN(test_simple_words);
  RUN(test_comments);
  RUN(test_braces_literal);
  RUN(test_quotes_and_subst);
  RUN(test_nested_subcmd);
  RUN(test_unmatched_brace_error);
  RUN(test_brace_backslash_balance);
  RUN(test_hex_escape_2digits);
  RUN(test_unknown_escape_literal);
  RUN(test_octal_escape);
  RUN(test_quoted_close_paren_in_list);
  RUN(test_comment_in_list_literal);

  /* Regression tests for ISSUE #22 (COW clone NULL deref) */
  RUN(test_issue22_dict_put_clone_oom);
  RUN(test_issue22_dict_del_clone_oom);
  RUN(test_issue22_list_push_clone_oom);
  RUN(test_issue22_list_set_clone_oom);

  /* Regression test for ISSUE #24 (scanner strndup NULL deref) */
  RUN(test_issue24_scanner_strndup_oom);

  /* Regression test for ISSUE #26 (hash used overcounts on tomb reuse) */
  RUN(test_issue26_hash_used_tombstone_reuse);

  /* Regression test for ISSUE #27 (FNV-1a constants / type width) */
  RUN(test_issue27_fnv1a_known_vector);

  /* Regression test for ISSUE #33 (c_catch strdup NULL not handled) */
  RUN(test_issue33_catch_strdup_oom);

  /* Regression test for ISSUE #35 (public API NULL safety) */
  RUN(test_issue35_public_api_null_safety);

  /* Regression test for ISSUE #15 (to-string NULL-on-OOM contract) */
  RUN(test_issue15_to_string_null_contract);

  /* Regression test for ISSUE #59 (lcl_eval_program cur_file UAF) */
  RUN(test_issue59_eval_program_cur_file_uaf);

  /* Regression test for ISSUE #37 (push_command failure leaks cmd) */
  RUN(test_issue37_push_command_leak);

  /* Regression test for ISSUE #39 (import NULL-derefs cleared cell) */
  RUN(test_issue39_import_cleared_cell);

  /* Regression test for ISSUE #40 (namespace max-depth leak) */
  RUN(test_issue40_namespace_max_depth_leak);

  /* Regression test for ISSUE #41 (namespace re-entry OOM) */
  RUN(test_issue41_namespace_reentry_oom);

  /* Regression test for ISSUE #42 (dict clone swallows put OOM) */
  RUN(test_issue42_dict_clone_put_oom);

  /* Regression test for ISSUE #45 (skip_balanced stack overflow) */
  RUN(test_issue45_skip_balanced_depth_limit);

  /* Regression test for ISSUE #47 (param bind ignores OOM) */
  RUN(test_issue47_param_bind_oom);

  /* Regression test for ISSUE #58 (cleared-cell deref via $name) */
  RUN(test_issue58_cleared_cell_returns_error);

  printf("\n%d/%d tests passed\n", passed, total);
  return (passed == total) ? 0 : 1;
}

/* Shared interpreter for tests that need to invoke registered commands. */
lcl_interp *lcl_test_interp = NULL;

#ifdef LCL_TEST
int main(void) {
  int result;

  /* Unbuffered so per-test status survives an ASan/UBSan abort during
   * regression tests that intentionally trigger NULL derefs on buggy code. */
  setvbuf(stdout, NULL, _IONBF, 0);

  lcl_test_interp = lcl_interp_new();
  lcl_register_core(lcl_test_interp);
  result = run_test();
  lcl_interp_free(lcl_test_interp);
  return result;
}
#endif
