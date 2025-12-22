#ifndef LCL_LEX_H
#define LCL_LEX_H

#include <stdlib.h>

typedef struct lcl_word lcl_word;

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
  const char *file;
} lcl_program;

void lcl_program_free(lcl_program *p);
lcl_program *lcl_program_compile(const char *src, const char *file);
int lcl_program_push_command(lcl_program *p, lcl_command *src);

typedef enum {
  LCL_WP_LIT,
  LCL_WP_VAR,
  LCL_WP_SUBCMD
} lcl_word_piece_kind;

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
};

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
} lcl_scan;

void lcl_scan_init(lcl_scan *sc, const char *src);
int lcl_scan_word(lcl_scan *sc, lcl_word *w);
int lcl_scan_parse_command(lcl_scan *sc, lcl_command *cmd);

#endif
