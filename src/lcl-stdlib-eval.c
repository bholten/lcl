#include "lcl-name.h"
#include "lcl-stdlib-internal.h"

static lcl_return_code s_subst(lcl_interp *interp, int argc,
                               const lcl_word **args, lcl_value **out) {
  lcl_value *input_v = NULL;
  const char *src;
  size_t src_len;
  size_t i;
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;

  if (!lcl_std_chk_argc(interp, "subst", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, input_v, &src) != LCL_OK) {
    lcl_ref_dec(input_v);
    return LCL_RC_ERR;
  }
  src_len = strlen(src);

  for (i = 0; i < src_len;) {
    char c = src[i];

    if (c == '\\' && i + 1 < src_len) {
      char next = src[i + 1];
      char esc;

      switch (next) {
      case 'n': esc = '\n'; break;
      case 't': esc = '\t'; break;
      case 'r': esc = '\r'; break;
      case '\\': esc = '\\'; break;
      case '[': esc = '['; break;
      case ']': esc = ']'; break;
      case '$': esc = '$'; break;
      case '{': esc = '{'; break;
      case '}': esc = '}'; break;
      case '"': esc = '"'; break;
      default:
        if (!lcl_std_buf_append_char(&result, &result_len, &result_cap, '\\')) {
          goto err;
        }
        esc = next;
        break;
      }

      if (!lcl_std_buf_append_char(&result, &result_len, &result_cap, esc)) {
        goto err;
      }

      i += 2;
      continue;
    }

    if (c == '$') {
      i++;

      if (i < src_len && src[i] == '{') {
        size_t start = ++i;

        while (i < src_len && src[i] != '}') {
          i++;
        }

        if (i >= src_len) {
          goto err;
        }

        {
          const char *bad = lcl_name_check_ref(src + start, i - start);

          if (bad) {
            LCL_ERR_MSG(interp, bad);
            goto err;
          }
        }

        {
          size_t name_len = i - start;
          char *name = malloc(name_len + 1);
          lcl_value *val = NULL;
          const char *val_str;

          if (!name) {
            goto err;
          }

          memcpy(name, src + start, name_len);
          name[name_len] = '\0';

          if (lcl_env_get_value(interp, name, &val) != LCL_OK) {
            free(name);
            goto err;
          }

          free(name);

          if (val->type == LCL_CELL) {
            lcl_value *content = NULL;

            if (lcl_cell_get(val, &content) != LCL_OK) {
              lcl_ref_dec(val);
              goto err;
            }

            lcl_ref_dec(val);
            val = content;
          }

          if (lcl_value_to_cstring(interp, val, &val_str) != LCL_OK) {
            lcl_ref_dec(val);
            goto err;
          }

          if (!lcl_std_buf_append(&result, &result_len, &result_cap, val_str,
                                  strlen(val_str))) {
            lcl_ref_dec(val);
            goto err;
          }

          lcl_ref_dec(val);
        }

        i++;
        continue;
      }

      if (i < src_len && lcl_name_is_start((unsigned char)src[i])) {
        size_t start = i;

        i++;

        while (i < src_len && lcl_name_is_char((unsigned char)src[i])) {
          i++;
        }

        if (i + 2 < src_len && src[i] == ':' && src[i + 1] == ':' &&
            lcl_name_is_char((unsigned char)src[i + 2])) {
          LCL_ERR_MSG(interp,
                      "qualified substitutions require braces: ${name::path}");
          goto err;
        }

        {
          size_t name_len = i - start;
          char *name = malloc(name_len + 1);
          lcl_value *val = NULL;
          const char *val_str;

          if (!name) {
            goto err;
          }

          memcpy(name, src + start, name_len);
          name[name_len] = '\0';

          if (lcl_env_get_value(interp, name, &val) != LCL_OK) {
            free(name);
            goto err;
          }

          free(name);

          if (val->type == LCL_CELL) {
            lcl_value *content = NULL;

            if (lcl_cell_get(val, &content) != LCL_OK) {
              lcl_ref_dec(val);
              goto err;
            }

            lcl_ref_dec(val);
            val = content;
          }

          if (lcl_value_to_cstring(interp, val, &val_str) != LCL_OK) {
            lcl_ref_dec(val);
            goto err;
          }

          if (!lcl_std_buf_append(&result, &result_len, &result_cap, val_str,
                                  strlen(val_str))) {
            lcl_ref_dec(val);
            goto err;
          }

          lcl_ref_dec(val);
        }

        continue;
      }

      if (!lcl_std_buf_append_char(&result, &result_len, &result_cap, '$')) {
        goto err;
      }

      continue;
    }

    if (c == '[') {
      size_t start = ++i;
      size_t close_end;

      if (lcl_scan_skip_balanced_span(src, src_len, start, '[', ']',
                                      &close_end) != 0) {
        LCL_ERR_MSG(interp, "subst: unmatched '['");
        goto err;
      }

      i = close_end - 1;

      {
        size_t subcmd_len = i - start;
        char *subcmd_src = malloc(subcmd_len + 1);
        lcl_program *prog;
        lcl_value *subcmd_result = NULL;
        lcl_return_code rc;
        const char *result_str;

        if (!subcmd_src) {
          goto err;
        }

        memcpy(subcmd_src, src + start, subcmd_len);
        subcmd_src[subcmd_len] = '\0';

        {
          char name[256];
          prog = lcl_compile_report(
              interp, subcmd_src,
              lcl_dyn_source_name(interp, "subst", name, sizeof(name)));
        }
        free(subcmd_src);

        if (!prog) {
          goto err;
        }

        if (interp->max_depth && interp->depth >= interp->max_depth) {
          lcl_program_free(prog);
          goto err;
        }

        interp->depth++;

        rc = LCL_RC_OK;

        {
          int j;
          int saved_tail_position = interp->in_tail_position;
          const char *saved_cur_file = interp->cur_file;
          int saved_cur_line = interp->cur_line;
          interp->in_tail_position = 0;

          for (j = 0; j < prog->ncmd; j++) {
            lcl_command *cmd = &prog->cmd[j];

            interp->cur_file = prog->file;
            interp->cur_line = cmd->line;

            if (subcmd_result) {
              lcl_ref_dec(subcmd_result);
              subcmd_result = NULL;
            }

            rc = lcl_call_from_words(interp, cmd, &subcmd_result);

            if (rc != LCL_RC_OK) {
              if (rc != LCL_RC_RETURN) {
                interp->err_line = cmd->line;

                if (interp->err_file_owned && interp->err_file) {
                  free((void *)interp->err_file);
                }

                interp->err_file = prog->file ? strdup(prog->file) : NULL;
                interp->err_file_owned = prog->file ? 1 : 0;
              }

              break;
            }
          }

          interp->in_tail_position = saved_tail_position;
          interp->cur_file = saved_cur_file;
          interp->cur_line = saved_cur_line;
        }

        interp->depth--;
        lcl_program_free(prog);

        if (rc != LCL_RC_OK) {
          if (subcmd_result) {
            lcl_ref_dec(subcmd_result);
          }
          goto err;
        }

        if (subcmd_result) {
          if (lcl_value_to_cstring(interp, subcmd_result, &result_str) !=
              LCL_OK) {
            lcl_ref_dec(subcmd_result);
            goto err;
          }
        } else {
          result_str = "";
        }

        if (!lcl_std_buf_append(&result, &result_len, &result_cap, result_str,
                                strlen(result_str))) {
          if (subcmd_result) {
            lcl_ref_dec(subcmd_result);
          }
          goto err;
        }

        if (subcmd_result) {
          lcl_ref_dec(subcmd_result);
        }
      }

      i++;
      continue;
    }

    if (!lcl_std_buf_append_char(&result, &result_len, &result_cap, c)) {
      goto err;
    }

    i++;
  }

  lcl_ref_dec(input_v);
  *out = lcl_value_new_string(result ? result : "");
  free(result);

  return *out ? LCL_RC_OK : LCL_RC_ERR;

err:
  if (!interp->err_msg) {
    LCL_ERR_MSG(interp, "subst: out of memory");
  }

  lcl_ref_dec(input_v);
  free(result);

  return LCL_RC_ERR;
}

