#include <ctype.h>
#include <memory.h>
#include <string.h>

#include "lcl-lex.h"
#include "str-compat.h"

static int is_name(int c) {
  return (c == '_' ||
          c == ':' ||
          (c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c>='0' && c<='9'));
}

static void skip_cmd_ws_and_comments(lcl_scan *sc) {
  int bol = sc->at_cmd_start;

  while (sc->i < sc->len) {
    char c = sc->s[sc->i];

    if (c == ' ' || c == '\t' || c == '\r') {
      sc->i++;
      continue;
    }

    if (c == '\n') {
      sc->i++;
      sc->line++;
      bol = 1;
      continue;
    }

    if (bol && c == '#') {
      while (sc->i < sc->len && sc->s[sc->i] != '\n') {
        sc->i++;
      }

      continue;
    }

    break;
  }
}

static void skip_intra_ws(lcl_scan *sc) {
  while (sc->i < sc->len) {
    char c = sc->s[sc->i];

    if (c == ' ' || c == '\t' || c == '\r') {
      sc->i++;
      continue;
    }

    break;
  }
}

void lcl_scan_init(lcl_scan *sc, const char *src) {
  sc->s = src;
  sc->i = 0;
  sc->len = (long)strlen(src);
  sc->line = 1;
  sc->at_cmd_start = 1;
}

int lcl_scan_word(lcl_scan *sc, lcl_word *w) {
  int in_quotes = 0;
  long start;

  if (sc->i < sc->len && sc->s[sc->i] == '{') {
    long depth = 1;
    sc->i++;
    start = sc->i;

    while (sc->i < sc->len) {
      char c = sc->s[sc->i++];

      if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;

        if (!depth) {
          break;
        }
      } else if (c == '\n') {
        sc->line++;
      }
    }

    if (depth) {
      return -1;
    }

    if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start - 1))) {
      return -1;
    }

    w->braced = 1;

    return 1;
  }

  if (sc->i < sc->len && sc->s[sc->i] == '"') {
    in_quotes = 1;
    w->quoted = 1;
    sc->i++;
  }

  start = sc->i;

  while (sc->i < sc->len) {
    char c = sc->s[sc->i];

    if (!in_quotes && (c == ' ' ||
                       c == '\t' ||
                       c == '\r' ||
                       c == ';' ||
                       c == '\n')) {
      break;
    }

    if (!in_quotes && c == ']') {
      break;
    }

    if (c == '$') {
      if (sc->i > start) {
        if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
          return -1;
        }
      }

      sc->i++;

      if (sc->i < sc->len && sc->s[sc->i] == '{') {
        long j = ++sc->i;

        while (j < sc->len && sc->s[j] != '}') {
          if (sc->s[j] == '\n') {
            sc->line++;
            j++;
          }
        }

        if (j >= sc->len) return -1;
        if (j == sc->i) return -1;

        {
          size_t n = (size_t)(j - sc->i);
          char *nm = (char *)malloc(n + 1);

          if (!nm) return -1;

          memcpy(nm, sc->s + sc->i, n);
          nm[n] = '\0';

          if (!lcl_word_add_var(w, nm)) {
            free(nm);
            return -1;
          }

          free(nm);
        }

        sc->i = j + 1;
        start = sc->i;
      } else {
        long j = sc->i;

        if (j < sc->len && (isalpha((unsigned char)sc->s[j]) || sc->s[j] == '_')) {
          char *varname;
          int ok;

          j++;

          while (j < sc->len && is_name((unsigned char)sc->s[j])) {
            j++;
          }

          varname = strndup(sc->s + sc->i, (size_t)(j - sc->i));
          ok = lcl_word_add_var(w, varname);
          free(varname);

          if (!ok) {
            return -1;
          }

          sc->i = j;
          start = sc->i;
        } else {
          if (!lcl_word_add_lit(w, "$", 1)) {
            return 1;
          }

          start = sc->i;
        }
      }

      continue;
    }

    if (c == '[') {
      if (sc->i > start) {
        if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
          return -1;
        }
      }

      {
        long depth = 1;
        long begin = ++sc->i;
        lcl_program *sub;

        while (sc->i < sc->len) {
          char d = sc->s[sc->i++];

          if (d == '\n') {
            sc->line++;
          }
          else if (d == '[') {
            depth++;
          }
          else if (d == ']') {
            depth--;

            if (!depth) break;
          }
          else if (d == '{') {
            long k = 1;

            while (sc->i < sc->len && k) {
              char e = sc->s[sc->i++];

              if (e == '{') {
                k++;
              }
              else if (e == '}') {
                k--;
              }
              else if (e == '\n') {
                sc->line++;
              }
            }

            if (k) return -1;
          }
          else if (d == '"') {
            while (sc->i < sc->len) {
              char e = sc->s[sc->line++];

              if (e == '"') break;
              if (e == '\\' && sc->i < sc->len) {
                sc->i++;
              } else if (e == '\n') {
                sc->line++;
              }
            }
          }
        }

        if (depth) return -1;

        {
          char *subsrc = strndup(sc->s + begin, (size_t)(sc->i - begin - 1));
          sub = lcl_program_compile(subsrc, NULL);
          free(subsrc);
        }

        if (!sub) return -1;

        if (!lcl_word_add_sub(w, sub)) {
          lcl_program_free(sub);
          return -1;
        }

        start = sc->i;
      }

      continue;
    }

    if (c == '"') {
      if (in_quotes) {
        if (sc->i > start) {
          if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
            return -1;
          }
        }

        sc->i++;
        in_quotes = 0;
        start = sc->i;
        break;
      } else {
        sc->i++;
        in_quotes = 1;
        start = sc->i;
        continue;
      }
    }

    if (c == '\\') {
      if (sc->i + 1 < sc->len && sc->s[sc->i + 1] == '\n') {
        if (sc->i > start) {
          if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
            return -1;
          }
        }

        sc->i += 2;
        sc->line++;
        start = sc->i;
        continue;
      }
      /* else copy backslash literally */
    }

    if (c == '\n') {
      sc->line++;
    }

    sc->i++;
  }

  if (sc->i > start) {
    if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
      return -1;
    }
  }

  if (in_quotes) return -1;

  return (w->np > 0) ? 1 : 0;
}

