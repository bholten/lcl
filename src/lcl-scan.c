#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <memory.h>
#include <string.h>

#include "lcl-lex.h"
#include "lcl-values.h"
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

/* Record the first parse failure. Nested/unwinding callers also
 * report, so only the innermost (first) message is kept. Always
 * returns -1 so failure sites can `return scan_fail(...)`. */
static int scan_fail(lcl_scan *sc, const char *msg, long line) {
  if (!sc->err) {
    sc->err = msg;
    sc->err_line = line;
  }

  return -1;
}

/* Same-type nesting (`[[[...]]]`) is depth-counted iteratively below;
 * the only true recursion is alternating types (e.g. `[ ( [ ... ] )
 * ]`). Cap the recursion to a sane depth so adversarial input like
 * `[([([(...)])])]` of depth 10k+ can't blow the C stack — return -1
 * instead. */
#define LCL_SCAN_MAX_NEST 256

static const char *unmatched_msg(char open_ch) {
  switch (open_ch) {
  case '(': return "unmatched '('";
  case '[': return "unmatched '['";
  default: return "unmatched '{'";
  }
}

/* Refactor:
 *
 * skip_balanced -- advance sc->i past a balanced open_ch/close_ch
 * pair.
 *
 * sc->i must point to the character AFTER the opening delimiter.
 *
 * `rec_depth` tracks how many alternating-type nestings deep we are;
 * public callers pass 0.
 *
 * `open_line` is the line the opening delimiter appeared on —
 * unmatched-delimiter errors are attributed there, not to EOF.
 *
 * On success sc->i points one past the closing delimiter; returns 0.
 *
 * On unmatched delimiter or nesting past LCL_SCAN_MAX_NEST returns -1.
 */
static int skip_balanced(lcl_scan *sc, char open_ch, char close_ch,
                         int rec_depth, long open_line) {
  long depth = 1;

  if (rec_depth > LCL_SCAN_MAX_NEST) {
    return scan_fail(sc, "nesting too deep", open_line);
  }

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
      long brace_line = sc->line;

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
        return scan_fail(sc, "unmatched '{'", brace_line);
      }
    } else if (c == '(' && open_ch != '(') {
      if (skip_balanced(sc, '(', ')', rec_depth + 1, sc->line) != 0) {
        return -1;
      }
    } else if (c == '[' && open_ch != '[') {
      if (skip_balanced(sc, '[', ']', rec_depth + 1, sc->line) != 0) {
        return -1;
      }
    } else if (c == '"') {
      long quote_line = sc->line;
      int closed = 0;

      while (sc->i < sc->len) {
        char e = sc->s[sc->i++];

        if (e == '"') {
          closed = 1;
          break;
        }

        if (e == '\\' && sc->i < sc->len) {
          sc->i++;
        } else if (e == '\n') {
          sc->line++;
        }
      }

      if (!closed) {
        return scan_fail(sc, "unmatched '\"'", quote_line);
      }
    }
  }

  return scan_fail(sc, unmatched_msg(open_ch), open_line);
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
  size_t j;
  size_t k;
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
  sc->err = NULL;
  sc->err_line = 0;
}

void lcl_scan_init_bytes(lcl_scan *sc, const char *src, size_t len) {
  sc->s = src;
  sc->i = 0;
  sc->len = (long)len;
  sc->line = 1;
  sc->at_cmd_start = 1;
  sc->err = NULL;
  sc->err_line = 0;
}

