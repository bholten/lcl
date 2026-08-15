#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif

#include "lcl-stdlib-internal.h"

static lcl_value *lex_word_record(const lcl_word *w) {
  lcl_value *rec;
  lcl_value *v;
  int i;
  int dynamic = 0;
  size_t text_len = 0;

  for (i = 0; i < w->np; i++) {
    if (w->wp[i].kind == LCL_WP_LIT) {
      text_len += w->wp[i].as.lit.n;
    } else {
      dynamic = 1;
    }
  }

  rec = lcl_dict_new();

  if (!rec) {
    return NULL;
  }

  if (dynamic) {
    v = lcl_string_new("");
  } else {
    char *buf = (char *)malloc(text_len + 1);
    size_t off = 0;

    if (!buf) {
      lcl_ref_dec(rec);
      return NULL;
    }

    for (i = 0; i < w->np; i++) {
      memcpy(buf + off, w->wp[i].as.lit.s, w->wp[i].as.lit.n);
      off += w->wp[i].as.lit.n;
    }

    buf[text_len] = '\0';
    v = lcl_string_new(buf);
    free(buf);
  }

  if (!v) {
    lcl_ref_dec(rec);
    return NULL;
  }

  lcl_dict_put(&rec, "text", v);
  lcl_ref_dec(v);

  v = lcl_int_new(dynamic);
  lcl_dict_put(&rec, "dynamic", v);
  lcl_ref_dec(v);

  v = lcl_int_new(w->quoted ? 1 : 0);
  lcl_dict_put(&rec, "quoted", v);
  lcl_ref_dec(v);

  v = lcl_int_new(w->braced ? 1 : 0);
  lcl_dict_put(&rec, "braced", v);
  lcl_ref_dec(v);

  v = lcl_int_new(w->expand ? 1 : 0);
  lcl_dict_put(&rec, "expand", v);
  lcl_ref_dec(v);

  {
    lcl_value *span = lcl_list_new();

    if (!span) {
      lcl_ref_dec(rec);
      return NULL;
    }

    v = lcl_int_new(w->src_start);
    lcl_list_push(&span, v);
    lcl_ref_dec(v);

    v = lcl_int_new(w->src_end);
    lcl_list_push(&span, v);
    lcl_ref_dec(v);

    lcl_dict_put(&rec, "span", span);
    lcl_ref_dec(span);
  }

  if (dynamic) {
    lcl_value *pieces = lcl_list_new();

    if (!pieces) {
      lcl_ref_dec(rec);
      return NULL;
    }

    for (i = 0; i < w->np; i++) {
      lcl_value *pd = lcl_dict_new();

      if (!pd) {
        lcl_ref_dec(pieces);
        lcl_ref_dec(rec);
        return NULL;
      }

      switch (w->wp[i].kind) {
      case LCL_WP_LIT:
        v = lcl_string_new("lit");
        lcl_dict_put(&pd, "kind", v);
        lcl_ref_dec(v);
        v = lcl_string_new(w->wp[i].as.lit.s);
        lcl_dict_put(&pd, "text", v);
        lcl_ref_dec(v);
        break;

      case LCL_WP_VAR:
        v = lcl_string_new("var");
        lcl_dict_put(&pd, "kind", v);
        lcl_ref_dec(v);
        v = lcl_string_new(w->wp[i].as.var.name);
        lcl_dict_put(&pd, "name", v);
        lcl_ref_dec(v);
        break;

      case LCL_WP_SUBCMD:
        v = lcl_string_new("sub");
        lcl_dict_put(&pd, "kind", v);
        lcl_ref_dec(v);
        break;
      }

      lcl_list_push(&pieces, pd);
      lcl_ref_dec(pd);
    }

    lcl_dict_put(&rec, "pieces", pieces);
    lcl_ref_dec(pieces);
  }

  return rec;
}

/* Lex::commands text - read `text` with the language's own reader,
 * WITHOUT evaluating any of it, and return the lexical structure: a
 * list of command records
 *   #{line N words (<word record> ...)}
 * one per statement.  This exists so embedders (weft's command
 * resolver) can decide what a piece of interactive text IS -- lcl
 * call, external command, shell handoff -- from the real grammar
 * instead of a lookalike parser.  Malformed input errors with the
 * compiler's message and line; comment-only or empty text is an
 * empty list. */
static lcl_return_code c_lex_commands(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  const char *src;
  lcl_program *prog;
  lcl_compile_err cerr;
  lcl_value *result;
  int ci;
  int wi;

  if (!lcl_std_chk_argc(interp, "Lex::commands", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &src) != LCL_OK) {
    return LCL_RC_ERR;
  }

  cerr.msg = NULL;
  cerr.line = 0;
  prog = lcl_program_compile_ex(src, NULL, &cerr);

  if (!prog) {
    char msg[192];

    snprintf(msg, sizeof(msg), "Lex::commands: %s (line %ld)",
             cerr.msg ? cerr.msg : "parse error", cerr.line);
    LCL_ERR_MSG_DUP(interp, msg);
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  if (!result) {
    lcl_program_free(prog);
    LCL_ERR_MSG(interp, "Lex::commands: out of memory");
    return LCL_RC_ERR;
  }

  for (ci = 0; ci < prog->ncmd; ci++) {
    const lcl_command *cmd = &prog->cmd[ci];
    lcl_value *cmd_rec = lcl_dict_new();
    lcl_value *words;
    lcl_value *v;

    if (!cmd_rec) {
      goto oom;
    }

    v = lcl_int_new(cmd->line);
    lcl_dict_put(&cmd_rec, "line", v);
    lcl_ref_dec(v);

    words = lcl_list_new();

    if (!words) {
      lcl_ref_dec(cmd_rec);
      goto oom;
    }

    for (wi = 0; wi < cmd->argc; wi++) {
      lcl_value *wrec = lex_word_record(&cmd->w[wi]);

      if (!wrec) {
        lcl_ref_dec(words);
        lcl_ref_dec(cmd_rec);
        goto oom;
      }

      lcl_list_push(&words, wrec);
      lcl_ref_dec(wrec);
    }

    lcl_dict_put(&cmd_rec, "words", words);
    lcl_ref_dec(words);
    lcl_list_push(&result, cmd_rec);
    lcl_ref_dec(cmd_rec);
  }

  lcl_program_free(prog);
  *out = result;

  return LCL_RC_OK;

oom:
  lcl_ref_dec(result);
  lcl_program_free(prog);
  LCL_ERR_MSG(interp, "Lex::commands: out of memory");
  return LCL_RC_ERR;
}

void lcl_std_register_lex(lcl_interp *interp) {
  lcl_value *lex_ns;

  lex_ns = lcl_ns_new("Lex");
  lcl_define_take(interp, "Lex", lex_ns);
  lcl_ns_def_take(lex_ns, "commands",
                  lcl_c_proc_new("Lex::commands", c_lex_commands));
}
