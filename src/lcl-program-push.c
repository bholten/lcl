#include <stdlib.h>
#include <string.h>

#include "lcl-lex.h"

int lcl_program_push_command(lcl_program *p, lcl_command *src) {
  int idx;

  if (p->ncmd >= p->cap) {
    int newcap = p->cap ? p->cap * 2 : 4;
    size_t bytes = (size_t)newcap * sizeof(*p->cmd);
    void *nv = realloc(p->cmd, bytes);

    if (!nv) {
      return 0;
    }

    p->cmd = (lcl_command *)nv;
    p->cap = newcap;
  }

  idx = p->ncmd++;
  p->cmd[idx] = *src;
  memset(src, 0, sizeof(*src));

  return 1;
}