static int scan_word_pieces(lcl_scan *sc, lcl_word *w) {
  int in_quotes = 0;
  long quote_line = 0;
  long start;

  if (sc->i < sc->len && sc->s[sc->i] == '@') {
    w->expand = 1;
    sc->i++;
  }

  if (sc->i < sc->len && sc->s[sc->i] == '{') {
    long depth = 1;
    long open_line = sc->line;
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
      return scan_fail(sc, "unmatched '{'", open_line);
    }

    if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start - 1))) {
      return scan_fail(sc, "out of memory", sc->line);
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
    long open_line = sc->line;
    lcl_program *sub;
    char *subsrc;
    size_t content_len;
    lcl_compile_err suberr;

    sc->i++;
    begin = sc->i;

    if (skip_balanced(sc, '(', ')', 0, open_line) != 0) {
      return -1;
    }

    content_len = (size_t)(sc->i - begin - 1);
    subsrc = (char *)malloc(5 + content_len + 1);

    if (!subsrc) {
      return scan_fail(sc, "out of memory", open_line);
    }

    memcpy(subsrc, "list ", 5);
    memcpy(subsrc + 5, sc->s + begin, content_len);
    subsrc[5 + content_len] = '\0';

    normalize_separators(subsrc + 5, content_len);

    sub = lcl_program_compile_ex(subsrc, NULL, &suberr);
    free(subsrc);

    if (!sub) {
      /* The sub-source is a normalized copy, so its line numbers do
       * not map back; attribute the sub's message to the line the
       * literal opened on. */
      return scan_fail(sc, suberr.msg, open_line);
    }

    if (!lcl_word_add_sub(w, sub)) {
      lcl_program_free(sub);
      return scan_fail(sc, "out of memory", open_line);
    }

    return 1;
  }

  /* #{} dict literal - desugars to [dict ...] */
  if (sc->i < sc->len && sc->s[sc->i] == '#' && sc->i + 1 < sc->len &&
      sc->s[sc->i + 1] == '{') {
    long begin;
    long open_line = sc->line;
    lcl_program *sub;
    char *subsrc;
    size_t content_len;
    lcl_compile_err suberr;

    sc->i += 2;
    begin = sc->i;

    if (skip_balanced(sc, '{', '}', 0, open_line) != 0) {
      return -1;
    }

    content_len = (size_t)(sc->i - begin - 1);
    subsrc = (char *)malloc(5 + content_len + 1);

    if (!subsrc) {
      return scan_fail(sc, "out of memory", open_line);
    }

    memcpy(subsrc, "dict ", 5);
    memcpy(subsrc + 5, sc->s + begin, content_len);
    subsrc[5 + content_len] = '\0';

    normalize_separators(subsrc + 5, content_len);

    sub = lcl_program_compile_ex(subsrc, NULL, &suberr);
    free(subsrc);

    if (!sub) {
      return scan_fail(sc, suberr.msg, open_line);
    }

    if (!lcl_word_add_sub(w, sub)) {
      lcl_program_free(sub);
      return scan_fail(sc, "out of memory", open_line);
    }

    return 1;
  }

  if (sc->i < sc->len && sc->s[sc->i] == '"') {
    in_quotes = 1;
    quote_line = sc->line;
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
          return scan_fail(sc, "out of memory", sc->line);
        }
      }

      sc->i++;

      if (sc->i < sc->len && sc->s[sc->i] == '{') {
        long open_line = sc->line;
        long j = ++sc->i;

        while (j < sc->len && sc->s[j] != '}') {
          if (sc->s[j] == '\n') {
            sc->line++;
          }
          j++;
        }

        if (j >= sc->len) {
          return scan_fail(sc, "unmatched '${'", open_line);
        }

        if (j == sc->i) {
          return scan_fail(sc, "empty variable name in '${}'", open_line);
        }

        {
          size_t n = (size_t)(j - sc->i);
          char *nm = (char *)malloc(n + 1);

          if (!nm) {
            return scan_fail(sc, "out of memory", sc->line);
          }

          memcpy(nm, sc->s + sc->i, n);
          nm[n] = '\0';

          if (!lcl_word_add_var(w, nm)) {
            free(nm);
            return scan_fail(sc, "out of memory", sc->line);
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
                /* Bugfix: require an identifier char (alpha or `_`)
                 * to start the segment after `::`. Without this,
                 * `$foo::` silently resolves to `foo` (env_get_value
                 * splits on `::`, then the trailing empty segment is
                 * skipped), and `$foo::1bad` is accepted by the
                 * scanner only to fail with a generic "undefined
                 * variable" at runtime. Reject both at parse time. */
                if (j + 2 >= sc->len ||
                    (!isalpha((unsigned char)sc->s[j + 2]) &&
                     sc->s[j + 2] != '_')) {
                  return scan_fail(sc, "expected identifier after '::'",
                                   sc->line);
                }

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

          if (!varname) {
            return scan_fail(sc, "out of memory", sc->line);
          }

          ok = lcl_word_add_var(w, varname);
          free(varname);

          if (!ok) {
            return scan_fail(sc, "out of memory", sc->line);
          }

          sc->i = j;
          start = sc->i;
        } else {
          if (!lcl_word_add_lit(w, "$", 1)) {
            return scan_fail(sc, "out of memory", sc->line);
          }

          start = sc->i;
        }
      }

      continue;
    }

    if (c == '[') {
      if (sc->i > start) {
        if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
          return scan_fail(sc, "out of memory", sc->line);
        }
      }

      {
        long begin;
        long open_line = sc->line;
        lcl_program *sub;
        size_t content_len;
        char *subsrc;
        lcl_compile_err suberr;

        sc->i++;
        begin = sc->i;

        if (skip_balanced(sc, '[', ']', 0, open_line) != 0) {
          return -1;
        }

        content_len = (size_t)(sc->i - begin - 1);
        /* Fuzz: Not strndup; the span may contain NUL bytes (the
         * _bytes entry points admit them), and strndup would stop
         * early, leaving the buffer shorter than content_len. */
        subsrc = malloc(content_len + 1);

        if (!subsrc) {
          return scan_fail(sc, "out of memory", open_line);
        }

        memcpy(subsrc, sc->s + begin, content_len);
        subsrc[content_len] = '\0';

        content_len = normalize_separators(subsrc, content_len);

        sub = lcl_program_compile_bytes_ex(subsrc, content_len, NULL, &suberr);
        free(subsrc);

        if (!sub) {
          return scan_fail(sc, suberr.msg, open_line);
        }

        if (!lcl_word_add_sub(w, sub)) {
          lcl_program_free(sub);
          return scan_fail(sc, "out of memory", open_line);
        }

        start = sc->i;
      }

      continue;
    }

    if (c == '"') {
      if (in_quotes) {
        if (sc->i > start) {
          if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start))) {
            return scan_fail(sc, "out of memory", sc->line);
          }
        }

        sc->i++;
        in_quotes = 0;
        start = sc->i;
        break;
      } else {
        sc->i++;
        in_quotes = 1;
        quote_line = sc->line;
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
            return scan_fail(sc, "out of memory", sc->line);
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
              val = val * 16u + (unsigned)(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
              val = val * 16u + (unsigned)(ch - 'a') + 10u;
            } else if (ch >= 'A' && ch <= 'F') {
              val = val * 16u + (unsigned)(ch - 'A') + 10u;
            } else {
              break;
            }

            ndig++;
            j++;
          }

          if (ndig == 2) {
            esc_char = (char)val;

            if (!lcl_word_add_lit(w, &esc_char, 1)) {
              return scan_fail(sc, "out of memory", sc->line);
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
            return scan_fail(sc, "out of memory", sc->line);
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
            return scan_fail(sc, "out of memory", sc->line);
          }
          sc->i += 2;
          start = sc->i;
          continue;
        }

        if (!lcl_word_add_lit(w, &esc_char, 1)) {
          return scan_fail(sc, "out of memory", sc->line);
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
      return scan_fail(sc, "out of memory", sc->line);
    }
  }

  if (in_quotes) {
    return scan_fail(sc, "unmatched '\"'", quote_line);
  }

  return (w->np > 0 || w->quoted) ? 1 : 0;
}

