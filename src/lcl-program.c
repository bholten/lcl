#include <memory.h>
#include <string.h>

#include "lcl-lex.h"
#include "str-compat.h"

void lcl_program_free(lcl_program *p) {
  int i;

  if (!p) {
    return;
  }

  for (i = 0; i < p->ncmd; i++) {
    lcl_command_free(&p->cmd[i]);
  }

  free(p->cmd);
  free(p->file);
  free(p);
}

lcl_program *lcl_program_compile(const char *src, const char *file) {
  lcl_scan sc;
  lcl_program *p;

  lcl_scan_init(&sc, src);
  p = (lcl_program *)calloc(1, sizeof(*p));

  if (!p) {
    return NULL;
  }

  p->file = file ? strdup(file) : NULL;
  if (file && !p->file) {
    free(p);
    return NULL;
  }

  for (;;) {
    lcl_command cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch (lcl_scan_parse_command(&sc, &cmd)) {
    case -1:
      lcl_command_free(&cmd);
      lcl_program_free(p);
      return NULL;
    case 0: return p;
    case 1:
      if (!lcl_program_push_command(p, &cmd)) {
        /* Bugfix: push_command does not consume `cmd` on failure;
         * free its owned words/pieces before discarding. */
        lcl_command_free(&cmd);
        lcl_program_free(p);
        return NULL;
      }
      break;
    default: break;
    }
  }
}

lcl_program *lcl_program_compile_bytes(const char *src, size_t len,
                                       const char *file) {
  lcl_scan sc;
  lcl_program *p;

  lcl_scan_init_bytes(&sc, src, len);
  p = (lcl_program *)calloc(1, sizeof(*p));

  if (!p) {
    return NULL;
  }

  p->file = file ? strdup(file) : NULL;
  if (file && !p->file) {
    free(p);
    return NULL;
  }

  for (;;) {
    lcl_command cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch (lcl_scan_parse_command(&sc, &cmd)) {
    case -1:
      lcl_command_free(&cmd);
      lcl_program_free(p);
      return NULL;
    case 0: return p;
    case 1:
      if (!lcl_program_push_command(p, &cmd)) {
        lcl_command_free(&cmd);
        lcl_program_free(p);
        return NULL;
      }
      break;
    default: break;
    }
  }
}