int lcl_scan_parse_command(lcl_scan *sc, lcl_command *cmd) {
  int got = 0;
  cmd->argc = 0;
  cmd->line = (int)sc->line;

  for (;;) {
    skip_cmd_ws_and_comments(sc);

    if (sc->i >= sc->len) return 0;

    if (sc->s[sc->i] == ';') {
      sc->i++;
      sc->at_cmd_start = 1;
      continue;
    }

    if (sc->s[sc->i] == '\n') {
      sc->i++;
      sc->line++;
      sc->at_cmd_start = 1;
      continue;
    }

    break;
  }

  cmd->argc = 0;
  cmd->cap  = 0;
  cmd->w    = NULL;
  cmd->line = (int)sc->line;

  sc->at_cmd_start = 0;

  for (;;) {
    lcl_word w = {0};

    if (sc->i >= sc->len) {
      break;
    }

    if (sc->s[sc->i] == ';') {
      sc->i++;
      sc->at_cmd_start = 1;
      break;
    }

    if (sc->s[sc->i] == '\n') {
      sc->i++;
      sc->line++;
      sc->at_cmd_start = 1;
      break;
    }

    skip_intra_ws(sc);

    if (sc->i >= sc->len) {
      break;
    }

    if (sc->s[sc->i] == ';' || sc->s[sc->i] == '\n') {
      continue;
    }

    if (lcl_scan_word(sc, &w) < 0) {
      return -1;
    }

    if (w.np == 0) {
      break;
    }

    if (!lcl_command_push_word(cmd, &w))  {
      return -1;
    }

    got = 1;
  }

  sc->at_cmd_start = 1;
  return got ? 1 : 0;
}
