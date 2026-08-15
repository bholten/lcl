#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <memory.h>
#include <string.h>

#include "lcl-lex.h"
#include "lcl-name.h"
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

/* Two recursion paths share the LCL_SCAN_MAX_NEST cap (lcl-lex.h):
 * skip_balanced below recurses on alternating delimiter types within
 * one scan (`[([(...)])]`), and the subcompile sites in
 * scan_word_pieces recurse through lcl_program_compile_depth once per
 * nesting level (`[[[...]]]`, `(((...)))`, braced bodies). Either
 * path past the cap is a "nesting too deep" compile error instead of
 * a C stack overflow. */

static const char *unmatched_msg(char open_ch) {
  switch (open_ch) {
  case '(': return "unmatched '('";
  case '[': return "unmatched '['";
  default: return "unmatched '{'";
  }
}

/* Advance sc->i past the matching '}' of a brace literal whose '{'
 * has already been consumed. Brace-literal grammar: nesting depth and
 * backslash escapes only — quotes and other delimiters are ordinary
 * bytes. The single definition of this grammar; the braced-word
 * scanner, skip_balanced's opaque-brace case, and the exported span
 * helper all use it. */
static int skip_brace_literal(lcl_scan *sc, long open_line) {
  long depth = 1;

  while (sc->i < sc->len) {
    char c = sc->s[sc->i++];

    if (c == '\\' && sc->i < sc->len) {
      if (sc->s[sc->i] == '\n') {
        sc->line++;
      }
      sc->i++;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;

      if (!depth) {
        return 0;
      }
    } else if (c == '\n') {
      sc->line++;
    }
  }

  return scan_fail(sc, "unmatched '{'", open_line);
}

/* Advance sc->i past the closing '"' of a double-quoted run whose
 * opening quote has already been consumed. Escapes consume the next
 * byte; newlines are counted. */
static int skip_dquote(lcl_scan *sc, long quote_line) {
  while (sc->i < sc->len) {
    char c = sc->s[sc->i++];

    if (c == '"') {
      return 0;
    }

    if (c == '\\' && sc->i < sc->len) {
      if (sc->s[sc->i] == '\n') {
        sc->line++;
      }
      sc->i++;
    } else if (c == '\n') {
      sc->line++;
    }
  }

  return scan_fail(sc, "unmatched '\"'", quote_line);
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
      if (skip_brace_literal(sc, sc->line) != 0) {
        return -1;
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
      if (skip_dquote(sc, sc->line) != 0) {
        return -1;
      }
    }
  }

  return scan_fail(sc, unmatched_msg(open_ch), open_line);
}

void lcl_scan_init(lcl_scan *sc, const char *src) {
  sc->s = src;
  sc->i = 0;
  sc->len = (long)strlen(src);
  sc->line = 1;
  sc->at_cmd_start = 1;
  sc->err = NULL;
  sc->err_line = 0;
  sc->nest = 0;
  sc->sep_as_ws = 0;
}

void lcl_scan_init_bytes(lcl_scan *sc, const char *src, size_t len) {
  sc->s = src;
  sc->i = 0;
  sc->len = (long)len;
  sc->line = 1;
  sc->at_cmd_start = 1;
  sc->err = NULL;
  sc->err_line = 0;
  sc->nest = 0;
  sc->sep_as_ws = 0;
}

/* Compile the span between a just-consumed opening delimiter and its
 * balanced closer into a SUBCMD piece. `prefix` is the desugaring
 * command head ("list " for `(...)`, "dict " for `#{...}`, "" for a
 * plain `[...]` subcommand). Top-level newlines/`;` in the span
 * separate *words*, not commands, so the copy is normalized before
 * compiling. Compiles by byte length, so embedded NUL bytes reach the
 * subcompiler instead of truncating the program. */