/* quasiquote {template} - template with unquote (,expr) and splice (,@expr)
 *
 * Supports depth tracking for nested quasiquotes:
 * - At depth 1: ,expr evaluates expr and inserts result
 * - At depth > 1: ,expr passes through literally
 * - ,,expr at depth 2: evaluates expr, outputs ,<result>
 *
 * Unquote takes one normal LCL word:
 * - ,$var, ,${name}, ,[cmd], ,(list), ,#{dict}
 */

typedef enum { QQ_LITERAL, QQ_EVAL, QQ_SPLICE } qq_node_kind;

typedef struct qq_node {
  qq_node_kind kind;
  char *text;
  int prefix_commas;
  struct qq_node *next;
} qq_node;

static void qq_node_free(qq_node *node) {
  while (node) {
    qq_node *next = node->next;
    free(node->text);
    free(node);
    node = next;
  }
}

static qq_node *qq_node_new(qq_node_kind kind, const char *text, size_t len) {
  qq_node *node = (qq_node *)calloc(1, sizeof(qq_node));
  if (!node) {
    return NULL;
  }

  node->kind = kind;

  if (text && len > 0) {
    node->text = (char *)malloc(len + 1);
    if (!node->text) {
      free(node);
      return NULL;
    }

    memcpy(node->text, text, len);
    node->text[len] = '\0';
  }

  return node;
}

