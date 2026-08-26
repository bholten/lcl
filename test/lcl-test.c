/* For mkdtemp (require-contract tests). */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hash-table.h"
#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-path.h"
#include "lcl-stdlib.h"
#include "lcl-values.h"
#include "lcl-version.h"

/* These three are declared in the public header <lcl.h>, which we
 * cannot include here because the internal headers above re-declare
 * the same types and enums (lcl_result, lcl_type, LCL_OK, ...). For
 * now just extern-forward what the test needs. */
extern void lcl_clear_error(lcl_interp *interp);
extern lcl_type lcl_value_type_of(const lcl_value *value);
extern lcl_result lcl_dict_keys(const lcl_value *dict, lcl_value **out);
extern void lcl_set_step_hook(lcl_interp *interp,
                              int (*fn)(lcl_interp *, void *), void *userdata,
                              unsigned long interval);

/* ---------------------------------------------------------------------------
 * OOM injection (linked via -Wl,--wrap=calloc,--wrap=strndup).
 * Set the counter to N to make the (N+1)th call return NULL; subsequent
 * calls work normally. Set to -1 (default) to disable.
 * ---------------------------------------------------------------------------
 */

extern void *__real_calloc(size_t nmemb, size_t size);
extern char *__real_strndup(const char *s, size_t n);
extern char *__real_strdup(const char *s);
extern int __real_lcl_program_push_command(lcl_program *p, lcl_command *src);
extern int __real_hash_table_put(hash_table *ht, const char *key,
                                 lcl_value *value);

static int oom_calloc_fail_at = -1; /* -1: disabled */
static int oom_strndup_fail_at = -1;
static int oom_strdup_fail_at = -1;
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
  char *s;
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

  if (cap == b->cap) {
    return 1;
  }

  p = (char *)realloc(b->s, cap);

  if (!p) {
    return 0;
  }

  b->s = p;
  b->cap = cap;

  return 1;
}

static int sb_putc(SB *b, int c) {
  if (!sb_reserve(b, b->len + 2)) {
    return 0;
  }

  b->s[b->len++] = (char)c;
  b->s[b->len] = '\0';

  return 1;
}

static int sb_puts(SB *b, const char *s) {
  size_t n = strlen(s);

  if (!sb_reserve(b, b->len + n + 1)) {
    return 0;
  }

  memcpy(b->s + b->len, s, n + 1);
  b->len += n;

  return 1;
}

static void sb_free(SB *b) {
  free(b->s);
  b->s = NULL;
  b->len = b->cap = 0;
}

/** Dump IL **/

static int dump_esc_lit(SB *b, const char *s, size_t n) {
  size_t i;
  if (!sb_puts(b, "\"")) {
    return 0;
  }

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

      if (!sb_puts(b, tmp)) {
        return 0;
      }
    } else {
      if (!sb_putc(b, c)) {
        return 0;
      }
    }
  }

  return sb_puts(b, "\"");
}

static int dump_program_rec(const lcl_program *P, SB *b, int depth);

static int dump_word(const lcl_word *w, SB *b, int depth) {
  int i;
  for (i = 0; i < w->np; i++) {
    const lcl_word_piece *pc = &w->wp[i];

    if (!sb_puts(b, "[")) {
      return 0;
    }

    if (pc->kind == LCL_WP_LIT) {
      if (!sb_puts(b, "lit:")) {
        return 0;
      }
      if (!dump_esc_lit(b, pc->as.lit.s, pc->as.lit.n)) {
        return 0;
      }
    } else if (pc->kind == LCL_WP_VAR) {
      if (!sb_puts(b, "var:")) {
        return 0;
      }
      if (!dump_esc_lit(b, pc->as.var.name, strlen(pc->as.var.name))) {
        return 0;
      }
    } else { /* LCL_WP_SUBCMD */
      if (!sb_puts(b, "sub:{")) {
        return 0;
      }
      if (!dump_program_rec(pc->as.sub.program, b, depth + 1)) {
        return 0;
      }
      if (!sb_puts(b, "}")) {
        return 0;
      }
    }

    if (!sb_puts(b, "]")) {
      return 0;
    }
    if (i + 1 < w->np) {
      if (!sb_puts(b, " ")) {
        return 0;
      }
    }
  }
  return 1;
}

static int dump_command(const lcl_command *c, SB *b, int depth) {
  int i;
  if (!sb_puts(b, "cmd(")) {
    return 0;
  }
  {
    char tmp[32];
    sprintf(tmp, "line=%d", c->line);
    if (!sb_puts(b, tmp)) {
      return 0;
    }
  }
  if (!sb_puts(b, "):")) {
    return 0;
  }

  for (i = 0; i < c->argc; i++) {
    if (!sb_puts(b, " ")) {
      return 0;
    }
    if (!dump_word(&c->w[i], b, depth)) {
      return 0;
    }
    if (i + 1 < c->argc) { /* optional extra space between words if you want */
    }
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
  if (!P) {
    return 0;
  }

  if (!dump_program_rec(P, &b, 0)) {
    sb_free(&b);
    return 0;
  }

  *out_str = b.s;

  return 1;
}

/** Signatures **/
int lcl_program_dump(const lcl_program *P, char **out_str);
lcl_program *lcl_compile(const char *src, const char *file);
void lcl_program_free(lcl_program *P);

#define ASSERT_TRUE(cond)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("    assert failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__);    \
      return 0;                                                                \
    }                                                                          \
  } while (0)

#define ASSERT_STREQ(a, b)                                                     \
  do {                                                                         \
    const char *a_ = (a);                                                      \
    const char *b_ = (b);                                                      \
    if ((a_ == NULL) != (b_ == NULL) || (a_ && strcmp(a_, b_) != 0)) {         \
      printf("    assert failed: strings not equal\n    A: %s\n    B: %s\n",   \
             a_ ? a_ : "(null)", b_ ? b_ : "(null)");                          \
      return 0;                                                                \
    }                                                                          \
  } while (0)

static int compile_and_dump(const char *src, const char *expect) {
  lcl_program *P = lcl_program_compile(src, "test.lcl");
  char *got = NULL;
  int ok;

  if (!P && expect == NULL) {
    return 1;
  }

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
  const char *src = "set x 10 ; puts $x\n";
  const char *exp = "cmd(line=1): [lit:\"set\"] [lit:\"x\"] [lit:\"10\"] ; "
                    "cmd(line=1): [lit:\"puts\"] [var:\"x\"]";
  return compile_and_dump(src, exp);
}

static int test_comments(void) {
  const char *src = ";; leading comment\n"
                    "set a 1  ;; trailing comment on same line\n"
                    "puts $a\n";
  const char *exp = "cmd(line=2): [lit:\"set\"] [lit:\"a\"] [lit:\"1\"] ; "
                    "cmd(line=3): [lit:\"puts\"] [var:\"a\"]";
  return compile_and_dump(src, exp);
}

static int test_braces_literal(void) {
  const char *src = "set s {a $b [c] \\x41}\n";
  const char *exp =
      "cmd(line=1): [lit:\"set\"] [lit:\"s\"] [lit:\"a $b [c] \\\\x41\"]";
  return compile_and_dump(src, exp);
}

static int test_quotes_and_subst(void) {
  const char *src = "set s \"a $b [echo hi]\"\n";
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
  const char *exp = "cmd(line=1): [lit:\"puts\"] [lit:\"foo \\\\} bar\"]";
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
  const char *exp = "cmd(line=1): [lit:\"puts\"] [lit:\"\\\\q\"]";
  return compile_and_dump(src, exp);
}