static int scan_sub_literal(lcl_scan *sc, lcl_word *w, const char *prefix,
                            char open_ch, char close_ch) {
  long begin = sc->i;
  long open_line = sc->line;
  lcl_program *sub;
  char *subsrc;
  size_t plen = strlen(prefix);
  size_t content_len;
  lcl_compile_err suberr;

  if (skip_balanced(sc, open_ch, close_ch, 0, open_line) != 0) {
    return -1;
  }

  content_len = (size_t)(sc->i - begin - 1);
  subsrc = (char *)malloc(plen + content_len + 1);

  if (!subsrc) {
    return scan_fail(sc, "out of memory", open_line);
  }

  memcpy(subsrc, prefix, plen);
  memcpy(subsrc + plen, sc->s + begin, content_len);
  subsrc[plen + content_len] = '\0';

  sub = lcl_program_compile_span(subsrc, plen + content_len, &suberr,
                                 sc->nest + 1, open_line);
  free(subsrc);

  if (!sub) {
    return scan_fail(sc, suberr.msg, suberr.line ? suberr.line : open_line);
  }

  if (!lcl_word_add_sub(w, sub)) {
    lcl_program_free(sub);
    return scan_fail(sc, "out of memory", open_line);
  }

  return 1;
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
    long open_line = sc->line;
    sc->i++;
    start = sc->i;

    if (skip_brace_literal(sc, open_line) != 0) {
      return -1;
    }

    if (!lcl_word_add_lit(w, sc->s + start, (size_t)(sc->i - start - 1))) {
      return scan_fail(sc, "out of memory", sc->line);
    }

    w->braced = 1;
    w->compiled =
        lcl_program_compile_depth(w->wp[0].as.lit.s, strlen(w->wp[0].as.lit.s),
                                  "<braced>", NULL, sc->nest + 1);

    return 1;
  }

  /* () list literal - desugars to [list ...] */
  if (sc->i < sc->len && sc->s[sc->i] == '(') {
    sc->i++;
    return scan_sub_literal(sc, w, "list ", '(', ')');
  }

  /* #{} dict literal - desugars to [dict ...] */
  if (sc->i < sc->len && sc->s[sc->i] == '#' && sc->i + 1 < sc->len &&
      sc->s[sc->i + 1] == '{') {
    sc->i += 2;
    return scan_sub_literal(sc, w, "dict ", '{', '}');
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

        /* No newline counting: a newline inside `${...}` fails the
         * grammar check below, reported at the open line. */
        while (j < sc->len && sc->s[j] != '}') {
          j++;
        }

        if (j >= sc->len) {
          return scan_fail(sc, "unmatched '${'", open_line);
        }

        /* `${...}` contents are a variable reference, not
         * arbitrary bytes — validate against the qualname grammar
         * (lcl-name.c), reporting at the open line. */
        {
          const char *bad =
              lcl_name_check_ref(sc->s + sc->i, (size_t)(j - sc->i));

          if (bad) {
            return scan_fail(sc, bad, open_line);
          }
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

        if (j < sc->len && lcl_name_is_start((unsigned char)sc->s[j])) {
          char *varname;
          int ok;

          j++;

          /* an unbraced substitution is exactly one simple name;
           * ':' is never part of it. Qualified lookup is `${a::b}`. */
          while (j < sc->len && lcl_name_is_char((unsigned char)sc->s[j])) {
            j++;
          }

          if (j + 2 < sc->len && sc->s[j] == ':' && sc->s[j + 1] == ':' &&
              lcl_name_is_char((unsigned char)sc->s[j + 2])) {
            return scan_fail(
                sc, "qualified substitutions require braces: ${name::path}",
                sc->line);
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

      sc->i++;

      if (scan_sub_literal(sc, w, "", '[', ']') < 0) {
        return -1;
      }

      start = sc->i;
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

  if (w->np != 1 || w->quoted || w->braced || w->wp[0].kind != LCL_WP_LIT) {
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

/* Byte-span skip helpers (lcl-lex.h): the scanner's delimiter
 * grammars exposed for consumers that walk raw template text (the
 * quasiquote boundary scanner, subst). One grammar definition — a
 * boundary found here is the boundary the compiler will see. */
int lcl_scan_skip_braces_span(const char *s, size_t len, size_t pos,
                              size_t *end) {
  lcl_scan sc;

  lcl_scan_init_bytes(&sc, s, len);
  sc.i = (long)pos;

  if (skip_brace_literal(&sc, 1) != 0) {
    return -1;
  }

  *end = (size_t)sc.i;
  return 0;
}

int lcl_scan_skip_balanced_span(const char *s, size_t len, size_t pos,
                                char open_ch, char close_ch, size_t *end) {
  lcl_scan sc;

  lcl_scan_init_bytes(&sc, s, len);
  sc.i = (long)pos;

  if (skip_balanced(&sc, open_ch, close_ch, 0, 1) != 0) {
    return -1;
  }

  *end = (size_t)sc.i;
  return 0;
}

/* On entry sc->i sits on the word's first byte (any @ prefix
 * included); on success it sits one past the last. Stamping the span
 * here covers every exit path of the piece scanner. */
int lcl_scan_word(lcl_scan *sc, lcl_word *w) {
  int rc;

  w->src_start = sc->i;
  rc = scan_word_pieces(sc, w);
  w->src_end = sc->i;

  /* Bugfix: a word consisting of only the spread prefix consumed the
   * `@` and produced zero pieces, which the empty-word check in
   * lcl_scan_parse_command reads as end-of-input — silently
   * discarding the rest of the program. A spread must have a word
   * attached (`""` counts: an explicit empty word is the caller's
   * business at eval time). */
  if (rc == 0 && w->expand) {
    return scan_fail(sc, "expected a word after '@'", sc->line);
  }

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
      if (sc->sep_as_ws) {
        /* Span mode: `;;` is a comment to end-of-line, a single `;`
         * is word whitespace. */
        if (sc->i + 1 < sc->len && sc->s[sc->i + 1] == ';') {
          while (sc->i < sc->len && sc->s[sc->i] != '\n') {
            sc->i++;
          }
        } else {
          sc->i++;
        }

        continue;
      }

      sc->i++;
      sc->at_cmd_start = 1;
      break;
    }

    if (sc->s[sc->i] == '\n') {
      sc->i++;
      sc->line++;

      if (sc->sep_as_ws) {
        continue;
      }

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