static int is_nested_quasiquote(const char *src, size_t len, size_t pos) {
  const char *kw = "quasiquote";
  size_t kw_len = 10;
  size_t i;

  if (pos + kw_len >= len) {
    return 0;
  }

  if (memcmp(src + pos, kw, kw_len) != 0) {
    return 0;
  }

  i = pos + kw_len;

  while (i < len && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n')) {
    i++;
  }

  return (i < len && src[i] == '{');
}

static size_t find_quasiquote_end(const char *src, size_t len, size_t start) {
  size_t end;

  if (lcl_scan_skip_braces_span(src, len, start + 1, &end) != 0) {
    return len + 1; /* unterminated: take the rest, as before */
  }

  return end;
}

static int parse_unquote_word(const char *src, size_t len, size_t pos,
                              size_t *word_start, size_t *word_end,
                              const char **err) {
  size_t i = pos;

  *err = NULL;

  while (i < len && (src[i] == ' ' || src[i] == '\t')) {
    i++;
  }

  if (i >= len) {
    return 0;
  }

  *word_start = i;

  if (src[i] == '$') {
    i++;
    if (i < len && src[i] == '{') {
      i++;

      while (i < len && src[i] != '}') {
        i++;
      }

      if (i < len) {
        i++;
      }
    } else {
      while (i < len && lcl_name_is_char((unsigned char)src[i])) {
        i++;
      }

      if (i + 2 < len && src[i] == ':' && src[i + 1] == ':' &&
          lcl_name_is_char((unsigned char)src[i + 2])) {
        *err = "qualified substitutions require braces: ${name::path}";
        return 0;
      }
    }

    *word_end = i;
    return 1;
  }

  if (src[i] == '[') {
    if (lcl_scan_skip_balanced_span(src, len, i + 1, '[', ']', &i) != 0) {
      return 0;
    }

    *word_end = i;
    return 1;
  }

  if (src[i] == '{') {
    if (lcl_scan_skip_braces_span(src, len, i + 1, &i) != 0) {
      return 0;
    }

    *word_end = i;
    return 1;
  }

  if (src[i] == '(') {
    if (lcl_scan_skip_balanced_span(src, len, i + 1, '(', ')', &i) != 0) {
      return 0;
    }

    *word_end = i;
    return 1;
  }

  if (src[i] == '#' && i + 1 < len && src[i + 1] == '{') {
    if (lcl_scan_skip_balanced_span(src, len, i + 2, '{', '}', &i) != 0) {
      return 0;
    }

    *word_end = i;
    return 1;
  }

  if (isalnum((unsigned char)src[i]) || src[i] == '_' || src[i] == '-') {
    while (i < len && (lcl_name_is_char((unsigned char)src[i]) ||
                       src[i] == ':' || src[i] == '-')) {
      i++;
    }

    *word_end = i;
    return 1;
  }

  return 0;
}

