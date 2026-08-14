#ifndef LCL_LEX_H
#define LCL_LEX_H

#include <stdlib.h>

typedef struct lcl_word lcl_word;
struct lcl_value;

typedef struct {
  lcl_word *w;
  int argc;
  int cap;
  int line;
} lcl_command;

void lcl_command_free(lcl_command *cmd);
int lcl_command_push_word(lcl_command *cmd, lcl_word *w);

typedef struct {
  lcl_command *cmd;
  int ncmd;
  int cap;
  char *file;
} lcl_program;

void lcl_program_free(lcl_program *p);
lcl_program *lcl_program_compile(const char *src, const char *file);
lcl_program *lcl_program_compile_bytes(const char *src, size_t len,
                                       const char *file);

/* Compile-error report. `msg` is always a static string (never owned
 * by the caller); `line` is 1-based within the compiled source. */
typedef struct {
  const char *msg;
  long line;
} lcl_compile_err;

lcl_program *lcl_program_compile_ex(const char *src, const char *file,
                                    lcl_compile_err *err);
lcl_program *lcl_program_compile_bytes_ex(const char *src, size_t len,
                                          const char *file,
                                          lcl_compile_err *err);

/* Maximum syntactic nesting depth. Bounds both the scanner's
 * alternating-delimiter recursion (skip_balanced) and the
 * compile-time recursion into subprograms (`[...]`, `(...)`, `#{...}`
 * and braced-body precompiles), which recurse one C round-trip per
 * nesting level. Past the cap: "nesting too deep" compile error. */
#define LCL_SCAN_MAX_NEST 256

/* Internal compile entry carrying the syntactic nesting level.
 * `nest` is 0 for top-level source; the scanner passes `nest + 1`
 * when compiling a nested subprogram. Fails with "nesting too deep"
 * once `nest` exceeds LCL_SCAN_MAX_NEST. The public compile
 * functions are nest-0 wrappers. */
lcl_program *lcl_program_compile_depth(const char *src, size_t len,
                                       const char *file, lcl_compile_err *err,
                                       int nest);
int lcl_program_push_command(lcl_program *p, lcl_command *src);

typedef enum { LCL_WP_LIT, LCL_WP_VAR, LCL_WP_SUBCMD } lcl_word_piece_kind;

typedef struct {
  lcl_word_piece_kind kind;
  union {
    struct {
      char *s;
      size_t n;
    } lit;
    struct {
      char *name;
    } var;
    struct {
      lcl_program *program;
    } sub;
  } as;
} lcl_word_piece;

void lcl_word_piece_free(lcl_word_piece *wp);

struct lcl_word {
  lcl_word_piece *wp;
  int np;
  int cap;
  unsigned quoted : 1;
  unsigned braced : 1;
  unsigned expand : 1;
  lcl_program *compiled;
  /* Non-NULL iff the word is an unbraced, unquoted, single-piece
   * literal matching the numeric-literal grammar (#75 rule 1): the
   * INT/FLOAT value it denotes, built once at scan time and owned by
   * the word (+1 ref, released with the word). The LIT piece keeps
   * the source bytes for spans/dumps; evaluation returns this value
   * instead. NULL for every other word, including all words not
   * built by the scanner. */
  struct lcl_value *typed;
  /* Byte span of the word in the scanned source, half-open
   * [src_start, src_end), including any @ / quotes / braces. Stamped
   * by lcl_scan_word; both 0 for words not built by the scanner. */
  long src_start;
  long src_end;
};

void lcl_word_free_contents(lcl_word *w);
void lcl_word_free(lcl_word *w);
int lcl_word_add_lit(lcl_word *w, const char *s, size_t n);
int lcl_word_add_var(lcl_word *w, const char *name);
int lcl_word_add_sub(lcl_word *w, lcl_program *sub);

typedef struct {
  const char *s;
  long i;
  long len;
  long line;
  int at_cmd_start;
  /* First parse failure: static message + 1-based line. Set once;
   * later failures during unwinding do not overwrite it. */
  const char *err;
  long err_line;
  int nest;
} lcl_scan;

void lcl_scan_init(lcl_scan *sc, const char *src);
void lcl_scan_init_bytes(lcl_scan *sc, const char *src, size_t len);
int lcl_scan_word(lcl_scan *sc, lcl_word *w);
int lcl_scan_parse_command(lcl_scan *sc, lcl_command *cmd);

#endif