static int scan_type_numeric_word(lcl_scan *sc, lcl_word *w) {
  const char *s;
  size_t n;
  long start;

  if (w->np != 1 || w->quoted || w->braced ||
      w->wp[0].kind != LCL_WP_LIT) {
    return 1;
  }

  s = w->wp[0].as.lit.s;
  n = w->wp[0].as.lit.n;
  start = w->src_start + (w->expand ? 1 : 0);

  if (w->src_end - start != (long)n || memcmp(sc->s + start, s, n) != 0) {
    return 1;
  }

  switch (lcl_num_literal_classify(s, n)) {
  case LCL_NUM_INT: {
    char *endptr;
    long v;

    errno = 0;
    v = strtol(s, &endptr, 10);

    if (errno == ERANGE) {
      return scan_fail(sc, "integer literal out of range", sc->line);
    }

    w->typed = lcl_int_new(v);
    break;
  }
  case LCL_NUM_FLOAT: {
    double d = 0.0;

    errno = 0;

    if (lcl_parse_double_c(s, &d) != n) {
      return 1;
    }

    if (errno == ERANGE && (d == HUGE_VAL || d == -HUGE_VAL)) {
      return scan_fail(sc, "float literal out of range", sc->line);
    }

    w->typed = lcl_float_new(d);
    break;
  }
  case LCL_NUM_NONE: return 1;
  }

  if (!w->typed) {
    return scan_fail(sc, "out of memory", sc->line);
  }

  return 1;
}

/* On entry sc->i sits on the word's first byte (any @ prefix
 * included); on success it sits one past the last. Stamping the span
 * here covers every exit path of the piece scanner. */
int lcl_scan_word(lcl_scan *sc, lcl_word *w) {
  int rc;

  w->src_start = sc->i;
  rc = scan_word_pieces(sc, w);
  w->src_end = sc->i;

  return rc;
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
      /* Bugfix: `lcl_scan_word` returns an empty word for EOF,
       * command separators, or an unmatched `]` (it stops without
       * consuming the bracket. EOF/separators are handled by the
       * checks above, so a leftover `]` here is a top-level unmatched
       * bracket — a parse error rather than a silent truncation of
       * the program. */
      if (sc->i < sc->len && sc->s[sc->i] == ']') {
        return scan_fail(sc, "unmatched ']'", sc->line);
      }

      break;
    }

    if (scan_type_numeric_word(sc, &w) < 0) {
      lcl_word_free_contents(&w);
      return -1;
    }

    if (!lcl_command_push_word(cmd, &w)) {
      lcl_word_free_contents(&w);
      return scan_fail(sc, "out of memory", sc->line);
    }

    got = 1;
  }

  sc->at_cmd_start = 1;
  return got ? 1 : 0;
}
