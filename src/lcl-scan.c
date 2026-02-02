#include <ctype.h>
#include <memory.h>
#include <string.h>

#include "lcl-lex.h"
#include "str-compat.h"

static void skip_cmd_ws_and_comments(lcl_scan *sc) {
  while (sc->i < sc->len) {
    char c = sc->s[sc->i];

    if (c == ' ' || c == '\t' || c == '\r') {
      sc->i++;
      continue;
    }

    if (c == '\n') {
      sc->i++;
      sc->line++;
      continue;
    }

    if (c == ';' && sc->i + 1 < sc->len && sc->s[sc->i + 1] == ';') {
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

    if (c == '\\' && sc->i + 1 < sc->len && sc->s[sc->i + 1] == '\n') {
      sc->i += 2;
      sc->line++;
      continue;
    }

    /* ;; comment to end of line */
    if (c == ';' && sc->i + 1 < sc->len && sc->s[sc->i + 1] == ';') {
      while (sc->i < sc->len && sc->s[sc->i] != '\n') {
        sc->i++;
      }
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

void lcl_scan_init_bytes(lcl_scan *sc, const char *src, size_t len) {
  sc->s = src;
  sc->i = 0;
  sc->len = (long)len;
  sc->line = 1;
  sc->at_cmd_start = 1;
}

int lcl_scan_word(lcl_scan *sc, lcl_word *w) {
  int in_quotes = 0;
  long start;

  /* Check for @ expand prefix */
  if (sc->i < sc->len && sc->s[sc->i] == '@') {
    w->expand = 1;
    sc->i++;
  }

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

  /* () list literal - desugars to [list ...] */
  if (sc->i < sc->len && sc->s[sc->i] == '(') {
    long depth = 1;
    long begin = ++sc->i;
    lcl_program *sub;
    char *subsrc;
    size_t content_len;

    while (sc->i < sc->len) {
      char c = sc->s[sc->i++];

      if (c == '(') {
        depth++;
      } else if (c == ')') {
        depth--;

        if (!depth) {
          break;
        }
      } else if (c == '\n') {
        sc->line++;
      } else if (c == '{') {
        long k = 1;

        while (sc->i < sc->len && k) {
          char e = sc->s[sc->i++];

          if (e == '{') {
            k++;
          } else if (e == '}') {
            k--;
          } else if (e == '\n') {
            sc->line++;
          }
        }

        if (k) {
          return -1;
        }
      } else if (c == '"') {
        /* Skip quoted strings */
        while (sc->i < sc->len) {
          char e = sc->s[sc->i++];

          if (e == '"') {
            break;
          }
          if (e == '\\' && sc->i < sc->len) {
            sc->i++;
          } else if (e == '\n') {
            sc->line++;
          }
        }
      }
    }

    if (depth) {
      return -1;
    }

    content_len = (size_t)(sc->i - begin - 1);
    subsrc = (char *)malloc(5 + content_len + 1); /* "list " + content + NUL */

    if (!subsrc) {
      return -1;
    }

    memcpy(subsrc, "list ", 5);
    memcpy(subsrc + 5, sc->s + begin, content_len);
    subsrc[5 + content_len] = '\0';

    /* Replace newlines with spaces to prevent command separation,
     * but only at top level - not inside {} blocks (e.g., lambda bodies) */
    {
      size_t j;
      int brace_depth = 0;
      for (j = 5; j < 5 + content_len; j++) {
        if (subsrc[j] == '{') {
          brace_depth++;
        } else if (subsrc[j] == '}') {
          brace_depth--;
        } else if (subsrc[j] == '\n' && brace_depth == 0) {
          subsrc[j] = ' ';
        }
      }
    }

    sub = lcl_program_compile(subsrc, NULL);
    free(subsrc);

    if (!sub) {
      return -1;
    }

    if (!lcl_word_add_sub(w, sub)) {
      lcl_program_free(sub);
      return -1;
    }

    return 1;
  }

  /* #{} dict literal - desugars to [dict ...] */
  if (sc->i < sc->len && sc->s[sc->i] == '#' && sc->i + 1 < sc->len &&
      sc->s[sc->i + 1] == '{') {
    long depth = 1;
    long begin;
    lcl_program *sub;
    char *subsrc;
    size_t content_len;

    sc->i += 2;
    begin = sc->i;

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
      } else if (c == '"') {
        while (sc->i < sc->len) {
          char e = sc->s[sc->i++];

          if (e == '"') {
            break;
          }
          if (e == '\\' && sc->i < sc->len) {
            sc->i++;
          } else if (e == '\n') {
            sc->line++;
          }
        }
      } else if (c == '(') {
        long k = 1;

        while (sc->i < sc->len && k) {
          char e = sc->s[sc->i++];

          if (e == '(') {
            k++;
          } else if (e == ')') {
            k--;
          } else if (e == '\n') {
            sc->line++;
          }
        }

        if (k) {
          return -1;
        }
      }
    }

    if (depth) {
      return -1;
    }

    content_len = (size_t)(sc->i - begin - 1);
    subsrc = (char *)malloc(5 + content_len + 1); /* "dict " + content + NUL */

    if (!subsrc) {
      return -1;
    }

    memcpy(subsrc, "dict ", 5);
    memcpy(subsrc + 5, sc->s + begin, content_len);
    subsrc[5 + content_len] = '\0';

    /* Replace newlines with spaces to prevent command separation,
     * but only at top level - not inside {} blocks (e.g., lambda bodies) */
    {
      size_t j;
      int brace_depth = 0;
      for (j = 5; j < 5 + content_len; j++) {
        if (subsrc[j] == '{') {
          brace_depth++;
        } else if (subsrc[j] == '}') {
          brace_depth--;
        } else if (subsrc[j] == '\n' && brace_depth == 0) {
          subsrc[j] = ' ';
        }
      }
    }

    sub = lcl_program_compile(subsrc, NULL);
    free(subsrc);

    if (!sub) {
      return -1;
    }

    if (!lcl_word_add_sub(w, sub)) {
      lcl_program_free(sub);
      return -1;
    }

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

    if (!in_quotes &&
        (c == ' ' || c == '\t' || c == '\r' || c == ';' || c == '\n')) {
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
          }
          j++;
        }

        if (j >= sc->len) {
          return -1;
        }
        if (j == sc->i) {
          return -1;
        }

        {
          size_t n = (size_t)(j - sc->i);
          char *nm = (char *)malloc(n + 1);

          if (!nm) {
            return -1;
          }

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

        if (j < sc->len &&
            (isalpha((unsigned char)sc->s[j]) || sc->s[j] == '_')) {
          char *varname;
          int ok;

          j++;

          while (j < sc->len) {
            unsigned char ch = (unsigned char)sc->s[j];

            /* Bugfix:
             * We were parsing :$foo: to do lookup a variable named $foo:
             * Only include colon if it's part of :: namespace separator
             */
            if (ch == ':') {
              if (j + 1 < sc->len && sc->s[j + 1] == ':') {
                j += 2;
                continue;
              }
              break;
            }

            if (ch != '_' && !isalnum(ch)) {
              break;
            }
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
          } else if (d == '[') {
            depth++;
          } else if (d == ']') {
            depth--;

            if (!depth) {
              break;
            }
          } else if (d == '{') {
            long k = 1;

            while (sc->i < sc->len && k) {
              char e = sc->s[sc->i++];

              if (e == '{') {
                k++;
              } else if (e == '}') {
                k--;
              } else if (e == '\n') {
                sc->line++;
              }
            }

            if (k) {
              return -1;
            }
          } else if (d == '"') {
            while (sc->i < sc->len) {
              char e = sc->s[sc->i++];

              if (e == '"') {
                break;
              }
              if (e == '\\' && sc->i < sc->len) {
                sc->i++;
              } else if (e == '\n') {
                sc->line++;
              }
            }
          }
        }

        if (depth) {
          return -1;
        }

        {
          size_t content_len = (size_t)(sc->i - begin - 1);
          char *subsrc = strndup(sc->s + begin, content_len);

          /* Replace newlines and semicolons with spaces to prevent command
           * separation. Inside [...], there is exactly one command, so \n and ;
           * are ordinary whitespace at the TOP LEVEL only. We must respect
           * quoting: don't modify content inside {...} braces or "..." quotes.
           * Handle backslash-newline continuation by removing both chars. */
          {
            size_t j, k;
            int brace_depth = 0;
            int in_dquotes = 0;
            int bracket_depth = 0;
            for (j = 0, k = 0; j < content_len; j++) {
              char ch = subsrc[j];

              if (ch == '\\' && j + 1 < content_len) {
                char next = subsrc[j + 1];
                if (next == '\n' && brace_depth == 0 && !in_dquotes &&
                    bracket_depth == 0) {
                  j++;
                  continue;
                }
                subsrc[k++] = ch;
                j++;
                subsrc[k++] = next;
                continue;
              }

              if (ch == '{' && !in_dquotes) {
                brace_depth++;
              } else if (ch == '}' && !in_dquotes && brace_depth > 0) {
                brace_depth--;
              } else if (ch == '"' && brace_depth == 0) {
                in_dquotes = !in_dquotes;
              } else if (ch == '[' && brace_depth == 0 && !in_dquotes) {
                bracket_depth++;
              } else if (ch == ']' && brace_depth == 0 && !in_dquotes &&
                         bracket_depth > 0) {
                bracket_depth--;
              }

              if ((ch == '\n' || ch == ';') && brace_depth == 0 &&
                  !in_dquotes && bracket_depth == 0) {
                subsrc[k++] = ' ';
              } else {
                subsrc[k++] = ch;
              }
            }
            subsrc[k] = '\0';
          }

          sub = lcl_program_compile(subsrc, NULL);
          free(subsrc);
        }

        if (!sub) {
          return -1;
        }

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
      if (sc->i + 1 < sc->len) {
        char next = sc->s[sc->i + 1];
        char esc_char;
        int is_escape = 1;

        if (sc->i > start) {
          if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
            return -1;
          }
        }

        if (next == '\n') {
          sc->i += 2;
          sc->line++;
          start = sc->i;
          continue;
        }

        switch (next) {
        case 'n': esc_char = '\n'; break;
        case 't': esc_char = '\t'; break;
        case 'r': esc_char = '\r'; break;
        case '\\': esc_char = '\\'; break;
        case '"': esc_char = '"'; break;
        case '$': esc_char = '$'; break;
        case '[': esc_char = '['; break;
        case ']': esc_char = ']'; break;
        case '{': esc_char = '{'; break;
        case '}': esc_char = '}'; break;
        default:
          is_escape = 0;
          sc->i++;
          start = sc->i;
          continue;
        }

        if (is_escape) {
          if (!lcl_word_add_lit(w, &esc_char, 1)) {
            return -1;
          }
          sc->i += 2;
          start = sc->i;
          continue;
        }
      }
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

  if (in_quotes) {
    return -1;
  }

  /* Return 1 if we have content OR if it was a quoted empty string "" */
  return (w->np > 0 || w->quoted) ? 1 : 0;
}

int lcl_scan_parse_command(lcl_scan *sc, lcl_command *cmd) {
  int got = 0;
  cmd->argc = 0;
  cmd->line = (int)sc->line;

  for (;;) {
    skip_cmd_ws_and_comments(sc);

    if (sc->i >= sc->len) {
      return 0;
    }

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
  cmd->cap = 0;
  cmd->w = NULL;
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

    /* Check for no word - but empty quoted string "" is valid */
    if (w.np == 0 && !w.quoted) {
      break;
    }

    if (!lcl_command_push_word(cmd, &w)) {
      return -1;
    }

    got = 1;
  }

  sc->at_cmd_start = 1;
  return got ? 1 : 0;
}
