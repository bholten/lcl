#include <memory.h>

#include "lcl-lex.h"

void lcl_program_free(lcl_program *p) {
  int i;

  if (!p) return;

  for (i = 0; i < p->ncmd; i++) {
    lcl_command_free(&p->cmd[i]);
  }

  free(p->cmd);
  free(p);
}

lcl_program *lcl_program_compile(const char *src, const char *file) {
  lcl_scan sc;
  lcl_program *p;

  lcl_scan_init(&sc, src);
  p = (lcl_program *)calloc(1, sizeof(*p));

  if (!p) return NULL;

  p->file = file;

  for (;;) {
    lcl_command cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch (lcl_scan_parse_command(&sc, &cmd)) {
    case -1:
      lcl_command_free(&cmd);
      lcl_program_free(p);
      return NULL;
    case 0:
      return p;
    case 1:
      if (!lcl_program_push_command(p, &cmd)) {
        lcl_program_free(p);
        return NULL;
      }  
      break;
    default:
      break;
    }
  }  
}

int lcl_program_push_command(lcl_program *p,
                             lcl_command *src) {
  int idx;

  if (p->ncmd >= p->cap) {
    int newcap = p->cap ? p->cap * 2 : 4;
    size_t bytes = (size_t)newcap * sizeof(*p->cmd);
    void *nv = realloc(p->cmd, bytes);

    if (!nv) return 0;

    p->cmd = (lcl_command *)nv;
    p->cap = newcap;
  }

  idx = p->ncmd++;
  p->cmd[idx] = *src;
  memset(src, 0, sizeof(*src));

  return 1;
}