static int test_octal_escape(void) {
  /* \101 -> 'A' (octal 101 = 65 = 0x41) */
  const char *src = "puts \"\\101\"\n";
  const char *exp = "cmd(line=1): [lit:\"puts\"] [lit:\"A\"]";
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
 *  #22 — COW clone failure causes NULL deref in dict/list mutation.
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
 * ---------------------------------------------------------------------------
 */

static int test_issue22_dict_put_clone_oom(void) {
  lcl_value *dict;
  lcl_value *original;
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
  ASSERT_TRUE(dict != NULL);     /* *io must NOT have been NULL'd */
  ASSERT_TRUE(dict == original); /* still the original */

  lcl_ref_dec(val);
  lcl_ref_dec(dict);
  lcl_ref_dec(original);
  return 1;
}

static int test_issue22_dict_del_clone_oom(void) {
  lcl_value *dict;
  lcl_value *original;
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
  lcl_value *list;
  lcl_value *original;
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
  lcl_value *list;
  lcl_value *original;
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
 *  #35 — Public API functions inconsistently NULL-deref on NULL value
 * arguments. Embedders routinely pass NULL (e.g. an `out` from a failed
 * call); sibling functions (e.g. `lcl_list_*`, `lcl_define`, `lcl_get`)
 * already guard. This pins the contract for the remaining functions.
 * ---------------------------------------------------------------------------
 */

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

static int test_issue75_literal_grammar(void) {
  static const struct {
    const char *s;
    lcl_num_class want;
  } cases[] = {
      {"42",     LCL_NUM_INT  },
      {"0",      LCL_NUM_INT  },
      {"-3",     LCL_NUM_INT  },
      {"3.14",   LCL_NUM_FLOAT},
      {"1.50",   LCL_NUM_FLOAT},
      {"0.1",    LCL_NUM_FLOAT},
      {"-0.0",   LCL_NUM_FLOAT},
      {"1e3",    LCL_NUM_FLOAT},
      {"1E+3",   LCL_NUM_FLOAT},
      {"0e0",    LCL_NUM_FLOAT},
      {"1.5e-3", LCL_NUM_FLOAT},
      /* outside the grammar — remain strings */
      {"007",    LCL_NUM_NONE },
      {"042",    LCL_NUM_NONE },
      {"+5",     LCL_NUM_NONE },
      {"-0",     LCL_NUM_NONE },
      {".5",     LCL_NUM_NONE },
      {"5.",     LCL_NUM_NONE },
      {"1e",     LCL_NUM_NONE },
      {"1e+",    LCL_NUM_NONE },
      {"1.2.3",  LCL_NUM_NONE },
      {"0x10",   LCL_NUM_NONE },
      {"nan",    LCL_NUM_NONE },
      {"inf",    LCL_NUM_NONE },
      {"-",      LCL_NUM_NONE },
      {"",       LCL_NUM_NONE },
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    if (lcl_num_literal_classify(cases[i].s, strlen(cases[i].s)) !=
        cases[i].want) {
      printf("    classify mismatch: \"%s\"\n", cases[i].s);
      return 0;
    }
  }

  /* Embedded NUL never classifies. */
  ASSERT_TRUE(lcl_num_literal_classify("4\0002", 3) == LCL_NUM_NONE);

  return 1;
}

static int test_issue75_literal_typing(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  lcl_return_code rc;

  rc = lcl_eval_string(lcl_test_interp, "42", &out);
  ASSERT_TRUE(rc == LCL_RC_OK && out != NULL);
  ASSERT_TRUE(out->type == LCL_INT && out->as.i == 42);
  lcl_ref_dec(out);
  out = NULL;

  rc = lcl_eval_string(lcl_test_interp, "1.5", &out);
  ASSERT_TRUE(rc == LCL_RC_OK && out != NULL);
  ASSERT_TRUE(out->type == LCL_FLOAT && out->as.f == 1.5);
  lcl_ref_dec(out);
  out = NULL;

  /* Quoted, braced, and non-grammar spellings stay strings. */
  rc = lcl_eval_string(lcl_test_interp, "\"42\"", &out);
  ASSERT_TRUE(rc == LCL_RC_OK && out != NULL);
  ASSERT_TRUE(out->type == LCL_STRING);
  lcl_ref_dec(out);
  out = NULL;

  rc = lcl_eval_string(lcl_test_interp, "{42}", &out);
  ASSERT_TRUE(rc == LCL_RC_OK && out != NULL);
  ASSERT_TRUE(out->type == LCL_STRING);
  lcl_ref_dec(out);
  out = NULL;

  rc = lcl_eval_string(lcl_test_interp, "007", &out);
  ASSERT_TRUE(rc == LCL_RC_OK && out != NULL);
  ASSERT_TRUE(out->type == LCL_STRING);
  ASSERT_STREQ(lcl_value_to_string(out), "007");
  lcl_ref_dec(out);
  out = NULL;

  /* Out-of-range integer literal is a compile error, never a string. */
  rc = lcl_eval_string(lcl_test_interp, "let x 99999999999999999999", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(out == NULL);
  ASSERT_TRUE(lcl_test_interp->err_msg != NULL);
  ASSERT_STREQ(lcl_test_interp->err_msg, "integer literal out of range");
  lcl_clear_error(lcl_test_interp);

  return 1;
}

static int test_issue75_numeric_text(void) {
  static const struct {
    const char *s;
    lcl_num_class want;
  } cases[] = {
      {"42",      LCL_NUM_INT  },
      {"007",     LCL_NUM_INT  },
      {"+5",      LCL_NUM_INT  },
      {"-0",      LCL_NUM_INT  },
      {".5",      LCL_NUM_FLOAT},
      {"5.",      LCL_NUM_FLOAT},
      {"1e3",     LCL_NUM_FLOAT},
      {"1e999",   LCL_NUM_FLOAT},
      {" 42",     LCL_NUM_NONE },
      {"42 ",     LCL_NUM_NONE },
      {"0x10",    LCL_NUM_NONE },
      {"nan",     LCL_NUM_NONE },
      {"inf",     LCL_NUM_NONE },
      {"3.14abc", LCL_NUM_NONE },
      {"",        LCL_NUM_NONE },
      {"-",       LCL_NUM_NONE },
  };
  size_t i;
  long iv;
  double fv;
  const char *sv;
  lcl_value *v;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    if (lcl_num_text_classify(cases[i].s, strlen(cases[i].s)) !=
        cases[i].want) {
      printf("    text classify mismatch: \"%s\"\n", cases[i].s);
      return 0;
    }
  }

  /* to_int / to_float follow the grammar, then range-check. */
  v = lcl_string_new("007");
  ASSERT_TRUE(v && lcl_value_to_int(v, &iv) == LCL_OK && iv == 7);
  lcl_ref_dec(v);

  v = lcl_string_new(" 42");
  ASSERT_TRUE(v && lcl_value_to_int(v, &iv) == LCL_ERROR);
  ASSERT_TRUE(lcl_value_to_float(v, &fv) == LCL_ERROR);
  lcl_ref_dec(v);

  v = lcl_string_new("3.14abc");
  ASSERT_TRUE(v && lcl_value_to_float(v, &fv) == LCL_ERROR);
  lcl_ref_dec(v);

  v = lcl_string_new("1e999");
  ASSERT_TRUE(v && lcl_value_to_float(v, &fv) == LCL_ERROR);
  lcl_ref_dec(v);

  v = lcl_string_new(".5");
  ASSERT_TRUE(v && lcl_value_to_float(v, &fv) == LCL_OK && fv == 0.5);
  lcl_ref_dec(v);

  /* NaN→long guard, now only reachable from C (scripts can no longer
   * materialize NaN via `float "nan"`). */
  {
    volatile double zero = 0.0;
    v = lcl_float_new(zero / zero);
    ASSERT_TRUE(v && lcl_value_to_int(v, &iv) == LCL_ERROR);
    lcl_ref_dec(v);
  }

  /* Domain-strict getter: STRING tag only, no rendering. */
  v = lcl_string_new("hi");
  ASSERT_TRUE(v && lcl_value_get_string(v, &sv) == LCL_OK);
  ASSERT_STREQ(sv, "hi");
  lcl_ref_dec(v);

  v = lcl_int_new(42);
  ASSERT_TRUE(v && lcl_value_get_string(v, &sv) == LCL_ERROR);
  ASSERT_TRUE(lcl_value_to_string(v) != NULL); /* renderer stays total */
  lcl_ref_dec(v);

  return 1;
}

/* ---------------------------------------------------------------------------
 *  #75 step 1: floats print via round-trip-safe compact formatting —
 * the shortest of %.15g/%.16g/%.17g whose parse is bit-identical wins.
 * Raw %.17g rendered 0.1 as "0.10000000000000001".
 * ---------------------------------------------------------------------------
 */

static int test_issue75_float_compact_formatting(void) {
  static const struct {
    double in;
    const char *want;
  } cases[] = {
      {0.1,                 "0.1"                },
      {1.5,                 "1.5"                },
      {100.0,               "100"                },
      {1e20,                "1e+20"              },
      {3.141592653589793,   "3.141592653589793"  },
      /* 0.1 + 0.2 needs all 17 digits; compaction must not round away. */
      {0.30000000000000004, "0.30000000000000004"},
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    lcl_value *v = lcl_float_new(cases[i].in);

    ASSERT_TRUE(v != NULL);
    ASSERT_STREQ(lcl_value_to_string(v), cases[i].want);
    lcl_ref_dec(v);
  }

  /* Signed zero survives — the round-trip check compares bits, not ==. */
  {
    lcl_value *v = lcl_float_new(-0.0);

    ASSERT_TRUE(v != NULL);
    ASSERT_STREQ(lcl_value_to_string(v), "-0");
    lcl_ref_dec(v);
  }

  /* Whatever precision wins, the rendering must parse back exactly. */
  {
    lcl_value *v = lcl_float_new(2.2250738585072014e-308);
    double back;

    ASSERT_TRUE(v != NULL);
    ASSERT_TRUE(lcl_parse_double_c(lcl_value_to_string(v), &back) > 0);
    ASSERT_TRUE(memcmp(&back, &v->as.f, sizeof(double)) == 0);
    lcl_ref_dec(v);
  }

  return 1;
}

static int test_issue15_to_string_null_contract(void) {
  extern lcl_interp *lcl_test_interp;
  extern const char *lcl_interp_error_msg(lcl_interp * interp);
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
 *  #59 — `lcl_eval_program` borrowed `pr->file` into `interp->cur_file`
 * without restoring on exit. When the caller then freed the program, any
 * later `LCL_ERR_MSG` (which strdups `cur_file`) read freed memory. Fixed
 * by save+restore around the eval. This test deliberately mimics the
 * pattern: compile a fresh program, eval it, free it, then trigger an
 * error and rely on ASan to catch the UAF if the fix regressed.
 * ---------------------------------------------------------------------------
 */
static int test_issue59_eval_program_cur_file_uaf(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_program *prog;
  lcl_value *result = NULL;
  lcl_return_code rc;

  /* Start from a known cur_file. */
  lcl_test_interp->cur_file = NULL;

  prog = lcl_program_compile("+ 1 1", "i59.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  ASSERT_TRUE(rc == LCL_RC_OK);
  if (result) {
    lcl_ref_dec(result);
  }

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
 * Version surface: compile-time macros and runtime accessor must agree,
 * the version string must be well-formed (non-empty, no trailing
 * whitespace, "MAJOR.MINOR.PATCH" shape), and LCL_VERSION_NUMBER must
 * match the macros. Catches CMake `configure_file` drift and accidental
 * hard-codes anywhere in the version pipeline.
 * ---------------------------------------------------------------------------
 */
static int test_version_surface(void) {
  const char *vs = lcl_version();
  char expected[32];
  int n;

  /* Runtime string is non-NULL, non-empty, and matches the macro. */
  ASSERT_TRUE(vs != NULL);
  ASSERT_TRUE(vs[0] != '\0');
  ASSERT_STREQ(vs, LCL_VERSION_STRING);

  /* Macros agree with the string: reformat from the integer
   * components and compare. If `configure_file` substituted
   * MAJOR/MINOR/PATCH inconsistently with PROJECT_VERSION, this
   * trips. Use strcmp directly rather than ASSERT_STREQ — the macro's
   * NULL guard fires a `-Waddress` warning on `expected`, which the
   * compiler knows cannot decay to NULL (it's a stack array). */
  n = snprintf(expected, sizeof(expected), "%d.%d.%d", LCL_VERSION_MAJOR,
               LCL_VERSION_MINOR, LCL_VERSION_PATCH);
  ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expected));
  ASSERT_TRUE(strcmp(LCL_VERSION_STRING, expected) == 0);

  /* MINOR and PATCH stay in 0..99 — invariant of LCL_VERSION_NUMBER. */
  ASSERT_TRUE(LCL_VERSION_MAJOR >= 0);
  ASSERT_TRUE(LCL_VERSION_MINOR >= 0 && LCL_VERSION_MINOR < 100);
  ASSERT_TRUE(LCL_VERSION_PATCH >= 0 && LCL_VERSION_PATCH < 100);

  /* LCL_VERSION_NUMBER decomposes correctly. */
  ASSERT_TRUE(LCL_VERSION_NUMBER == LCL_VERSION_MAJOR * 10000 +
                                        LCL_VERSION_MINOR * 100 +
                                        LCL_VERSION_PATCH);

  return 1;
}

/* ---------------------------------------------------------------------------
 *  #33 — `c_catch` doesn't NULL-check `strdup` of its result/error
 * var-name arguments. On OOM, the strdup of `result_var` returns NULL and
 * the function silently completes without binding the user's variable —
 * a confusing failure where downstream `$result_var` lookups error as
 * "unbound name" with no indication of the actual cause. The fix: detect
 * the strdup failure and return LCL_RC_ERR with a clear message.
 * ---------------------------------------------------------------------------
 */

static int test_issue33_catch_strdup_oom(void) {
  /* The lcl-test binary uses one global interp set up by main(). Compile
   * a fresh program here and target the specific strdup of the result-var
   * name with `oom_strdup_fail_match`. */
  extern lcl_interp *lcl_test_interp; /* set by main */
  lcl_program *prog;
  lcl_value *result = NULL;
  lcl_return_code rc;

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
 *  #27 — FNV-1a uses a corrupted offset basis and a not-portably-wide
 * integer type. The canonical 64-bit FNV-1a offset basis is
 * 0xCBF29CE484222325 (14695981039346656037); the buggy code dropped a
 * digit (1469598103934665603). This test computes the hash of "foo"
 * against the standard FNV-1a expected value 0xDCB27518FED9D577.
 * ---------------------------------------------------------------------------
 */

static int test_issue27_fnv1a_known_vector(void) {
  /* Standard FNV-1a 64-bit hashes
   * (https://datatracker.ietf.org/doc/html/draft-eastlake-fnv) */
  ASSERT_TRUE(lcl_hash_fnv1a("foo") == LCL_U64_C(0xDCB27518FED9D577));
  ASSERT_TRUE(lcl_hash_fnv1a("a") == LCL_U64_C(0xAF63DC4C8601EC8C));
  return 1;
}

/* ---------------------------------------------------------------------------
 *  #26 — hash_table_put over-increments `used` on tombstone reuse.
 *
 * Insert→delete→insert of the same key should leave used == len == 1.
 * Buggy code unconditionally does `used++` regardless of whether the
 * chosen slot was H_EMPTY or H_TOMB, so the second insert produces
 * used == 2 (the tombstone already counted toward used).
 * ---------------------------------------------------------------------------
 */

static int test_issue26_hash_used_tombstone_reuse(void) {
  hash_table *ht = hash_table_new();
  lcl_value *v = lcl_string_new("x");

  ASSERT_TRUE(ht != NULL);
  ASSERT_TRUE(v != NULL);

  ASSERT_TRUE(hash_table_put(ht, "a", v));
  ASSERT_TRUE(ht->used == 1);
  ASSERT_TRUE(ht->len == 1);

  ASSERT_TRUE(hash_table_delete(ht, "a"));
  ASSERT_TRUE(ht->used == 1); /* tomb still counted */
  ASSERT_TRUE(ht->len == 0);

  /* Reuse the tombstone slot. */
  ASSERT_TRUE(hash_table_put(ht, "a", v));
  ASSERT_TRUE(ht->used == 1); /* MUST NOT double-count */
  ASSERT_TRUE(ht->len == 1);

  hash_table_free(ht);
  lcl_ref_dec(v);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Scanner $name branch dereferences NULL strndup result.
 *
 * Reproduction: compile a script containing `$name` (the non-`::`-suffix
 * branch in lcl-scan.c) with strndup forced to return NULL.
 *
 * Buggy behavior: lcl_word_add_var(NULL) → strlen(NULL) → segfault.
 * Fixed behavior: lcl_program_compile returns NULL cleanly.
 * ---------------------------------------------------------------------------
 */

/* Param/upvalue binding ignores OOM. We invoke a lambda
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
  extern const char *lcl_interp_error_msg(lcl_interp * interp);
  lcl_program *prog;
  lcl_value *result = NULL;
  const char *err;
  lcl_return_code rc;

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

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_program_free(prog);
  lcl_clear_error(lcl_test_interp);
  return 1;
}

/*  #45 — `skip_balanced` recurses on alternating `(`/`[`. A
 * crafted `[([([(... )])])]` of depth N exhausts the C stack. We build
 * the worst case at depth 300 and expect `lcl_program_compile` to
 * return NULL cleanly (parse error). Pre-fix: ASan SEGV from stack
 * overflow. */
static int test_issue45_skip_balanced_depth_limit(void) {
  enum { DEPTH = 20000 };
  size_t total = (size_t)DEPTH * 2 * 2; /* DEPTH "([" + DEPTH "])" */
  char *src = (char *)malloc(total + 1);
  size_t i;
  lcl_program *prog;

  ASSERT_TRUE(src != NULL);
  for (i = 0; i < DEPTH; i++) {
    src[i * 2] = '[';
    src[i * 2 + 1] = '(';
  }
  for (i = 0; i < DEPTH; i++) {
    src[(size_t)DEPTH * 2 + i * 2] = ')';
    src[(size_t)DEPTH * 2 + i * 2 + 1] = ']';
  }
  src[total] = '\0';

  prog = lcl_program_compile(src, "test.lcl");

  /* Either NULL (parse error from depth limit) or non-NULL (somehow
   * succeeded). What we MUST NOT see is a crash. */
  ASSERT_TRUE(prog == NULL);

  if (prog) {
    lcl_program_free(prog);
  }
  free(src);
  return 1;
}

/*  compile-time same-type nesting cap; skip_balanced cap only bounds
 * alternating nesting within one scan; every `[...]`, `(...)`,
 * `#{...}` (and braced-body precompile) level restarts compilation
 * with a fresh scanner, so `[[[...` of depth 10k+ overflowed the C
 * stack through the compile recursion. The nesting level is now
 * threaded through lcl_program_compile_depth and capped at
 * LCL_SCAN_MAX_NEST: deep subprogram nesting is a clean "nesting too
 * deep" compile error. Deep *brace* nesting stays a valid string
 * literal — only its eager precompile is skipped. */
static char *make_same_type_nest(const char *open, char close, int depth) {
  size_t olen = strlen(open);
  size_t total = (olen + 1) * (size_t)depth + 1;
  char *src = (char *)malloc(total + 1);
  int i;
  char *p;

  if (!src) {
    return NULL;
  }

  p = src;

  for (i = 0; i < depth; i++) {
    memcpy(p, open, olen);
    p += olen;
  }

  *p++ = 'a';

  for (i = 0; i < depth; i++) {
    *p++ = close;
  }

  *p = '\0';

  return src;
}

static int test_issue71_compile_nesting_cap(void) {
  enum { DEPTH = 2000 };
  static const char *opens[] = {"[", "(", "#{"};
  static const char closes[] = {']', ')', '}'};
  size_t k;

  for (k = 0; k < sizeof(opens) / sizeof(opens[0]); k++) {
    char *src = make_same_type_nest(opens[k], closes[k], DEPTH);
    lcl_compile_err cerr;
    lcl_program *prog;

    ASSERT_TRUE(src != NULL);
    prog = lcl_program_compile_ex(src, "test.lcl", &cerr);
    free(src);

    /* Pre-fix: ASan stack overflow. */
    ASSERT_TRUE(prog == NULL);
    ASSERT_TRUE(cerr.msg != NULL);
    ASSERT_TRUE(strcmp(cerr.msg, "nesting too deep") == 0);
  }

  /* Deep braces are literal data, not a subprogram: still valid. */
  {
    char *src = make_same_type_nest("{", '}', DEPTH);
    lcl_program *prog;

    ASSERT_TRUE(src != NULL);
    prog = lcl_program_compile(src, "test.lcl");
    free(src);
    ASSERT_TRUE(prog != NULL);
    lcl_program_free(prog);
  }

  return 1;
}

/*  eval step hook (lcl_set_step_hook). A hook returning nonzero
 * aborts evaluation with a sticky error: `while {1} {}` terminates,
 * `catch` cannot trap the abort, and the next top-level eval on the
 * same interp starts fresh. */
static int step_hook_fires = 0;

static int step_hook_abort(lcl_interp *interp, void *userdata) {
  (void)interp;
  (void)userdata;
  step_hook_fires++;
  return 1;
}

static int test_issue71_step_hook_budget(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_program *prog;
  lcl_value *result = NULL;
  lcl_return_code rc;

  lcl_set_step_hook(lcl_test_interp, step_hook_abort, NULL, 1000);
  step_hook_fires = 0;

  prog = lcl_program_compile(
      "var __i71 0\nwhile {1} { set! __i71 [+ $__i71 1] }", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  lcl_program_free(prog);

  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(step_hook_fires == 1); /* sticky: fires once, not per cmd */
  ASSERT_TRUE(lcl_test_interp->err_msg != NULL);
  ASSERT_STREQ(lcl_test_interp->err_msg, "evaluation aborted by host");

  if (result) {
    lcl_ref_dec(result);
    result = NULL;
  }

  lcl_clear_error(lcl_test_interp);

  /* `catch` must not shield the script from the abort. */
  step_hook_fires = 0;
  prog = lcl_program_compile("while {1} { catch { let __x71 1 } }", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  lcl_program_free(prog);

  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(step_hook_fires == 1);

  if (result) {
    lcl_ref_dec(result);
    result = NULL;
  }

  lcl_clear_error(lcl_test_interp);

  /* The abort is per-eval: a fresh top-level eval runs normally
   * (budget reset), and removing the hook restores unlimited eval. */
  prog = lcl_program_compile("+ 2 3", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  lcl_program_free(prog);

  ASSERT_TRUE(rc == LCL_RC_OK);
  ASSERT_TRUE(result != NULL);
  lcl_ref_dec(result);
  result = NULL;

  lcl_set_step_hook(lcl_test_interp, NULL, NULL, 0);
  return 1;
}

/*  the step budget must tick per loop iteration, not just per command
 * dispatched. A `while`/`for` whose test is an unbraced
 * (constant-truthy) word and whose body compiles to zero commands
 * executes no commands at all, so without the per-iteration tick in
 * s_while/s_for the loop spins forever in C, outside the budget. */
static int test_issue99_step_budget_empty_body_loops(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_program *prog;
  lcl_value *result = NULL;
  lcl_return_code rc;

  lcl_set_step_hook(lcl_test_interp, step_hook_abort, NULL, 1000);

  /* while: unbraced truthy test + empty body. */
  step_hook_fires = 0;
  prog = lcl_program_compile("while x {}", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  lcl_program_free(prog);

  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(step_hook_fires == 1);
  ASSERT_TRUE(lcl_test_interp->err_msg != NULL);
  ASSERT_STREQ(lcl_test_interp->err_msg, "evaluation aborted by host");

  if (result) {
    lcl_ref_dec(result);
    result = NULL;
  }

  lcl_clear_error(lcl_test_interp);

  /* for: empty start/next/body, unbraced truthy test. */
  step_hook_fires = 0;
  prog = lcl_program_compile("for {} x {} {}", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  lcl_program_free(prog);

  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(step_hook_fires == 1);

  if (result) {
    lcl_ref_dec(result);
    result = NULL;
  }

  lcl_clear_error(lcl_test_interp);

  /* An empty braced test is falsy (empty string) — the loop must
   * still exit normally, well under budget. */
  step_hook_fires = 0;
  prog = lcl_program_compile("while {} {}", "test.lcl");
  ASSERT_TRUE(prog != NULL);
  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  lcl_program_free(prog);

  ASSERT_TRUE(rc == LCL_RC_OK);
  ASSERT_TRUE(step_hook_fires == 0);

  if (result) {
    lcl_ref_dec(result);
    result = NULL;
  }

  lcl_set_step_hook(lcl_test_interp, NULL, NULL, 0);
  return 1;
}

/*  a word consisting of only the spread prefix (`@` followed by a
 * separator, `]`, or EOF) used to scan as an empty word, which the
 * command parser reads as end-of-input: everything after the bare `@`
 * was silently discarded from the compiled program. It must be a
 * compile error instead. */
static int test_issue98_bare_spread_is_compile_error(void) {
  static const struct {
    const char *src;
    long line;
  } bad[] = {
      {"puts hi\n@\nputs bye\n",   2}, /* bare @ command head */
      {"puts hi\n@ x\nputs bye\n", 2}, /* bare @ with trailing words */
      {"puts @ 1\n",               1}, /* bare @ in argument position */
      {"puts [list @]\n",          1}, /* bare @ before ']' */
      {"puts @",                   1}, /* bare @ at EOF */
  };
  static const char *good[] = {
      "apply $f @$args\n", /* spread of a variable */
      "puts @(1 2)\n",     /* spread of a list literal */
      "puts @[list 1]\n",  /* spread of a subcommand */
      "puts @{a b}\n",     /* spread of a braced literal */
      "puts @\"\"\n",      /* explicit empty word — eval's business */
      "puts a@b\n@ok\n",   /* mid-word '@' stays literal; @word compiles */
  };
  size_t i;

  for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    lcl_compile_err err;
    lcl_program *prog = lcl_program_compile_ex(bad[i].src, "t.lcl", &err);

    ASSERT_TRUE(prog == NULL);
    ASSERT_TRUE(err.msg != NULL);
    ASSERT_STREQ(err.msg, "expected a word after '@'");
    ASSERT_TRUE(err.line == bad[i].line);
  }

  for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
    lcl_compile_err err;
    lcl_program *prog = lcl_program_compile_ex(good[i], "t.lcl", &err);

    ASSERT_TRUE(prog != NULL);
    lcl_program_free(prog);
  }

  return 1;
}

/* Scanner-unification regressions: list/dict literals compile by
 * byte length (an embedded NUL used to strlen-truncate the desugared
 * `list ...` program — the #84 wrinkle), and the exported span
 * helpers reject unterminated spans. */
static int test_sub_literal_scan_unification(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  lcl_return_code rc;

  /* "len (a \0 b)" — the NUL byte is an ordinary 1-byte word, so the
   * list has three elements. Pre-fix the program truncated at the
   * NUL and compiled `list a `. */
  {
    static const char src[] = "len (a \0 b)";

    rc = lcl_eval_bytes(lcl_test_interp, src, sizeof(src) - 1, &out);
    ASSERT_TRUE(rc == LCL_RC_OK);
    ASSERT_TRUE(out != NULL);
    ASSERT_TRUE(out->type == LCL_INT);
    ASSERT_TRUE(out->as.i == 3);
    lcl_ref_dec(out);
    out = NULL;
  }

  /* Span helpers: boundary conventions and failure on unterminated. */
  {
    static const char buf[] = "a \"]\" ] x"; /* after '[': a "]" ] x */
    size_t end = 0;

    ASSERT_TRUE(lcl_scan_skip_balanced_span(buf, sizeof(buf) - 1, 0, '[', ']',
                                            &end) == 0);
    ASSERT_TRUE(buf[end - 1] == ']');
    ASSERT_TRUE(end == 7); /* the ']' inside quotes is not a boundary */

    ASSERT_TRUE(lcl_scan_skip_balanced_span("no close", 8, 0, '[', ']', &end) !=
                0);
    ASSERT_TRUE(lcl_scan_skip_braces_span("no close", 8, 0, &end) != 0);
  }

  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* Delimiter spans compile with file-absolute line numbers
 * (lcl_program_compile_span): both compile and runtime errors inside
 * a multiline `[...]`/`(...)`/`#{...}` name the real line, not the
 * line the span opened on. */
static int test_span_line_attribution(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  lcl_return_code rc;

  /* Compile error on line 3 (bare `@` inside the bracket span). */
  {
    lcl_compile_err err;
    lcl_program *prog =
        lcl_program_compile_ex("let a 1\nlet x [+ 1\n@\n]\n", "t.lcl", &err);

    ASSERT_TRUE(prog == NULL);
    ASSERT_TRUE(err.line == 3);
  }

  /* Runtime error on line 3 (unknown command inside the span). */
  rc = lcl_eval_string(lcl_test_interp,
                       "let a 1\nlet y [\n  span_no_such_cmd\n]\n", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(out == NULL);
  ASSERT_TRUE(lcl_test_interp->err_line == 3);

  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* Braced bodies compile in the enclosing file at the line their `{`
 * opened on (lcl_program_compile_at), so a runtime error inside a
 * proc / if / while body reports the real file and an absolute line,
 * not "<braced>:N" relative to the body. Runtime text (a non-braced
 * body, `eval`, a macro template) has no file line: its program is
 * named for the construction site ("<eval at f.lcl:3>") and keeps
 * body-relative lines. */
static int test_body_line_attribution(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  lcl_return_code rc;

  /* proc body -> nested if body: error on file line 5. */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "proc f {x} {\n"
                            "  let a 1\n"
                            "  if [> $x 0] {\n"
                            "    let b 2\n"
                            "    body_no_such_cmd\n"
                            "  }\n"
                            "}\n"
                            "f 1\n",
                            "body.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(out == NULL);
  ASSERT_STREQ(lcl_test_interp->err_file, "body.lcl");
  ASSERT_TRUE(lcl_test_interp->err_line == 5);
  lcl_clear_error(lcl_test_interp);

  /* lambda body called later: still the definition site. */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "let g [lambda {} {\n"
                            "  lambda_no_such_cmd\n"
                            "}]\n"
                            "apply $g\n",
                            "lam.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "lam.lcl");
  ASSERT_TRUE(lcl_test_interp->err_line == 2);
  lcl_clear_error(lcl_test_interp);

  /* while body inside a namespace builder: three brace levels deep. */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "namespace N {\n"
                            "  proc f {} {\n"
                            "    while {1} {\n"
                            "      while_no_such_cmd\n"
                            "    }\n"
                            "  }\n"
                            "}\n"
                            "N::f\n",
                            "ns.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "ns.lcl");
  ASSERT_TRUE(lcl_test_interp->err_line == 4);
  lcl_clear_error(lcl_test_interp);

  /* Runtime text: named for the construction site, relative line. */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "let a 1\n"
                            "eval \"let b 2\\neval_no_such_cmd\"\n",
                            "ev.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "<eval at ev.lcl:2>");
  ASSERT_TRUE(lcl_test_interp->err_line == 2);
  lcl_clear_error(lcl_test_interp);

  /* Non-braced proc body (text held in a variable): same rule. */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "let body \"let c 3\\ndyn_no_such_cmd\"\n"
                            "\n"
                            "proc h {} $body\n"
                            "h\n",
                            "dyn.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "<lambda at dyn.lcl:3>");
  ASSERT_TRUE(lcl_test_interp->err_line == 2);
  lcl_clear_error(lcl_test_interp);

  /* eval'd text calling a proc that fails deeper: the innermost site
   * wins (eval/load/require used to overwrite it with their own). */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "proc deep {} {\n"
                            "  let a 1\n"
                            "  deep_no_such_cmd\n"
                            "}\n"
                            "eval \"deep\"\n",
                            "clob.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "clob.lcl");
  ASSERT_TRUE(lcl_test_interp->err_line == 3);
  lcl_clear_error(lcl_test_interp);

  return 1;
}

/* Proc::origin is the file and line of the command that constructed
 * the proc -- a source location, not an identity: it follows the
 * value (`let g $f`), a lambda records its own site, and text run
 * through `eval` reports the eval's name so definitions from
 * different sources stay distinguishable. */
/* `eval_at` names its program for the given file and base line, so
 * an error inside it reports that provenance. A plain `eval` nested
 * in it is named for the eval_at source. */
static int test_eval_at_error_context(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  lcl_return_code rc;

  rc = lcl_eval_string_file(lcl_test_interp,
                            "eval_at {\n"
                            "  let a 1\n"
                            "  eval_at_no_such_cmd\n"
                            "} \"buf\" 10\n",
                            "host.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(out == NULL);
  ASSERT_STREQ(lcl_test_interp->err_file, "buf");
  ASSERT_TRUE(lcl_test_interp->err_line == 12);
  lcl_clear_error(lcl_test_interp);

  rc = lcl_eval_string_file(lcl_test_interp,
                            "eval_at {eval {eval_at_no_such_cmd}} \"buf\" 3\n",
                            "host.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "<eval at buf:3>");
  ASSERT_TRUE(lcl_test_interp->err_line == 1);
  lcl_clear_error(lcl_test_interp);

  /* A compile error in the text is reported at the base-offset line. */
  rc = lcl_eval_string_file(lcl_test_interp,
                            "eval_at {let a 1\n[} \"buf\" 20\n", "host.lcl",
                            &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_STREQ(lcl_test_interp->err_file, "buf");
  ASSERT_TRUE(lcl_test_interp->err_line == 21);
  lcl_clear_error(lcl_test_interp);

  return 1;
}

static int test_proc_origin(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  const char *s;
  lcl_return_code rc;

  rc = lcl_eval_string_file(lcl_test_interp,
                            "proc f {} {}\n"
                            "let g $f\n"
                            "let l [lambda {} {}]\n"
                            "eval \"proc e {} {}\"\n"
                            "String::join ("
                            "[get [Proc::origin f] file] "
                            "[get [Proc::origin g] line] "
                            "[get [Proc::origin $l] line] "
                            "[get [Proc::origin e] file] "
                            "[get [Proc::origin e] line] "
                            "[len [Proc::origin {+}]]) \"|\"\n",
                            "org.lcl", &out);
  ASSERT_TRUE(rc == LCL_RC_OK);
  ASSERT_TRUE(out != NULL);
  ASSERT_TRUE(lcl_value_to_cstring(lcl_test_interp, out, &s) == LCL_OK);
  ASSERT_STREQ(s, "org.lcl|1|3|<eval at org.lcl:4>|1|0");
  lcl_ref_dec(out);

  return 1;
}

/* lcl_set_error must copy — the idiomatic extension shape is "format
 * into a stack buffer, set error, return", so a borrowed pointer
 * dangles by the time the host reads the message. */
static int test_issue102_set_error_copies(void) {
  extern lcl_interp *lcl_test_interp;
  extern void lcl_set_error(lcl_interp * interp, const char *msg);
  extern const char *lcl_interp_error_msg(lcl_interp * interp);
  char buf[64];

  strcpy(buf, "arg 2: expected int");
  lcl_set_error(lcl_test_interp, buf);
  memset(buf, 'X', sizeof(buf) - 1); /* clobber the caller's buffer */
  buf[sizeof(buf) - 1] = '\0';

  ASSERT_STREQ(lcl_interp_error_msg(lcl_test_interp), "arg 2: expected int");

  lcl_clear_error(lcl_test_interp);
  ASSERT_TRUE(lcl_interp_error_msg(lcl_test_interp) == NULL);
  return 1;
}

/* lcl_ns_def borrows (namespace takes its own +1); lcl_ns_def_take
 * consumes the caller's reference. */
static int test_issue102_ns_def_ownership(void) {
  lcl_value *ns = lcl_ns_new("t");
  lcl_value *a = lcl_int_new(1);
  lcl_value *b = lcl_int_new(2);
  lcl_value *got = NULL;

  ASSERT_TRUE(a->refc == 1);
  ASSERT_TRUE(lcl_ns_def(ns, "a", a) == LCL_OK);
  ASSERT_TRUE(a->refc == 2); /* ns holds one, caller still holds one */
  lcl_ref_dec(a);            /* caller's */

  ASSERT_TRUE(lcl_ns_def_take(ns, "b", b) == LCL_OK);
  ASSERT_TRUE(b->refc == 1); /* only the ns holds it now */

  ASSERT_TRUE(lcl_ns_get(ns, "b", &got) == LCL_OK);
  ASSERT_TRUE(got == b && b->refc == 2);
  lcl_ref_dec(got);

  /* Non-namespace target: def fails, but _take still consumes. */
  {
    lcl_value *not_ns = lcl_int_new(0);
    lcl_value *c = lcl_int_new(3);

    ASSERT_TRUE(lcl_ns_def_take(not_ns, "c", c) == LCL_ERROR);
    lcl_ref_dec(not_ns);
  }

  lcl_ref_dec(ns);
  return 1;
}

/* The _peek accessors hand back the container's own reference:
 * same pointer as _get, refcount untouched, NULL for absent /
 * out-of-range / wrong type. */
static int test_peek_accessors_borrow(void) {
  lcl_value *l = lcl_list_new();
  lcl_value *d = lcl_dict_new();
  lcl_value *ns = lcl_ns_new("pk");
  lcl_value *a = lcl_int_new(1);
  lcl_value *cell;
  lcl_value *got = NULL;

  ASSERT_TRUE(lcl_list_push(&l, a) == LCL_OK);
  ASSERT_TRUE(lcl_dict_put(&d, "k", a) == LCL_OK);
  ASSERT_TRUE(lcl_ns_def(ns, "m", a) == LCL_OK);
  cell = lcl_cell_new(a);
  ASSERT_TRUE(cell != NULL);
  ASSERT_TRUE(a->refc == 5); /* caller + list + dict + ns + cell */

  ASSERT_TRUE(lcl_list_peek(l, 0) == a && a->refc == 5);
  ASSERT_TRUE(lcl_dict_peek(d, "k") == a && a->refc == 5);
  ASSERT_TRUE(lcl_ns_peek(ns, "m") == a && a->refc == 5);
  ASSERT_TRUE(lcl_cell_peek(cell) == a && a->refc == 5);

  /* Agrees with the owning accessor, which does take +1. */
  ASSERT_TRUE(lcl_dict_get(d, "k", &got) == LCL_OK);
  ASSERT_TRUE(got == lcl_dict_peek(d, "k") && a->refc == 6);
  lcl_ref_dec(got);
  ASSERT_TRUE(a->refc == 5);

  /* Absent / out of range / wrong type. */
  ASSERT_TRUE(lcl_list_peek(l, 1) == NULL);
  ASSERT_TRUE(lcl_dict_peek(d, "nope") == NULL);
  ASSERT_TRUE(lcl_ns_peek(ns, "nope") == NULL);
  ASSERT_TRUE(lcl_list_peek(d, 0) == NULL);
  ASSERT_TRUE(lcl_dict_peek(l, "k") == NULL);
  ASSERT_TRUE(lcl_cell_peek(a) == NULL);
  ASSERT_TRUE(lcl_ns_peek(d, "m") == NULL);
  ASSERT_TRUE(lcl_list_peek(NULL, 0) == NULL);
  ASSERT_TRUE(lcl_dict_peek(NULL, "k") == NULL);
  ASSERT_TRUE(lcl_cell_peek(NULL) == NULL);
  ASSERT_TRUE(lcl_ns_peek(NULL, "m") == NULL);

  lcl_ref_dec(cell);
  lcl_ref_dec(ns);
  lcl_ref_dec(d);
  lcl_ref_dec(l);
  ASSERT_TRUE(a->refc == 1);
  lcl_ref_dec(a);
  return 1;
}

/* `lcl_dict_clone_shallow` ignores `hash_table_put` return.  On OOM
 * the clone is silently truncated. We force the first put inside the
 * clone iteration to fail, trigger COW on a shared dict, and verify:
 * (a) lcl_dict_put returns LCL_ERROR, (b) the caller's dict pointer
 * is unchanged, and (c) LSan sees no leak from the partial clone. */
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
    lcl_ref_dec(v1); /* dict_put didn't take ownership */
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
    lcl_ref_dec(v3); /* dict_put doesn't take ownership */
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

/*  #41 — `s_namespace`'s re-entry pre-population loop does not
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
  lcl_return_code rc;

  setup = lcl_program_compile("namespace __i41_ns { let x 1 }", "test.lcl");
  ASSERT_TRUE(setup != NULL);
  rc = lcl_eval_program(lcl_test_interp, setup, &result);
  ASSERT_TRUE(rc == LCL_RC_OK);
  if (result) {
    lcl_ref_dec(result);
    result = NULL;
  }
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

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_program_free(reentry);
  return 1;
}

/*  #40 — `s_namespace` discards the +1 ref returned by
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
  lcl_return_code rc;

  lcl_test_interp->max_depth = saved_depth + 1;

  prog = lcl_program_compile("namespace __i40_ns { let x 1 }", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);

  lcl_test_interp->max_depth = saved_max_depth;

  ASSERT_TRUE(rc == LCL_RC_ERR);

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_program_free(prog);
  return 1;
}

/*  #39 — s_import dereferences `as.cell.inner` without NULL-check.
 * lcl_frame_clear / lcl_frame_free's cycle-breaker NULLs cell.inner for
 * lambda-captured cells; if `import` is then called on such a cell, the
 * type-check (line ~2608) segfaults on NULL. We simulate the cleared-cell
 * state directly (`lcl_cell_new(NULL)`) and verify `import` errors cleanly. */
static int test_issue39_import_cleared_cell(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *cell;
  lcl_program *prog;
  lcl_value *result = NULL;
  lcl_return_code rc;

  cell = lcl_cell_new(NULL);
  ASSERT_TRUE(cell != NULL);
  ASSERT_TRUE(cell->as.cell.inner == NULL);

  /* Bind the broken cell so `import __i39_broken` resolves to it. */
  ASSERT_TRUE(lcl_env_let(&lcl_test_interp->env, "__i39_broken", cell) ==
              LCL_OK);
  lcl_ref_dec(cell);

  prog = lcl_program_compile("import __i39_broken", "test.lcl");
  ASSERT_TRUE(prog != NULL);

  rc = lcl_eval_program(lcl_test_interp, prog, &result);
  /* Pre-fix: segfault. Post-fix: clean LCL_RC_ERR. */
  ASSERT_TRUE(rc == LCL_RC_ERR);

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_program_free(prog);
  return 1;
}

/*  #58 — `lcl_eval_word` returned LCL_RC_OK with `*out == NULL` when
 * `$name` resolved to a cell whose `inner` was NULLed by the cycle-breaker.
 * That violated the "OK ⇒ non-NULL out" contract every caller relied on, and
 * silently propagated NULL through `$cleared` references. The fix is
 * defense-in-depth: `lcl_cell_get` rejects NULL inner, and `lcl_eval_word`
 * sets a "use of cleared cell" error message before propagating. */
static int test_issue58_cleared_cell_returns_error(void) {
  extern lcl_interp *lcl_test_interp;
  extern const char *lcl_interp_error_msg(lcl_interp * interp);
  lcl_value *cell;
  lcl_program *prog;
  lcl_value *result = NULL;
  const char *err;
  lcl_return_code rc;

  cell = lcl_cell_new(NULL);
  ASSERT_TRUE(cell != NULL);
  ASSERT_TRUE(cell->as.cell.inner == NULL);

  ASSERT_TRUE(lcl_env_let(&lcl_test_interp->env, "__i58_broken", cell) ==
              LCL_OK);
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

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_program_free(prog);
  lcl_clear_error(lcl_test_interp);
  return 1;
}

/*  #37 — lcl_program_push_command failure leaks the local `cmd`'s
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

/*  #67 — compile errors carry a message and line number.
 * lcl_program_compile_ex must report which delimiter is unmatched and
 * the line the construct opened on (not the EOF line). */
static int test_issue67_compile_ex_msg_and_line(void) {
  static const struct {
    const char *src;
    const char *msg;
    long line;
  } cases[] = {
      {"let a 1\nlet b [+ 1\nlet c 3\n",               "unmatched '['",                                2},
      {"let a 1\nlet b 2\nputs \"unclosed\nlet c 3\n", "unmatched '\"'",                               3},
      {"let x {\n  foo\n",                             "unmatched '{'",                                1},
      {"puts ]\n",                                     "unmatched ']'",                                1},
      {"let a 1\nlet l (1 2\n",                        "unmatched '('",                                2},
      {"let d #{a\n",                                  "unmatched '{'",                                1},
      {"puts ${\n",                                    "unmatched '${'",                               1},
      {"puts ${}\n",                                   "empty variable name in '${}'",                 1},
      {"let x $foo::bar\n",
       "qualified substitutions require braces: ${name::path}",                                        1},
      {"let x $foo::1bad\n",
       "qualified substitutions require braces: ${name::path}",                                        1},
      {"let x ${foo::}\n",                             "empty segment in qualified variable name",     1},
      {"let x ${::}\n",                                "empty variable name in '${}'",                 1},
      {"let x ${a::::b}\n",                            "empty segment in qualified variable name",     1},
      {"let x ${foo::1bad}\n",                         "name segment must start with a letter or '_'",
       1                                                                                                },
      {"let x ${foo bar}\n",                           "invalid character in variable name",           1},
      {"let x ${fo\no}\n",                             "invalid character in variable name",           1},
  };
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    lcl_compile_err err;
    lcl_program *prog = lcl_program_compile_ex(cases[i].src, "t.lcl", &err);

    ASSERT_TRUE(prog == NULL);
    ASSERT_TRUE(err.msg != NULL);
    ASSERT_STREQ(err.msg, cases[i].msg);
    ASSERT_TRUE(err.line == cases[i].line);
  }

  /* Success clears the report. */
  {
    lcl_compile_err err;
    lcl_program *prog = lcl_program_compile_ex("+ 1 1\n", "t.lcl", &err);

    ASSERT_TRUE(prog != NULL);
    ASSERT_TRUE(err.msg == NULL);
    ASSERT_TRUE(err.line == 0);
    lcl_program_free(prog);
  }

  return 1;
}

/*  #67 — a compile failure in lcl_eval_string_file lands in the
 * interp's error state (message + file + line), same as runtime
 * errors, so the CLI and embedders report real diagnostics. */
static int test_issue67_eval_string_sets_error_state(void) {
  extern lcl_interp *lcl_test_interp;
  lcl_value *out = NULL;
  lcl_return_code rc;

  rc = lcl_eval_string_file(lcl_test_interp, "let a 1\nlet b [+ 1\n", "i67.lcl",
                            &out);

  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(out == NULL);
  ASSERT_TRUE(lcl_test_interp->err_msg != NULL);
  ASSERT_STREQ(lcl_test_interp->err_msg, "unmatched '['");
  ASSERT_TRUE(lcl_test_interp->err_file != NULL);
  ASSERT_STREQ(lcl_test_interp->err_file, "i67.lcl");
  ASSERT_TRUE(lcl_test_interp->err_line == 2);

  lcl_clear_error(lcl_test_interp);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Require module-loader contract (relative-to-file resolution, search
 * roots, cycle detection, CWD independence). Fixtures are created in a
 * mkdtemp directory so the tests are self-contained and can chdir
 * freely; the previous CWD is always restored before asserting.
 * ---------------------------------------------------------------------------
 */
extern void lcl_add_require_root(lcl_interp *interp, const char *dir);
extern int lcl_eval_file(lcl_interp *interp, const char *path, lcl_value **out);

static int req_write_file(const char *dir, const char *name, const char *text) {
  char path[1024];
  FILE *f;

  if (strlen(dir) + strlen(name) + 2 > sizeof(path)) {
    return 0;
  }

  snprintf(path, sizeof(path), "%s/%s", dir, name);
  f = fopen(path, "w");

  if (!f) {
    return 0;
  }

  fputs(text, f);
  return fclose(f) == 0;
}

static void req_remove(const char *dir, const char *name) {
  char path[1024];

  snprintf(path, sizeof(path), "%s/%s", dir, name);
  remove(path);
}

/* Evaluate `src` in `in` and return 1 iff it succeeds and the result
 * stringifies to `expect`. */
static int req_eval_expect(lcl_interp *in, const char *src,
                           const char *expect) {
  lcl_value *out = NULL;
  lcl_return_code rc = lcl_eval_string_file(in, src, "req-test.lcl", &out);
  int ok;

  if (rc != LCL_RC_OK || !out) {
    printf("    eval failed: %s (err: %s)\n", src,
           in->err_msg ? in->err_msg : "(none)");
    return 0;
  }

  ok = strcmp(lcl_value_to_string(out), expect) == 0;

  if (!ok) {
    printf("    eval %s: got \"%s\", expected \"%s\"\n", src,
           lcl_value_to_string(out), expect);
  }

  lcl_ref_dec(out);
  return ok;
}

static int test_require_relative_to_file_chdir_independent(void) {
  char root[] = "/tmp/lcl-req-rel-XXXXXX";
  char saved_cwd[1024];
  char sub[1024];
  char main_path[1024];
  lcl_interp *in = NULL;
  lcl_interp *in2 = NULL;
  lcl_value *out = NULL;
  int rc;
  int rc2;
  int ok_a;
  int ok_b;

  ASSERT_TRUE(getcwd(saved_cwd, sizeof(saved_cwd)) != NULL);
  ASSERT_TRUE(mkdtemp(root) != NULL);

  snprintf(sub, sizeof(sub), "%s/lib", root);
  ASSERT_TRUE(mkdir(sub, 0700) == 0);
  snprintf(sub, sizeof(sub), "%s/lib/deeper", root);
  ASSERT_TRUE(mkdir(sub, 0700) == 0);
  snprintf(sub, sizeof(sub), "%s/elsewhere", root);
  ASSERT_TRUE(mkdir(sub, 0700) == 0);

  /* main requires ./lib/util, which requires ./deeper/core — the
   * nested "./" must chain relative to util's own directory. */
  ASSERT_TRUE(req_write_file(root, "main.lcl", "require ./lib/util.lcl\n"));
  ASSERT_TRUE(req_write_file(root, "lib/util.lcl",
                             "require ./deeper/core.lcl\n"
                             "namespace util { var greeting \"hi\" }\n"));
  ASSERT_TRUE(req_write_file(root, "lib/deeper/core.lcl",
                             "namespace core { var v 42 }\n"));

  snprintf(main_path, sizeof(main_path), "%s/main.lcl", root);

  /* Evaluate with the process CWD pointed somewhere unrelated. */
  ASSERT_TRUE(chdir(sub) == 0);

  in = lcl_interp_new();
  lcl_register_core(in);
  rc = lcl_eval_file(in, main_path, &out);

  if (out) {
    lcl_ref_dec(out);
    out = NULL;
  }

  ok_a = (rc == LCL_RC_OK) && req_eval_expect(in, "${core::v}", "42") &&
         req_eval_expect(in, "${util::greeting}", "hi");

  /* Same file from a different CWD in a fresh interp: behavior must
   * be identical. */
  ASSERT_TRUE(chdir("/") == 0);
  in2 = lcl_interp_new();
  lcl_register_core(in2);
  rc2 = lcl_eval_file(in2, main_path, &out);

  if (out) {
    lcl_ref_dec(out);
    out = NULL;
  }

  ok_b = (rc2 == LCL_RC_OK) && req_eval_expect(in2, "${core::v}", "42");

  ASSERT_TRUE(chdir(saved_cwd) == 0);
  lcl_interp_free(in);
  lcl_interp_free(in2);

  req_remove(root, "main.lcl");
  req_remove(root, "lib/util.lcl");
  req_remove(root, "lib/deeper/core.lcl");
  snprintf(sub, sizeof(sub), "%s/lib/deeper", root);
  rmdir(sub);
  snprintf(sub, sizeof(sub), "%s/lib", root);
  rmdir(sub);
  snprintf(sub, sizeof(sub), "%s/elsewhere", root);
  rmdir(sub);
  rmdir(root);

  ASSERT_TRUE(ok_a);
  ASSERT_TRUE(ok_b);
  return 1;
}

static int test_require_search_roots_order(void) {
  char root[] = "/tmp/lcl-req-roots-XXXXXX";
  char saved_cwd[1024];
  char root_a[1024];
  char root_b[1024];
  lcl_interp *in = NULL;
  lcl_interp *no_roots = NULL;
  lcl_interp *cwd_fallback = NULL;
  lcl_value *out = NULL;
  int ok_order;
  int ok_fallthrough;
  int ok_missing;
  int ok_cwd;

  ASSERT_TRUE(getcwd(saved_cwd, sizeof(saved_cwd)) != NULL);
  ASSERT_TRUE(mkdtemp(root) != NULL);

  snprintf(root_a, sizeof(root_a), "%s/rootA", root);
  snprintf(root_b, sizeof(root_b), "%s/rootB", root);
  ASSERT_TRUE(mkdir(root_a, 0700) == 0);
  ASSERT_TRUE(mkdir(root_b, 0700) == 0);

  ASSERT_TRUE(req_write_file(root_a, "mod.lcl",
                             "namespace whichmod { var src \"A\" }\n"));
  ASSERT_TRUE(req_write_file(root_b, "mod.lcl",
                             "namespace whichmod { var src \"B\" }\n"));
  ASSERT_TRUE(req_write_file(root_b, "only_b.lcl",
                             "namespace onlyb { var src \"B-only\" }\n"));

  /* CWD must be irrelevant when roots are registered. */
  ASSERT_TRUE(chdir("/") == 0);

  in = lcl_interp_new();
  lcl_register_core(in);
  lcl_add_require_root(in, root_a);
  lcl_add_require_root(in, root_b);

  /* Both roots have mod.lcl: registration order wins (rootA). */
  ok_order = req_eval_expect(in, "require mod.lcl\n${whichmod::src}", "A");
  /* Only rootB has only_b.lcl: search falls through in order. */
  ok_fallthrough =
      req_eval_expect(in, "require only_b.lcl\n${onlyb::src}", "B-only");

  /* No roots registered + CWD without the file: clean error naming
   * the argument. */
  no_roots = lcl_interp_new();
  lcl_register_core(no_roots);
  ok_missing = (lcl_eval_string_file(no_roots, "require mod.lcl",
                                     "req-test.lcl", &out) == LCL_RC_ERR);

  if (out) {
    lcl_ref_dec(out);
    out = NULL;
  }

  ok_missing = ok_missing && no_roots->err_msg != NULL &&
               strstr(no_roots->err_msg, "mod.lcl") != NULL;

  /* No roots registered: bare paths keep resolving against the CWD
   * (legacy behavior). */
  ASSERT_TRUE(chdir(root_b) == 0);
  cwd_fallback = lcl_interp_new();
  lcl_register_core(cwd_fallback);
  ok_cwd =
      req_eval_expect(cwd_fallback, "require mod.lcl\n${whichmod::src}", "B");

  ASSERT_TRUE(chdir(saved_cwd) == 0);
  lcl_interp_free(in);
  lcl_interp_free(no_roots);
  lcl_interp_free(cwd_fallback);

  req_remove(root_a, "mod.lcl");
  req_remove(root_b, "mod.lcl");
  req_remove(root_b, "only_b.lcl");
  rmdir(root_a);
  rmdir(root_b);
  rmdir(root);

  ASSERT_TRUE(ok_order);
  ASSERT_TRUE(ok_fallthrough);
  ASSERT_TRUE(ok_missing);
  ASSERT_TRUE(ok_cwd);
  return 1;
}

static int test_require_cycle_detection(void) {
  char root[] = "/tmp/lcl-req-cyc-XXXXXX";
  char path[1024];
  lcl_interp *in = NULL;
  lcl_interp *in2 = NULL;
  lcl_value *out = NULL;
  int rc;
  int ok_pair;
  int ok_self;

  ASSERT_TRUE(mkdtemp(root) != NULL);

  ASSERT_TRUE(req_write_file(root, "cyc_a.lcl",
                             "require ./cyc_b.lcl\n"
                             "namespace cyca { var x 1 }\n"));
  ASSERT_TRUE(req_write_file(root, "cyc_b.lcl",
                             "require ./cyc_a.lcl\n"
                             "namespace cycb { var x 2 }\n"));
  ASSERT_TRUE(req_write_file(root, "self.lcl", "require ./self.lcl\n"));

  in = lcl_interp_new();
  lcl_register_core(in);
  snprintf(path, sizeof(path), "%s/cyc_a.lcl", root);
  rc = lcl_eval_file(in, path, &out);

  if (out) {
    lcl_ref_dec(out);
    out = NULL;
  }

  ok_pair = (rc == LCL_RC_ERR) && in->err_msg != NULL &&
            strstr(in->err_msg, "dependency cycle") != NULL &&
            strstr(in->err_msg, "cyc_a.lcl") != NULL &&
            strstr(in->err_msg, "cyc_b.lcl") != NULL;

  if (!ok_pair) {
    printf("    cycle err: %s\n", in->err_msg ? in->err_msg : "(none)");
  }

  in2 = lcl_interp_new();
  lcl_register_core(in2);
  snprintf(path, sizeof(path), "%s/self.lcl", root);
  rc = lcl_eval_file(in2, path, &out);

  if (out) {
    lcl_ref_dec(out);
    out = NULL;
  }

  ok_self = (rc == LCL_RC_ERR) && in2->err_msg != NULL &&
            strstr(in2->err_msg, "dependency cycle") != NULL;

  lcl_interp_free(in);
  lcl_interp_free(in2);

  req_remove(root, "cyc_a.lcl");
  req_remove(root, "cyc_b.lcl");
  req_remove(root, "self.lcl");
  rmdir(root);

  ASSERT_TRUE(ok_pair);
  ASSERT_TRUE(ok_self);
  return 1;
}

/* ---------------------------------------------------------------------------
 * Lexical module paths. lcl_path_clean/join/dirname
 * are pure string operations: the tables below are the pinned
 * language semantics, exercised with no filesystem at all.
 * ---------------------------------------------------------------------------
 */

static int path_clean_case(const char *in, const char *expect) {
  char *got = lcl_path_clean(in);
  int ok = got != NULL && strcmp(got, expect) == 0;

  if (!ok) {
    printf("    clean(\"%s\"): got \"%s\", expected \"%s\"\n", in,
           got ? got : "(null)", expect);
  }

  free(got);
  return ok;
}

static int test_path_clean_table(void) {
  /* Empty and dot */
  ASSERT_TRUE(path_clean_case("", "."));
  ASSERT_TRUE(path_clean_case(".", "."));
  ASSERT_TRUE(path_clean_case("./", "."));
  ASSERT_TRUE(path_clean_case("./.", "."));

  /* Already clean */
  ASSERT_TRUE(path_clean_case("a", "a"));
  ASSERT_TRUE(path_clean_case("a/b/c", "a/b/c"));
  ASSERT_TRUE(path_clean_case("/a/b", "/a/b"));

  /* "." components */
  ASSERT_TRUE(path_clean_case("a/.", "a"));
  ASSERT_TRUE(path_clean_case("./a", "a"));
  ASSERT_TRUE(path_clean_case("a/./b", "a/b"));

  /* Repeated and trailing separators */
  ASSERT_TRUE(path_clean_case("a//b", "a/b"));
  ASSERT_TRUE(path_clean_case("//a///b////c", "/a/b/c"));
  ASSERT_TRUE(path_clean_case("a/b/", "a/b"));
  ASSERT_TRUE(path_clean_case("/", "/"));
  ASSERT_TRUE(path_clean_case("///", "/"));

  /* ".." cancels the previous component */
  ASSERT_TRUE(path_clean_case("a/b/..", "a"));
  ASSERT_TRUE(path_clean_case("a/..", "."));
  ASSERT_TRUE(path_clean_case("a/../b", "b"));
  ASSERT_TRUE(path_clean_case("a/b/../../c", "c"));

  /* Relative paths keep excess ".." */
  ASSERT_TRUE(path_clean_case("..", ".."));
  ASSERT_TRUE(path_clean_case("../..", "../.."));
  ASSERT_TRUE(path_clean_case("a/../../b", "../b"));
  ASSERT_TRUE(path_clean_case("../a", "../a"));
  ASSERT_TRUE(path_clean_case("../a/../..", "../.."));

  /* ".." cannot climb above a lexical root */
  ASSERT_TRUE(path_clean_case("/..", "/"));
  ASSERT_TRUE(path_clean_case("/a/..", "/"));
  ASSERT_TRUE(path_clean_case("/../../a", "/a"));

  /* Dots that are not "." or ".." are ordinary components */
  ASSERT_TRUE(path_clean_case("a/...", "a/..."));
  ASSERT_TRUE(path_clean_case(".foo", ".foo"));
  ASSERT_TRUE(path_clean_case("..bar", "..bar"));
  ASSERT_TRUE(path_clean_case("a/.b/..", "a"));

  return 1;
}

static int path_join_case(const char *base, const char *rel,
                          const char *expect) {
  char *got = lcl_path_join(base, rel);
  int ok = got != NULL && strcmp(got, expect) == 0;

  if (!ok) {
    printf("    join(\"%s\", \"%s\"): got \"%s\", expected \"%s\"\n", base, rel,
           got ? got : "(null)", expect);
  }

  free(got);
  return ok;
}

static int path_dirname_case(const char *in, const char *expect) {
  char *got = lcl_path_dirname(in);
  int ok = got != NULL && strcmp(got, expect) == 0;

  if (!ok) {
    printf("    dirname(\"%s\"): got \"%s\", expected \"%s\"\n", in,
           got ? got : "(null)", expect);
  }

  free(got);
  return ok;
}

static int test_path_join_and_dirname(void) {
  ASSERT_TRUE(path_join_case("/a/b", "c.lcl", "/a/b/c.lcl"));
  ASSERT_TRUE(path_join_case("/a/b", "./c.lcl", "/a/b/c.lcl"));
  ASSERT_TRUE(path_join_case("/a/b", "../c.lcl", "/a/c.lcl"));
  ASSERT_TRUE(path_join_case(".", "./lib/x.lcl", "lib/x.lcl"));
  ASSERT_TRUE(path_join_case("lib", "../x.lcl", "x.lcl"));
  ASSERT_TRUE(path_join_case("lib/", "x.lcl", "lib/x.lcl"));

  /* A rooted rel ignores base; an empty base yields cleaned rel. */
  ASSERT_TRUE(path_join_case("/a", "/b/c.lcl", "/b/c.lcl"));
  ASSERT_TRUE(path_join_case("", "a//b", "a/b"));

  ASSERT_TRUE(path_dirname_case("/a/b/c.lcl", "/a/b"));
  ASSERT_TRUE(path_dirname_case("x.lcl", "."));
  ASSERT_TRUE(path_dirname_case("/x.lcl", "/"));
  ASSERT_TRUE(path_dirname_case("a/b", "a"));

  return 1;
}

/* ---------------------------------------------------------------------------
 * Module-identity hook (lcl_set_module_key_fn): the hook derives the
 * require cache/cycle key from the resolved lexical path. Identity
 * only — resolution and diagnostics stay lexical.
 * ---------------------------------------------------------------------------
 */
extern void lcl_set_module_key_fn(lcl_interp *interp,
                                  char *(*fn)(const char *lexical_path,
                                              void *userdata),
                                  void *userdata);

/* Collapses every module to one identity: the second require of ANY
 * file must be served from cache. */
static char *modkey_constant(const char *lexical_path, void *userdata) {
  (void)lexical_path;
  (void)userdata;
  return strdup("the-one-module");
}

/* Declines to produce a key: core must fall back to the lexical path. */
static char *modkey_null(const char *lexical_path, void *userdata) {
  (void)lexical_path;
  (void)userdata;
  return NULL;
}

static int test_require_module_key_hook(void) {
  char root[] = "/tmp/lcl-req-key-XXXXXX";
  char src[1200];
  lcl_interp *in = NULL;
  lcl_interp *in2 = NULL;
  lcl_value *out = NULL;
  lcl_return_code rc;
  int ok_first;
  int ok_dedup;
  int ok_fallback;

  ASSERT_TRUE(mkdtemp(root) != NULL);
  ASSERT_TRUE(req_write_file(root, "amod.lcl", "namespace amod { var x 1 }\n"));
  ASSERT_TRUE(req_write_file(root, "bmod.lcl", "namespace bmod { var x 2 }\n"));

  /* Constant key: amod and bmod share one identity, so requiring
   * bmod after amod is a cache hit — bmod's body never runs. */
  in = lcl_interp_new();
  lcl_register_core(in);
  lcl_set_module_key_fn(in, modkey_constant, NULL);

  snprintf(src, sizeof(src), "require %s/amod.lcl\n${amod::x}", root);
  ok_first = req_eval_expect(in, src, "1");

  snprintf(src, sizeof(src), "require %s/bmod.lcl", root);
  ok_dedup = req_eval_expect(in, src, "");

  rc = lcl_eval_string_file(in, "${bmod::x}", "req-test.lcl", &out);
  ok_dedup = ok_dedup && (rc == LCL_RC_ERR);

  if (out) {
    lcl_ref_dec(out);
    out = NULL;
  }

  /* NULL-returning hook: lexical fallback, both modules load. */
  in2 = lcl_interp_new();
  lcl_register_core(in2);
  lcl_set_module_key_fn(in2, modkey_null, NULL);

  snprintf(
      src, sizeof(src),
      "require %s/amod.lcl\nrequire %s/bmod.lcl\n[+ ${amod::x} ${bmod::x}]",
      root, root);
  ok_fallback = req_eval_expect(in2, src, "3");

  lcl_interp_free(in);
  lcl_interp_free(in2);

  req_remove(root, "amod.lcl");
  req_remove(root, "bmod.lcl");
  rmdir(root);

  ASSERT_TRUE(ok_first);
  ASSERT_TRUE(ok_dedup);
  ASSERT_TRUE(ok_fallback);
  return 1;
}

/* Name-resolution redesign: anchor lifecycle under teardown.
 *
 * (a) A builder body that errors mid-definition leaves its pending
 * anchor list unconsumed (released defensively at the next pop or
 * interp free) — must not leak or dangle.
 *
 * (b) A proc extracted from a namespace that then dies keeps a nulled
 * anchor and fails cleanly.  (c) Live self-reference through re-entry
 * (the old M -> f -> M cycle error). Fresh interp so teardown runs
 * with live anchors. */
static int test_ns_anchor_lifecycle(void) {
  lcl_interp *in = lcl_interp_new();
  lcl_value *out = NULL;
  lcl_return_code rc;

  ASSERT_TRUE(in != NULL);
  lcl_register_core(in);

  rc = lcl_eval_string(
      in, "namespace T { proc f {} { T::g } ; this_does_not_exist }\n", &out);
  ASSERT_TRUE(rc == LCL_RC_ERR);
  ASSERT_TRUE(out == NULL);
  lcl_clear_error(in);

  rc = lcl_eval_string(
      in,
      "proc mk {} { namespace D { proc f {} { D::g } ; proc g {} { 1 } } ; "
      "${D::f} }\n"
      "let f [mk]\n"
      "catch { apply $f } r e\n"
      "$e\n",
      &out);
  ASSERT_TRUE(rc == LCL_RC_OK);
  ASSERT_TRUE(out != NULL);
  {
    const char *msg = NULL;

    ASSERT_TRUE(lcl_value_get_string(out, &msg) == LCL_OK);
    ASSERT_STREQ(msg, "D: namespace no longer exists");
  }
  lcl_ref_dec(out);
  out = NULL;

  rc = lcl_eval_string(in,
                       "namespace S { proc f {} { S::g } ; proc g {} { 7 } }\n"
                       "namespace S { proc f {} { S::g } }\n"
                       "S::f\n",
                       &out);
  ASSERT_TRUE(rc == LCL_RC_OK);
  ASSERT_TRUE(out != NULL);
  {
    long n = 0;

    ASSERT_TRUE(lcl_value_to_int(out, &n) == LCL_OK);
    ASSERT_TRUE(n == 7);
  }
  lcl_ref_dec(out);

  lcl_interp_free(in);
  return 1;
}

int run_test(void) {
  int total = 0;
  int passed = 0;

#define RUN(tfn)                                                               \
  do {                                                                         \
    int ok;                                                                    \
    total++;                                                                   \
    printf("TEST %s ...\n", #tfn);                                             \
    ok = tfn();                                                                \
    if (ok) {                                                                  \
      passed++;                                                                \
      printf("  ok\n");                                                        \
    } else {                                                                   \
      printf("  FAIL\n");                                                      \
    }                                                                          \
  } while (0)

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
  RUN(test_issue22_dict_put_clone_oom);
  RUN(test_issue22_dict_del_clone_oom);
  RUN(test_issue22_list_push_clone_oom);
  RUN(test_issue22_list_set_clone_oom);
  RUN(test_issue24_scanner_strndup_oom);
  RUN(test_issue26_hash_used_tombstone_reuse);
  RUN(test_issue27_fnv1a_known_vector);
  RUN(test_issue33_catch_strdup_oom);
  RUN(test_issue35_public_api_null_safety);
  RUN(test_issue15_to_string_null_contract);
  RUN(test_issue75_float_compact_formatting);
  RUN(test_issue75_literal_grammar);
  RUN(test_issue75_literal_typing);
  RUN(test_issue75_numeric_text);
  RUN(test_issue59_eval_program_cur_file_uaf);
  RUN(test_version_surface);
  RUN(test_issue37_push_command_leak);
  RUN(test_issue39_import_cleared_cell);
  RUN(test_issue40_namespace_max_depth_leak);
  RUN(test_issue41_namespace_reentry_oom);
  RUN(test_issue42_dict_clone_put_oom);
  RUN(test_issue45_skip_balanced_depth_limit);
  RUN(test_issue71_compile_nesting_cap);
  RUN(test_issue71_step_hook_budget);
  RUN(test_issue98_bare_spread_is_compile_error);
  RUN(test_sub_literal_scan_unification);
  RUN(test_span_line_attribution);
  RUN(test_body_line_attribution);
  RUN(test_proc_origin);
  RUN(test_eval_at_error_context);
  RUN(test_issue102_set_error_copies);
  RUN(test_issue102_ns_def_ownership);
  RUN(test_peek_accessors_borrow);
  RUN(test_issue99_step_budget_empty_body_loops);
  RUN(test_issue47_param_bind_oom);
  RUN(test_issue58_cleared_cell_returns_error);
  RUN(test_issue67_compile_ex_msg_and_line);
  RUN(test_issue67_eval_string_sets_error_state);
  RUN(test_require_relative_to_file_chdir_independent);
  RUN(test_require_search_roots_order);
  RUN(test_require_cycle_detection);
  RUN(test_path_clean_table);
  RUN(test_path_join_and_dirname);
  RUN(test_require_module_key_hook);
  RUN(test_ns_anchor_lifecycle);

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
