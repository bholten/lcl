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

    if (c == ';' && sc->i + 1 < sc->len && sc->s[sc->i + 1] == ';') {
      while (sc->i < sc->len && sc->s[sc->i] != '\n') {
        sc->i++;
      }
      continue;
    }

    break;
  }
}

/* Refactor:
 *
 * skip_balanced -- advance sc->i past a balanced open_ch/close_ch
 * pair.
 *
 * sc->i must point to the character AFTER the opening delimiter.
 *
 * On success sc->i points one past the closing delimiter; returns 0.
 *
 * On unmatched delimiter returns -1.
 */
static int skip_balanced(lcl_scan *sc, char open_ch, char close_ch) {
  long depth = 1;

  while (sc->i < sc->len) {
    char c = sc->s[sc->i++];

    if (c == '\\' && sc->i < sc->len) {
      if (sc->s[sc->i] == '\n') {
        sc->line++;
      }

      sc->i++;
      continue;
    }

    if (c == open_ch) {
      depth++;
    } else if (c == close_ch) {
      depth--;

      if (!depth) {
        return 0;
      }
    } else if (c == '\n') {
      sc->line++;
    } else if (c == '{' && open_ch != '{') {
      long k = 1;

      while (sc->i < sc->len && k) {
        char e = sc->s[sc->i++];

        if (e == '\\' && sc->i < sc->len) {
          if (sc->s[sc->i] == '\n') {
            sc->line++;
          }
          sc->i++;
        } else if (e == '{') {
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
    } else if (c == '(' && open_ch != '(') {
      if (skip_balanced(sc, '(', ')') != 0) {
        return -1;
      }
    } else if (c == '[' && open_ch != '[') {
      if (skip_balanced(sc, '[', ']') != 0) {
        return -1;
      }
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
    }
  }

  /* unmatched */
  return -1;
}

/* Refactor:
 *
 * normalize_separators -- replace top-level \n and ; with spaces in-place.
 *
 * Tracks brace_depth, in_dquotes, bracket_depth, paren_depth.
 * Handles \\\n continuation by removing both chars.
 *
 * Returns new length after normalization.
 */
static size_t normalize_separators(char *buf, size_t len) {
  size_t j, k;
  int brace_depth = 0;
  int in_dquotes = 0;
  int bracket_depth = 0;
  int paren_depth = 0;

  for (j = 0, k = 0; j < len; j++) {
    char ch = buf[j];

    if (ch == '\\' && j + 1 < len) {
      char next = buf[j + 1];

      if (next == '\n' && brace_depth == 0 && !in_dquotes &&
          bracket_depth == 0 && paren_depth == 0) {
        j++;
        continue;
      }

      buf[k++] = ch;
      j++;
      buf[k++] = next;
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
    } else if (ch == '(' && brace_depth == 0 && !in_dquotes) {
      paren_depth++;
    } else if (ch == ')' && brace_depth == 0 && !in_dquotes &&
               paren_depth > 0) {
      paren_depth--;
    }

    /* ;; comment: skip to next \n (which will become a space) */
    if (ch == ';' && j + 1 < len && buf[j + 1] == ';' && brace_depth == 0 &&
        !in_dquotes && bracket_depth == 0 && paren_depth == 0) {

      while (j < len && buf[j] != '\n') {
        j++;
      }

      if (j < len) {
        /* \n at top level becomes a space */
        buf[k++] = ' ';
      }

      continue;
    }

    if ((ch == '\n' || ch == ';') && brace_depth == 0 && !in_dquotes &&
        bracket_depth == 0 && paren_depth == 0) {
      buf[k++] = ' ';
    } else {
      buf[k++] = ch;
    }
  }

  buf[k] = '\0';
  return k;
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

      if (c == '\\' && sc->i < sc->len) {
        if (sc->s[sc->i] == '\n') {
          sc->line++;
        }
        sc->i++;
        continue;
      }

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

    /* Pre-compile braced content as a program so the upvalue scanner
     * can see variables inside code bodies (eval, foreach, while, etc.)
     * and special forms can skip runtime compilation. If the content
     * isn't valid code, compiled stays NULL and that's fine. */
    w->compiled = lcl_program_compile(w->wp[0].as.lit.s, "<braced>");

    return 1;
  }

  /* () list literal - desugars to [list ...] */
  if (sc->i < sc->len && sc->s[sc->i] == '(') {
    long begin;
    lcl_program *sub;
    char *subsrc;
    size_t content_len;

    sc->i++;
    begin = sc->i;

    if (skip_balanced(sc, '(', ')') != 0) {
      return -1;
    }

    content_len = (size_t)(sc->i - begin - 1);
    subsrc = (char *)malloc(5 + content_len + 1);

    if (!subsrc) {
      return -1;
    }

    memcpy(subsrc, "list ", 5);
    memcpy(subsrc + 5, sc->s + begin, content_len);
    subsrc[5 + content_len] = '\0';

    normalize_separators(subsrc + 5, content_len);

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
    long begin;
    lcl_program *sub;
    char *subsrc;
    size_t content_len;

    sc->i += 2;
    begin = sc->i;

    if (skip_balanced(sc, '{', '}') != 0) {
      return -1;
    }

    content_len = (size_t)(sc->i - begin - 1);
    subsrc = (char *)malloc(5 + content_len + 1);

    if (!subsrc) {
      return -1;
    }

    memcpy(subsrc, "dict ", 5);
    memcpy(subsrc + 5, sc->s + begin, content_len);
    subsrc[5 + content_len] = '\0';

    normalize_separators(subsrc + 5, content_len);

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
            return -1;
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
        long begin;
        lcl_program *sub;
        size_t content_len;
        char *subsrc;

        sc->i++;
        begin = sc->i;

        if (skip_balanced(sc, '[', ']') != 0) {
          return -1;
        }

        content_len = (size_t)(sc->i - begin - 1);
        subsrc = strndup(sc->s + begin, content_len);
        if (!subsrc) {
          return -1;
        }

        normalize_separators(subsrc, content_len);

        sub = lcl_program_compile(subsrc, NULL);
        free(subsrc);

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

        if (next == 'x') {
          long j = sc->i + 2;
          unsigned val = 0;
          int ndig = 0;

          while (ndig < 2 && j < sc->len) {
            unsigned char ch = (unsigned char)sc->s[j];

            if (ch >= '0' && ch <= '9') {
              val = val * 16 + (ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
              val = val * 16 + (ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
              val = val * 16 + (ch - 'A' + 10);
            } else {
              break;
            }

            ndig++;
            j++;
          }

          if (ndig == 2) {
            esc_char = (char)val;

            if (!lcl_word_add_lit(w, &esc_char, 1)) {
              return -1;
            }

            sc->i = j;
            start = sc->i;
            continue;
          }
        }

        if (next >= '0' && next <= '7') {
          long j = sc->i + 1;
          unsigned val = 0;
          int ndig = 0;

          while (ndig < 3 && j < sc->len) {
            unsigned char ch = (unsigned char)sc->s[j];

            if (ch >= '0' && ch <= '7') {
              val = val * 8 + (ch - '0');
            } else {
              break;
            }
            ndig++;
            j++;
          }

          esc_char = (char)(val & 0xFF);

          if (!lcl_word_add_lit(w, &esc_char, 1)) {
            return -1;
          }

          sc->i = j;
          start = sc->i;
          continue;
        }

        switch (next) {
        case 'a': esc_char = '\a'; break;
        case 'b': esc_char = '\b'; break;
        case 'f': esc_char = '\f'; break;
        case 'n': esc_char = '\n'; break;
        case 't': esc_char = '\t'; break;
        case 'r': esc_char = '\r'; break;
        case 'v': esc_char = '\v'; break;
        case '\\': esc_char = '\\'; break;
        case '"': esc_char = '"'; break;
        case '$': esc_char = '$'; break;
        case '[': esc_char = '['; break;
        case ']': esc_char = ']'; break;
        case '{': esc_char = '{'; break;
        case '}': esc_char = '}'; break;
        default:
          if (!lcl_word_add_lit(w, sc->s + sc->i, 2)) {
            return -1;
          }
          sc->i += 2;
          start = sc->i;
          continue;
        }

        if (!lcl_word_add_lit(w, &esc_char, 1)) {
          return -1;
        }

        sc->i += 2;
        start = sc->i;
        continue;
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
      lcl_word_free_contents(&w);
      return -1;
    }

    if (w.np == 0 && !w.quoted) {
      break;
    }

    if (!lcl_command_push_word(cmd, &w)) {
      lcl_word_free_contents(&w);
      return -1;
    }

    got = 1;
  }

  sc->at_cmd_start = 1;
  return got ? 1 : 0;
}
