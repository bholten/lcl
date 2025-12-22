#include <ctype.h>
#include <memory.h>
#include <string.h>

#include "lcl-lex.h"
#include "str-compat.h"

/** TODO this needs to be better **/
static int lcl_word_push_word_piece(lcl_word *word,
                                    lcl_word_piece wp) {
  int idx;

  if (word->np >= word->cap) {
    int newcap = (word->cap > 0) ? word->cap * 2 : 4;

    {
      size_t bytes = (size_t)newcap * sizeof(*word->wp);
      void *p = realloc(word->wp, bytes);

      if (!p) return 0;

      word->wp = (lcl_word_piece *)p;
      word->cap = newcap;
    }
  }

  idx = word->np;
  word->wp[idx] = wp;
  word->np = idx + 1;

  return 1;
}

void lcl_word_piece_free(lcl_word_piece *wp) {
  switch (wp->kind) {
  case LCL_WP_LIT:
    free(wp->as.lit.s);
    break;
  case LCL_WP_VAR:
    free(wp->as.var.name);
    break;
  case LCL_WP_SUBCMD:
    lcl_program_free(wp->as.sub.program);
    break;
  default:
    break;
  }

  free(wp);
}

void lcl_word_free(lcl_word *w) {
  lcl_word_piece_free(w->wp);
  free(w);
}

int lcl_word_add_lit(lcl_word *w, const char *s, size_t n) {
  lcl_word_piece wp;
  wp.kind = LCL_WP_LIT;
  wp.as.lit.s = (char *)malloc(n + 1);

  if (!wp.as.lit.s) {
    return 0;
  }

  memcpy(wp.as.lit.s, s, n);
  wp.as.lit.s[n] = '\0';
  wp.as.lit.n = n;

  lcl_word_push_word_piece(w, wp);

  return 1;
}

int lcl_word_add_var(lcl_word *w, const char *name) {
  lcl_word_piece wp;
  size_t n = strlen(name);

  wp.kind = LCL_WP_VAR;
  wp.as.var.name = (char *)malloc(n + 1);

  if (!wp.as.var.name) {
    return 0;
  }

  memcpy(wp.as.var.name, name, n + 1);

  lcl_word_push_word_piece(w, wp);

  return 1;  
}

int lcl_word_add_sub(lcl_word *w, lcl_program *sub) {
  lcl_word_piece wp;
  wp.kind = LCL_WP_SUBCMD;
  wp.as.sub.program = sub;

  lcl_word_push_word_piece(w, wp);

  return 1;
}
