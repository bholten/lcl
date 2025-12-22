#include <stdio.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-eval.h"
#include "lcl-values.h"
#include "lcl-stdlib.h"

/**
   Testing
**/

/* tiny string buffer */
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
    /* space between pieces (but not before the first) is handled by caller */
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

/* public helper used by tests */
static int lcl_program_dump(const lcl_program *P, char **out_str) {
  SB b;
  sb_init(&b);
  if (!P) return 0;

  if (!dump_program_rec(P, &b, 0)) {
    sb_free(&b);
    return 0;
  }

  *out_str = b.s; /* caller free()s */

  return 1;
}

/** Signatures **/
int  lcl_program_dump(const lcl_program *P, char **out_str);
lcl_program *lcl_compile(const char *src, const char *file);
void lcl_program_free(lcl_program *P);

/* ---- dumb assert macros (C89-friendly) ---- */
#define ASSERT_TRUE(cond) do { if (!(cond)) { \
  printf("    assert failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
  return 0; } } while(0)

#define ASSERT_STREQ(a,b) do { if (((a)==NULL && (b)!=NULL) || ((a)!=NULL && (b)==NULL) || strcmp((a),(b))!=0) { \
  printf("    assert failed: strings not equal\n    A: %s\n    B: %s\n", (a)?(a):"(null)", (b)?(b):"(null)"); \
  return 0; } } while(0)

/* ---- helperscm ---- */
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

/* ---- test cases ---- */
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
    "# leading comment\n"
    "set a 1  # NOT a comment in middle\n"
    "  ; # trailing comment\n"
    "puts $a\n";
  const char *exp =
    "cmd(line=2): [lit:\"set\"] [lit:\"a\"] [lit:\"1\"] [lit:\"#\"] [lit:\"NOT\"] [lit:\"a\"] [lit:\"comment\"] [lit:\"in\"] [lit:\"middle\"] ; "
    "cmd(line=4): [lit:\"puts\"] [var:\"a\"]";
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

  printf("\n%d/%d tests passed\n", passed, total);
  return (passed == total) ? 0 : 1;  
}

#ifdef LCL_TEST
int main(void) {
  int result;
  lcl_interp *interp = lcl_interp_new();
  lcl_register_core(interp);
  result = run_test();
  lcl_interp_free(interp);
  return result;
}
#endif
