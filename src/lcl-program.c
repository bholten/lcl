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

static void compile_err_set(lcl_compile_err *err, const char *msg, long line) {
  if (err) {
    err->msg = msg;
    err->line = line;
  }
}

/* Shared compile loop; `sc` is already initialized over the source. */
static lcl_program *compile_scan(lcl_scan *sc, const char *file,
                                 lcl_compile_err *err) {
  lcl_program *p = (lcl_program *)calloc(1, sizeof(*p));

  compile_err_set(err, NULL, 0);

  if (!p) {
    compile_err_set(err, "out of memory", sc->line);
    return NULL;
  }

  p->file = file ? strdup(file) : NULL;
  if (file && !p->file) {
    free(p);
    compile_err_set(err, "out of memory", sc->line);
    return NULL;
  }

  for (;;) {
    lcl_command cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch (lcl_scan_parse_command(sc, &cmd)) {
    case -1:
      compile_err_set(err, sc->err ? sc->err : "syntax error",
                      sc->err_line ? sc->err_line : sc->line);
      lcl_command_free(&cmd);
      lcl_program_free(p);
      return NULL;
    case 0: return p;
    case 1:
      if (!lcl_program_push_command(p, &cmd)) {
        /* Bugfix: push_command does not consume `cmd` on failure;
         * free its owned words/pieces before discarding. */
        compile_err_set(err, "out of memory", (long)cmd.line);
        lcl_command_free(&cmd);
        lcl_program_free(p);
        return NULL;
      }
      break;
    default: break;
    }
  }
}

lcl_program *lcl_program_compile_depth(const char *src, size_t len,
                                       const char *file, lcl_compile_err *err,
                                       int nest) {
  lcl_scan sc;

  if (nest > LCL_SCAN_MAX_NEST) {
    compile_err_set(err, "nesting too deep", 1);
    return NULL;
  }

  lcl_scan_init_bytes(&sc, src, len);
  sc.nest = nest;
  return compile_scan(&sc, file, err);
}

lcl_program *lcl_program_compile_span(const char *src, size_t len,
                                      lcl_compile_err *err, int nest,
                                      long start_line) {
  lcl_scan sc;

  if (nest > LCL_SCAN_MAX_NEST) {
    compile_err_set(err, "nesting too deep", start_line);
    return NULL;
  }

  lcl_scan_init_bytes(&sc, src, len);
  sc.nest = nest;
  sc.line = start_line;
  sc.sep_as_ws = 1;
  return compile_scan(&sc, NULL, err);
}

lcl_program *lcl_program_compile_ex(const char *src, const char *file,
                                    lcl_compile_err *err) {
  return lcl_program_compile_depth(src, strlen(src), file, err, 0);
}

lcl_program *lcl_program_compile_bytes_ex(const char *src, size_t len,
                                          const char *file,
                                          lcl_compile_err *err) {
  return lcl_program_compile_depth(src, len, file, err, 0);
}

lcl_program *lcl_program_compile(const char *src, const char *file) {
  return lcl_program_compile_ex(src, file, NULL);
}

lcl_program *lcl_program_compile_bytes(const char *src, size_t len,
                                       const char *file) {
  return lcl_program_compile_bytes_ex(src, len, file, NULL);
}
