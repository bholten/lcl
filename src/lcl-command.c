#include <memory.h>

#include "lcl-lex.h"

void lcl_command_free(lcl_command *cmd) {
  int i;
  int j;

  for (i = 0; i < cmd->argc; i++) {
    lcl_word *w = &cmd->w[i];

    for (j = 0; j < w->np; j++) {
      lcl_word_piece *pc = &w->wp[j];

      switch (pc->kind) {
      case LCL_WP_LIT:
        free(pc->as.lit.s);
        break;
      case LCL_WP_VAR:
        free(pc->as.var.name);
        break;
      case LCL_WP_SUBCMD:
        lcl_program_free(pc->as.sub.program);
        break;
      }
    }

    free(w->wp);
  }

  free(cmd->w);
  memset(cmd, 0, sizeof(*cmd));
}

int lcl_command_push_word(lcl_command *cmd, lcl_word *w) {
  int idx;

  if (cmd->argc >= cmd->cap) {
    int newcap = cmd->cap ? cmd->cap * 2 : 4;
    size_t bytes = (size_t)newcap * sizeof(*cmd->w);
    void *nv = realloc(cmd->w, bytes);

    if (!nv) return 0;

    cmd->w = (lcl_word *)nv;
    cmd->cap = newcap;
  }

  idx = cmd->argc++;
  cmd->w[idx] = *w;
  memset(w, 0, sizeof(*w));
  
  return 1;
}