/* Recursive quasiquote parser with depth tracking */
static qq_node *qq_parse(const char *src, size_t len, int depth,
                         const char **err_msg) {
  qq_node *head = NULL;
  qq_node *tail = NULL;
  size_t i = 0;
  size_t lit_start = 0;

  while (i < len) {
    char c = src[i];

    if (c == '\\' && i + 1 < len) {
      if (i > lit_start) {
        qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      {
        qq_node *node = qq_node_new(QQ_LITERAL, src + i + 1, 1);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      i += 2;
      lit_start = i;
      continue;
    }

    if (c == ';' && i + 1 < len && src[i + 1] == ';') {
      while (i < len && src[i] != '\n') {
        i++;
      }

      continue;
    }

    if (is_nested_quasiquote(src, len, i)) {
      if (i > lit_start) {
        qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      {
        size_t kw_start = i;
        size_t brace_pos = i + 10; /* "quasiquote" */
        size_t inner_start;
        size_t inner_end;
        qq_node *inner_nodes;
        qq_node *node;

        while (brace_pos < len && src[brace_pos] != '{') {
          brace_pos++;
        }

        inner_start = brace_pos + 1;
        inner_end = find_quasiquote_end(src, len, brace_pos) - 1;
        node = qq_node_new(QQ_LITERAL, src + kw_start, inner_start - kw_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;

        inner_nodes = qq_parse(src + inner_start, inner_end - inner_start,
                               depth + 1, err_msg);
        if (!inner_nodes && *err_msg) {
          qq_node_free(head);
          return NULL;
        }

        if (inner_nodes) {
          tail->next = inner_nodes;
          while (tail->next) {
            tail = tail->next;
          }
        }

        node = qq_node_new(QQ_LITERAL, "}", 1);

        if (!node) {
          goto parse_err;
        }

        tail->next = node;
        tail = node;
        i = inner_end + 1;
        lit_start = i;
      }

      continue;
    }

    if (c == '"') {
      i++;

      while (i < len && src[i] != '"') {
        if (src[i] == '\\' && i + 1 < len) {
          i++;
        }
        i++;
      }

      if (i < len) {
        i++;
      }

      continue;
    }

    if (c == ',') {
      int num_commas = 0;
      int splice = 0;
      size_t comma_start = i;
      size_t word_start;
      size_t word_end;

      if (i > lit_start) {
        qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      while (i < len && src[i] == ',') {
        num_commas++;
        i++;
      }

      if (i < len && src[i] == '@') {
        splice = 1;
        i++;
      }

      {
        const char *uq_err = NULL;

        if (!parse_unquote_word(src, len, i, &word_start, &word_end, &uq_err)) {
          *err_msg = uq_err
                         ? uq_err
                         : "invalid unquote: expected $var, [cmd], {literal}, "
                           "(list), or #{dict}";
          qq_node_free(head);
          return NULL;
        }
      }

      if (num_commas > depth) {
        *err_msg = "too many unquotes for current quasiquote depth";
        qq_node_free(head);
        return NULL;
      }

      if (num_commas < depth) {
        size_t total_len = (word_end - comma_start);
        qq_node *node = qq_node_new(QQ_LITERAL, src + comma_start, total_len);

        if (!node) {
          goto parse_err;
        }

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      } else {
        qq_node *node;

        if (splice) {
          node =
              qq_node_new(QQ_SPLICE, src + word_start, word_end - word_start);
        } else {
          node = qq_node_new(QQ_EVAL, src + word_start, word_end - word_start);
        }

        if (!node) {
          goto parse_err;
        }

        node->prefix_commas = num_commas - 1;

        if (tail) {
          tail->next = node;
        } else {
          head = node;
        }

        tail = node;
      }

      i = word_end;
      lit_start = i;
      continue;
    }

    i++;
  }

  if (i > lit_start) {
    qq_node *node = qq_node_new(QQ_LITERAL, src + lit_start, i - lit_start);

    if (!node) {
      goto parse_err;
    }

    if (tail) {
      tail->next = node;
    } else {
      head = node;
    }

    tail = node;
  }

  return head;

parse_err:
  *err_msg = "out of memory";
  qq_node_free(head);
  return NULL;
}

/* Check if a string needs braces to be a valid unquote word. Returns
 * 1 if braces needed, 0 if bare word is fine. */
static int qq_needs_braces(const char *s) {
  size_t i;
  if (!s || !*s) {
    return 1;
  }

  if (s[0] == '$' || s[0] == '[' || s[0] == '{' || s[0] == '(' || s[0] == '#') {
    return 1;
  }

  for (i = 0; s[i]; i++) {
    unsigned char c = (unsigned char)s[i];

    if (!isalnum(c) && c != '_' && c != '-' && c != '.' && c != ':') {
      return 1;
    }
  }

  return 0;
}

/* Build result string from IR, evaluating as needed */
static int qq_build(lcl_interp *interp, qq_node *nodes, char **result,
                    size_t *result_len, size_t *result_cap) {
  qq_node *node;

  for (node = nodes; node; node = node->next) {
    switch (node->kind) {
    case QQ_LITERAL:
      if (node->text) {
        if (!lcl_std_buf_append(result, result_len, result_cap, node->text,
                                strlen(node->text))) {
          return 0;
        }
      }
      break;

    case QQ_EVAL:
    case QQ_SPLICE: {
      lcl_scan sc;
      lcl_word w;
      lcl_value *val = NULL;
      const char *saved_file;
      int saved_line;
      int scan_rc;
      lcl_return_code eval_rc;
      int j;

      for (j = 0; j < node->prefix_commas; j++) {
        if (!lcl_std_buf_append_char(result, result_len, result_cap, ',')) {
          return 0;
        }
      }

      /* The unquote text is a single syntactic word ($var, [...],
       * {...}, (...), #{...}, or a bare word). Evaluate it as a word
       * so a variable substitution like ,$var yields the variable's
       * value -- not a one-word program that would dispatch that
       * value as a zero-arg command if its string form happened to
       * name a proc or built-in. */
      memset(&w, 0, sizeof(w));
      lcl_scan_init(&sc, node->text);
      scan_rc = lcl_scan_word(&sc, &w);

      if (scan_rc < 0) {
        lcl_word_free_contents(&w);
        LCL_ERR_MSG(interp,
                    sc.err ? sc.err
                           : "quasiquote: failed to parse unquote expression");
        return 0;
      }

      saved_file = interp->cur_file;
      saved_line = interp->cur_line;

      eval_rc = lcl_eval_word(interp, &w, &val);

      interp->cur_file = saved_file;
      interp->cur_line = saved_line;

      lcl_word_free_contents(&w);

      if (eval_rc != LCL_RC_OK) {
        lcl_ref_dec(val);
        return 0;
      }

      if (node->kind == QQ_SPLICE) {
        lcl_value *list_val = val;

        /* ,@ splices a list's elements; text is not reparsed as a
         * list (build one with (...), List::push, or String::split). */
        if (val->type != LCL_LIST) {
          lcl_std_err_expected_got(interp, "quasiquote: ,@", "list", val);
          lcl_ref_dec(val);
          return 0;
        }

        {
          size_t len = lcl_list_len(list_val);
          size_t k;

          for (k = 0; k < len; k++) {
            lcl_value *elem = NULL;
            const char *elem_str;

            if (lcl_list_get(list_val, k, &elem) != LCL_OK) {
              lcl_ref_dec(val);
              return 0;
            }

            if (lcl_value_to_cstring(interp, elem, &elem_str) != LCL_OK) {
              lcl_ref_dec(elem);
              lcl_ref_dec(val);
              return 0;
            }

            if (k > 0) {
              if (!lcl_std_buf_append_char(result, result_len, result_cap,
                                           ' ')) {
                lcl_ref_dec(elem);
                lcl_ref_dec(val);
                return 0;
              }
            }

            if (!lcl_std_buf_append(result, result_len, result_cap, elem_str,
                                    strlen(elem_str))) {
              lcl_ref_dec(elem);
              lcl_ref_dec(val);
              return 0;
            }

            lcl_ref_dec(elem);
          }
        }
      } else {
        const char *val_str;
        int needs_braces;

        if (lcl_value_to_cstring(interp, val, &val_str) != LCL_OK) {
          lcl_ref_dec(val);
          return 0;
        }
        needs_braces = node->prefix_commas > 0 && qq_needs_braces(val_str);

        if (needs_braces) {
          if (!lcl_std_buf_append_char(result, result_len, result_cap, '{')) {
            lcl_ref_dec(val);
            return 0;
          }
        }

        if (!lcl_std_buf_append(result, result_len, result_cap, val_str,
                                strlen(val_str))) {
          lcl_ref_dec(val);
          return 0;
        }

        if (needs_braces) {
          if (!lcl_std_buf_append_char(result, result_len, result_cap, '}')) {
            lcl_ref_dec(val);
            return 0;
          }
        }
      }

      lcl_ref_dec(val);
    } break;
    }
  }

  return 1;
}

static lcl_return_code s_quasiquote(lcl_interp *interp, int argc,
                                    const lcl_word **args, lcl_value **out) {
  lcl_value *input_v = NULL;
  const char *src;
  size_t src_len;
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;
  qq_node *ir = NULL;
  const char *err_msg = NULL;

  if (!lcl_std_chk_argc(interp, "quasiquote", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_eval_word_to_str(interp, args[0], &input_v) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, input_v, &src) != LCL_OK) {
    lcl_ref_dec(input_v);
    return LCL_RC_ERR;
  }

  src_len = strlen(src);
  ir = qq_parse(src, src_len, 1, &err_msg);

  if (!ir && err_msg) {
    LCL_ERR_MSG(interp, err_msg);
    lcl_ref_dec(input_v);
    return LCL_RC_ERR;
  }

  if (!qq_build(interp, ir, &result, &result_len, &result_cap)) {
    qq_node_free(ir);
    lcl_ref_dec(input_v);
    free(result);
    return LCL_RC_ERR;
  }

  qq_node_free(ir);
  lcl_ref_dec(input_v);

  *out = lcl_value_new_string(result ? result : "");
  free(result);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code s_eval(lcl_interp *interp, int argc,
                              const lcl_word **args, lcl_value **out) {
  int i;
  lcl_program *prog = NULL;
  lcl_return_code rc = LCL_RC_OK;
  lcl_value *last = NULL;
  int saved_tail_position = interp->in_tail_position;
  const char *saved_cur_file = interp->cur_file;
  int saved_cur_line = interp->cur_line;
  int saved_dyn_mode = interp->env.dyn_mode;

  if (!lcl_std_chk_argc(interp, "eval", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  /* MVP: single argument */
  if (argc == 1) {
    lcl_value *script_v = NULL;
    const char *script_src;

    if (lcl_eval_word_to_str(interp, args[0], &script_v) != LCL_RC_OK) {
      return LCL_RC_ERR;
    }

    if (lcl_value_to_cstring(interp, script_v, &script_src) != LCL_OK) {
      lcl_ref_dec(script_v);
      return LCL_RC_ERR;
    }

    {
      char name[256];
      prog = lcl_compile_report(
          interp, script_src,
          lcl_dyn_source_name(interp, "eval", name, sizeof(name)));
    }
    lcl_ref_dec(script_v);
  } else {
    size_t total_len = 0;
    lcl_value **parts = NULL;
    char *script_str = NULL;
    char *p;

    parts = malloc(sizeof(lcl_value *) * (size_t)argc);
    if (!parts) {
      LCL_ERR_MSG(interp, "eval: out of memory");
      return LCL_RC_ERR;
    }

    for (i = 0; i < argc; i++) {
      size_t part_len;
      const char *part_str;

      if (lcl_eval_word_to_str(interp, args[i], &parts[i]) != LCL_RC_OK) {
        int j;

        for (j = 0; j < i; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        return LCL_RC_ERR;
      }

      if (lcl_value_to_cstring(interp, parts[i], &part_str) != LCL_OK) {
        int j;

        for (j = 0; j <= i; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        return LCL_RC_ERR;
      }

      part_len = strlen(part_str);

      if (!lcl_std_safe_add_size(total_len, part_len, &total_len)) {
        int j;

        for (j = 0; j <= i; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        LCL_ERR_MSG(interp, "eval: combined script length overflows size_t");
        return LCL_RC_ERR;
      }
    }

    if (!lcl_std_safe_add_size(total_len, (size_t)(argc - 1), &total_len) ||
        !lcl_std_safe_add_size(total_len, 1, &total_len)) {
      for (i = 0; i < argc; i++) {
        lcl_ref_dec(parts[i]);
      }

      free(parts);
      LCL_ERR_MSG(interp, "eval: combined script length overflows size_t");
      return LCL_RC_ERR;
    }

    script_str = malloc(total_len);

    if (!script_str) {
      LCL_ERR_MSG(interp, "eval: out of memory");

      for (i = 0; i < argc; i++) {
        lcl_ref_dec(parts[i]);
      }

      free(parts);
      return LCL_RC_ERR;
    }

    p = script_str;

    for (i = 0; i < argc; i++) {
      const char *s;
      size_t l;

      if (lcl_value_to_cstring(interp, parts[i], &s) != LCL_OK) {
        int j;

        for (j = 0; j < argc; j++) {
          lcl_ref_dec(parts[j]);
        }

        free(parts);
        free(script_str);
        return LCL_RC_ERR;
      }

      l = strlen(s);
      memcpy(p, s, l);
      p += l;

      if (i + 1 < argc) {
        *p++ = ' ';
      }
    }

    *p = '\0';

    for (i = 0; i < argc; i++) {
      lcl_ref_dec(parts[i]);
    }

    free(parts);

    {
      char name[256];
      prog = lcl_compile_report(
          interp, script_str,
          lcl_dyn_source_name(interp, "eval", name, sizeof(name)));
    }
    free(script_str);
  }

  if (!prog) {
    return LCL_RC_ERR;
  }

  if (interp->max_depth && interp->depth >= interp->max_depth) {
    LCL_ERR_MSG(interp, "eval: max recursion depth exceeded");
    lcl_program_free(prog);
    return LCL_RC_ERR;
  }

  interp->depth++;

  interp->env.dyn_mode = 1;

  for (i = 0; i < prog->ncmd; i++) {
    lcl_command *cmd = &prog->cmd[i];
    int is_last_cmd = (i == prog->ncmd - 1);

    interp->cur_file = prog->file;
    interp->cur_line = cmd->line;

    if (last) {
      lcl_ref_dec(last);
      last = NULL;
    }

    interp->in_tail_position = saved_tail_position && is_last_cmd;
    rc = lcl_call_from_words(interp, cmd, &last);

    if (rc == LCL_RC_TAILCALL) {
      interp->in_tail_position = saved_tail_position;
      interp->cur_file = saved_cur_file;
      interp->cur_line = saved_cur_line;
      interp->env.dyn_mode = saved_dyn_mode;
      interp->depth--;
      lcl_program_free(prog);

      if (out) {
        *out = NULL;
      }

      return rc;
    }

    if (rc != LCL_RC_OK) {
      break;
    }
  }

  interp->in_tail_position = saved_tail_position;
  interp->cur_file = saved_cur_file;
  interp->cur_line = saved_cur_line;
  interp->env.dyn_mode = saved_dyn_mode;
  interp->depth--;
  lcl_program_free(prog);

  if (rc == LCL_RC_OK || rc == LCL_RC_RETURN) {
    *out = last ? last : lcl_string_new("");
  } else {
    if (last) {
      lcl_ref_dec(last);
    }
  }

  return rc;
}

/* apply callable arg1 arg2 ... argN
 *
 * Explicit value-dispatch primitive. The complement to `eval`:
 * - `eval`  takes a *source string* and runs it as code.
 * - `apply` takes a *value* and dispatches it as a call.
 *
 * Resolution:
 *   LCL_PROC (non-macro)  -> call via lcl_call_user_proc
 *   LCL_CPROC, normal     -> call fn.proc(interp, N, &argv[1], out)
 *   LCL_CPROC, special    -> error: cannot apply special form
 *   LCL_PROC, is_macro    -> error: cannot apply macro
 *   LCL_STRING            -> resolve as a command name, recurse
 *   anything else         -> error: not callable
 *
 * this is the explicit-dispatch keyword that replaces the implicit
 * one-word-program dispatch once the parse-time rule lands. */
static lcl_return_code c_apply(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  lcl_value *callee;
  int call_argc;
  lcl_value **call_argv;
  lcl_value *resolved = NULL;

  if (!lcl_std_chk_argc(interp, "apply", argc, 1, -1)) {
    return LCL_RC_ERR;
  }

  callee = argv[0];
  call_argc = argc - 1;
  call_argv = (call_argc > 0) ? &argv[1] : NULL;

  while (callee->type == LCL_STRING) {
    const char *name;
    lcl_value *next = NULL;

    if (lcl_value_to_cstring(interp, callee, &name) != LCL_OK) {
      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    if (lcl_env_get_command(interp, name, &next) != LCL_OK) {
      const size_t name_len = strlen(name);
      const size_t prefix_len = 17;
      char *buf = (char *)malloc(name_len + prefix_len + 1);

      if (buf) {
        memcpy(buf, "unknown command: ", prefix_len);
        memcpy(buf + prefix_len, name, name_len + 1);
        LCL_ERR_MSG_DUP(interp, buf);
        free(buf);
      } else {
        LCL_ERR_MSG(interp, "apply: unknown command");
      }

      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    lcl_ref_dec(resolved);
    resolved = next;
    callee = resolved;
  }

  if (callee->type == LCL_CPROC) {
    lcl_return_code rc;

    if (callee->as.c_proc.fn->kind == LCL_CK_SPECIAL) {
      LCL_ERR_MSG(interp, "apply: cannot apply special form");
      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    rc = callee->as.c_proc.fn->fn.proc(interp, call_argc, call_argv, out);
    lcl_ref_dec(resolved);
    return rc;
  }

  if (callee->type == LCL_PROC) {
    lcl_proc *p = (lcl_proc *)callee->as.procedure.proc;
    lcl_return_code rc;

    if (p->is_macro) {
      LCL_ERR_MSG(interp, "apply: cannot apply macro");
      lcl_ref_dec(resolved);
      return LCL_RC_ERR;
    }

    rc = lcl_call_user_proc(interp, callee, p, NULL, call_argc, call_argv, out);
    lcl_ref_dec(resolved);
    return rc;
  }

  lcl_std_err_expected_got(interp, "apply", "callable", callee);
  lcl_ref_dec(resolved);
  return LCL_RC_ERR;
}

/* find the end of a callable expression starting with [ */
static const char *find_bracket_end(const char *s) {
  int depth = 1;
  s++;

  while (*s && depth > 0) {
    if (*s == '[') {
      depth++;
    } else if (*s == ']') {
      depth--;
    }
    s++;
  }
  return s;
}

/*
 * Thread-first operator: -> initial {form1} {form2} ...
 *
 * Threads the value through each form as the first argument.
 *
 * Example: -> $d {get b} becomes: get $d b
 *          -> $d {put c 3} {del a} becomes: del [put $d c 3] a
 *          -> 10 {$f} becomes: [$f 10] (call lambda in variable)
 *          -> 10 {[lambda {x} ...]} becomes: [[lambda {x} ...] 10]
 */
static lcl_return_code s_thread_first_core(lcl_interp *interp, int argc,
                                           const lcl_word **args,
                                           lcl_value **out) {
  lcl_value *current = NULL;
  lcl_value *form_v = NULL;
  int i;
  lcl_return_code rc;

  if (argc < 1) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  rc = lcl_eval_word(interp, args[0], &current);

  if (rc != LCL_RC_OK) {
    if (current) {
      *out = current;
    }

    return rc;
  }

  for (i = 1; i < argc; i++) {
    const char *form;
    const char *cmd_end;
    const char *rest;
    char *threaded = NULL;
    size_t cmd_len;
    size_t total;
    lcl_value *result = NULL;

    lcl_env_let(&interp->env, "_thread_", current);
    rc = lcl_eval_word_to_str(interp, args[i], &form_v);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(current);
      return rc;
    }

    if (lcl_value_to_cstring(interp, form_v, &form) != LCL_OK) {
      lcl_ref_dec(form_v);
      lcl_ref_dec(current);
      return LCL_RC_ERR;
    }

    if (form[0] == '[') {
      cmd_end = find_bracket_end(form);
      cmd_len = (size_t)(cmd_end - form);
      rest = cmd_end;

      while (*rest == ' ' || *rest == '\t') {
        rest++;
      }

      total = 1 + cmd_len + 11 + strlen(rest) + 2;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (*rest) {
        sprintf(threaded, "[%.*s $_thread_ %s]", (int)cmd_len, form, rest);
      } else {
        sprintf(threaded, "[%.*s $_thread_]", (int)cmd_len, form);
      }
    } else if (form[0] == '$') {
      cmd_end = form + 1;

      while (*cmd_end && *cmd_end != ' ' && *cmd_end != '\t' &&
             *cmd_end != '\n') {
        cmd_end++;
      }

      cmd_len = (size_t)(cmd_end - form);
      rest = cmd_end;

      while (*rest == ' ' || *rest == '\t') {
        rest++;
      }

      total = 1 + cmd_len + 11 + strlen(rest) + 2;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (*rest) {
        sprintf(threaded, "[%.*s $_thread_ %s]", (int)cmd_len, form, rest);
      } else {
        sprintf(threaded, "[%.*s $_thread_]", (int)cmd_len, form);
      }
    } else {
      cmd_end = form;

      while (*cmd_end && *cmd_end != ' ' && *cmd_end != '\t' &&
             *cmd_end != '\n') {
        cmd_end++;
      }

      cmd_len = (size_t)(cmd_end - form);
      rest = cmd_end;

      while (*rest == ' ' || *rest == '\t') {
        rest++;
      }

      total = cmd_len + 12 + strlen(rest) + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      if (*rest) {
        sprintf(threaded, "%.*s $_thread_ %s", (int)cmd_len, form, rest);
      } else {
        sprintf(threaded, "%.*s $_thread_", (int)cmd_len, form);
      }
    }

    lcl_ref_dec(form_v);
    form_v = NULL;

    rc = lcl_eval_string(interp, threaded, &result);
    free(threaded);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(current);
      *out = result;

      return rc;
    }

    lcl_ref_dec(current);
    current = result;
  }

  *out = current;
  return LCL_RC_OK;
}

/* Thread-last operator: ->> initial {form1} {form2} ...
 *
 * Threads the value through each form as the last argument.
 *
 * Example: ->> $d {cmd a b} becomes: cmd a b $d
 *          ->> 10 {$f a} becomes: [$f a 10]
 *          ->> 10 {[lambda {x} ...]} becomes: [[lambda {x} ...] 10]
 */
static lcl_return_code s_thread_last_core(lcl_interp *interp, int argc,
                                          const lcl_word **args,
                                          lcl_value **out) {
  lcl_value *current = NULL;
  lcl_value *form_v = NULL;
  int i;
  lcl_return_code rc;

  if (argc < 1) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  rc = lcl_eval_word(interp, args[0], &current);

  if (rc != LCL_RC_OK) {
    if (current) {
      *out = current;
    }

    return rc;
  }

  for (i = 1; i < argc; i++) {
    const char *form;
    char *threaded = NULL;
    size_t total;
    lcl_value *result = NULL;

    lcl_env_let(&interp->env, "_thread_", current);
    rc = lcl_eval_word_to_str(interp, args[i], &form_v);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(current);

      return rc;
    }

    if (lcl_value_to_cstring(interp, form_v, &form) != LCL_OK) {
      lcl_ref_dec(form_v);
      lcl_ref_dec(current);
      return LCL_RC_ERR;
    }

    if (form[0] == '[' || form[0] == '$') {
      total = 1 + strlen(form) + 11 + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      sprintf(threaded, "[%s $_thread_]", form);
    } else {
      total = strlen(form) + 11 + 1;
      threaded = (char *)malloc(total);

      if (!threaded) {
        LCL_ERR_MSG(interp, "out of memory");
        lcl_ref_dec(form_v);
        lcl_ref_dec(current);

        return LCL_RC_ERR;
      }

      sprintf(threaded, "%s $_thread_", form);
    }

    lcl_ref_dec(form_v);
    form_v = NULL;

    rc = lcl_eval_string(interp, threaded, &result);
    free(threaded);

    if (rc != LCL_RC_OK) {
      lcl_ref_dec(current);
      *out = result;

      return rc;
    }

    lcl_ref_dec(current);
    current = result;
  }

  *out = current;
  return LCL_RC_OK;
}

static lcl_return_code s_thread_first(lcl_interp *interp, int argc,
                                      const lcl_word **args, lcl_value **out) {
  int saved_dyn = interp->env.dyn_mode;
  lcl_return_code rc;

  interp->env.dyn_mode = 1;
  rc = s_thread_first_core(interp, argc, args, out);
  interp->env.dyn_mode = saved_dyn;

  return rc;
}

static lcl_return_code s_thread_last(lcl_interp *interp, int argc,
                                     const lcl_word **args, lcl_value **out) {
  int saved_dyn = interp->env.dyn_mode;
  lcl_return_code rc;

  interp->env.dyn_mode = 1;
  rc = s_thread_last_core(interp, argc, args, out);
  interp->env.dyn_mode = saved_dyn;

  return rc;
}

void lcl_std_register_eval(lcl_interp *interp) {
  lcl_register_spec(interp, "eval", s_eval);
  lcl_register_proc(interp, "apply", c_apply);
  lcl_register_spec(interp, "subst", s_subst);
  lcl_register_spec(interp, "quasiquote", s_quasiquote);
  lcl_register_spec(interp, "->", s_thread_first);
  lcl_register_spec(interp, "->>", s_thread_last);
}
