#include "lcl-stdlib-internal.h"

/* String::from v - explicit conversion to v's canonical string */
static lcl_return_code c_string_from(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  const char *s;

  if (!lcl_std_chk_argc(interp, "String::from", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &s) != LCL_OK) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(s);
  return LCL_RC_OK;
}

/* join list ?separator? - join list elements with separator (default
   space) */
static lcl_return_code c_join(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lcl_value *list;
  const char *sep = " ";
  size_t sep_len;
  size_t len;
  size_t i;
  char *buf = NULL;
  size_t buf_len = 0;
  size_t buf_cap = 0;

  if (!lcl_std_chk_argc(interp, "String::join", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  list = argv[0];

  if (argc == 2) {
    if (!lcl_std_arg_str(interp, "String::join", argv[1], &sep)) {
      return LCL_RC_ERR;
    }
  }

  sep_len = strlen(sep);

  if (list->type != LCL_LIST) {
    const char *list_str;
    if (lcl_value_to_cstring(interp, list, &list_str) != LCL_OK) {
      return LCL_RC_ERR;
    }
    *out = lcl_string_new(list_str);

    return LCL_RC_OK;
  }

  len = lcl_list_len(list);

  for (i = 0; i < len; i++) {
    lcl_value *elem = NULL;
    const char *elem_str;
    size_t elem_len;

    if (lcl_list_get(list, i, &elem) != LCL_OK) {
      continue;
    }

    if (lcl_value_to_cstring(interp, elem, &elem_str) != LCL_OK) {
      lcl_ref_dec(elem);
      free(buf);
      return LCL_RC_ERR;
    }
    elem_len = strlen(elem_str);

    if (i > 0 && sep_len > 0) {
      if (!lcl_std_buf_append(&buf, &buf_len, &buf_cap, sep, sep_len)) {
        LCL_ERR_MSG(interp, "String::join: out of memory");
        lcl_ref_dec(elem);
        free(buf);

        return LCL_RC_ERR;
      }
    }

    if (!lcl_std_buf_append(&buf, &buf_len, &buf_cap, elem_str, elem_len)) {
      LCL_ERR_MSG(interp, "String::join: out of memory");
      lcl_ref_dec(elem);
      free(buf);

      return LCL_RC_ERR;
    }

    lcl_ref_dec(elem);
  }

  *out = lcl_string_new(buf ? buf : "");
  free(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* split string ?splitChars? - split string into list (default split
 * on each char) */
static lcl_return_code c_split(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *str;
  const char *split_chars = NULL;
  lcl_value *result;

  if (!lcl_std_chk_argc(interp, "String::split", argc, 1, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::split", argv[0], &str)) {
    return LCL_RC_ERR;
  }

  if (argc == 2) {
    if (!lcl_std_arg_str(interp, "String::split", argv[1], &split_chars)) {
      return LCL_RC_ERR;
    }
  }

  result = lcl_list_new();

  if (!result) {
    LCL_ERR_MSG(interp, "String::split: out of memory");
    return LCL_RC_ERR;
  }

  if (!split_chars || *split_chars == '\0') {
    const char *p = str;

    while (*p) {
      /* C89: aggregate initializers must be constant expressions. */
      char c[2];
      lcl_value *elem;

      c[0] = *p;
      c[1] = '\0';
      elem = lcl_string_new(c);

      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "String::split: out of memory");
        if (elem) {
          lcl_ref_dec(elem);
        }

        lcl_ref_dec(result);

        return LCL_RC_ERR;
      }

      lcl_ref_dec(elem);
      p++;
    }
  } else {
    const char *p = str;
    const char *start = str;

    while (*p) {
      if (strchr(split_chars, *p)) {
        size_t len = (size_t)(p - start);
        char *word = (char *)malloc(len + 1);
        lcl_value *elem;

        if (!word) {
          LCL_ERR_MSG(interp, "String::split: out of memory");
          lcl_ref_dec(result);
          return LCL_RC_ERR;
        }

        memcpy(word, start, len);
        word[len] = '\0';
        elem = lcl_string_new(word);
        free(word);

        if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
          LCL_ERR_MSG(interp, "String::split: out of memory");
          if (elem) {
            lcl_ref_dec(elem);
          }
          lcl_ref_dec(result);

          return LCL_RC_ERR;
        }

        lcl_ref_dec(elem);
        start = p + 1;
      }

      p++;
    }

    {
      lcl_value *elem = lcl_string_new(start);
      if (!elem || lcl_list_push(&result, elem) != LCL_OK) {
        LCL_ERR_MSG(interp, "String::split: out of memory");
        if (elem) {
          lcl_ref_dec(elem);
        }
        lcl_ref_dec(result);

        return LCL_RC_ERR;
      }

      lcl_ref_dec(elem);
    }
  }

  *out = result;
  return LCL_RC_OK;
}

static lcl_return_code c_is_string(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  (void)interp;

  if (!lcl_std_chk_argc(interp, "string?", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(argv[0]->type == LCL_STRING ? 1 : 0);

  return LCL_RC_OK;
}

/* string::upper s - return uppercase string */
static lcl_return_code c_string_upper(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  const char *src;
  char *result;
  size_t i;
  size_t len;

  if (!lcl_std_chk_argc(interp, "String::upper", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::upper", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);
  result = malloc(len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::upper: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    char c = src[i];

    if (c >= 'a' && c <= 'z') {
      result[i] = (char)(c - 32);
    } else {
      result[i] = c;
    }
  }

  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* string::lower s - return lowercase string */
static lcl_return_code c_string_lower(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  const char *src;
  char *result;
  size_t i;
  size_t len;

  if (!lcl_std_chk_argc(interp, "String::lower", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::lower", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);
  result = malloc(len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::lower: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    char c = src[i];

    if (c >= 'A' && c <= 'Z') {
      result[i] = (char)(c + 32);
    } else {
      result[i] = c;
    }
  }

  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* string::find s sub - return index of first occurrence or -1 */
static lcl_return_code c_string_find(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  const char *haystack;
  const char *needle;
  const char *found;

  if (!lcl_std_chk_argc(interp, "String::find", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::find", argv[0], &haystack)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::find", argv[1], &needle)) {
    return LCL_RC_ERR;
  }

  found = strstr(haystack, needle);

  if (found) {
    *out = lcl_int_new((long)(found - haystack));
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* string::replace s old new - return string with replacements */
static lcl_return_code c_string_replace(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  const char *src;
  const char *old_str;
  const char *new_str;
  const char *p;
  const char *found;
  size_t old_len;
  size_t new_len;
  size_t result_len;
  char *result;
  char *dst;
  int count = 0;

  if (!lcl_std_chk_argc(interp, "String::replace", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::replace", argv[0], &src)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::replace", argv[1], &old_str)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::replace", argv[2], &new_str)) {
    return LCL_RC_ERR;
  }

  old_len = strlen(old_str);
  new_len = strlen(new_str);

  if (old_len == 0) {
    *out = lcl_ref_inc(argv[0]);
    return LCL_RC_OK;
  }

  /* Count occurrences */
  p = src;

  while ((found = strstr(p, old_str)) != NULL) {
    count++;
    p = found + old_len;
  }

  if (count == 0) {
    *out = lcl_ref_inc(argv[0]);

    return LCL_RC_OK;
  }

  {
    size_t src_len = strlen(src);
    size_t total_new;
    size_t total_old = (size_t)count * old_len;

    if (new_len > 0 && (size_t)count > (size_t)-1 / new_len) {
      LCL_ERR_MSG(interp, "String::replace: result too large");
      return LCL_RC_ERR;
    }

    total_new = (size_t)count * new_len;

    if (total_new > (size_t)-1 - (src_len - total_old)) {
      LCL_ERR_MSG(interp, "String::replace: result too large");
      return LCL_RC_ERR;
    }

    result_len = (src_len - total_old) + total_new;

    if (result_len == (size_t)-1) {
      /* malloc(result_len + 1) below would overflow */
      LCL_ERR_MSG(interp, "String::replace: result too large");
      return LCL_RC_ERR;
    }
  }

  result = malloc(result_len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::replace: out of memory");
    return LCL_RC_ERR;
  }

  dst = result;
  p = src;

  while ((found = strstr(p, old_str)) != NULL) {
    size_t prefix_len = (size_t)(found - p);
    memcpy(dst, p, prefix_len);
    dst += prefix_len;
    memcpy(dst, new_str, new_len);
    dst += new_len;
    p = found + old_len;
  }

  strcpy(dst, p);

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* String::length s - return length of string */
static lcl_return_code c_string_length(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  const char *src;

  if (!lcl_std_chk_argc(interp, "String::length", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::length", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  *out = lcl_int_new((long)strlen(src));

  return LCL_RC_OK;
}

/* String::index s i - return character at index i as a string */
static lcl_return_code c_string_index(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  const char *src;
  long idx;
  size_t len;
  char buf[2];

  if (!lcl_std_chk_argc(interp, "String::index", argc, 2, 2)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::index", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);

  if (!lcl_std_arg_int(interp, "String::index", argv[1], &idx)) {
    return LCL_RC_ERR;
  }

  if (idx < 0) {
    idx = (long)len + idx;
  }

  if (idx < 0 || (size_t)idx >= len) {
    LCL_ERR_MSG(interp, "string index out of range");
    return LCL_RC_ERR;
  }

  buf[0] = src[idx];
  buf[1] = '\0';
  *out = lcl_string_new(buf);

  return LCL_RC_OK;
}

/* String::range s start end - return substring from start to end (exclusive) */
static lcl_return_code c_string_range(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  const char *src;
  long start;
  long end;
  size_t len;
  size_t sub_len;
  char *result;

  if (!lcl_std_chk_argc(interp, "String::range", argc, 3, 3)) {
    return LCL_RC_ERR;
  }

  if (!lcl_std_arg_str(interp, "String::range", argv[0], &src)) {
    return LCL_RC_ERR;
  }
  len = strlen(src);

  if (!lcl_std_arg_int(interp, "String::range", argv[1], &start) ||
      !lcl_std_arg_int(interp, "String::range", argv[2], &end)) {
    return LCL_RC_ERR;
  }

  if (start < 0) {
    start = (long)len + start;
  }

  if (end < 0) {
    end = (long)len + end;
  }

  if (start < 0) {
    start = 0;
  }

  if (end < 0) {
    end = 0;
  }

  if ((size_t)start > len) {
    start = (long)len;
  }

  if ((size_t)end > len) {
    end = (long)len;
  }

  if (start >= end) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  sub_len = (size_t)(end - start);
  result = malloc(sub_len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::range: out of memory");
    return LCL_RC_ERR;
  }

  memcpy(result, src + start, sub_len);
  result[sub_len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

/* String::trim s - remove leading and trailing whitespace */
static lcl_return_code c_string_trim(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  const char *src;
  const char *start;
  const char *end;
  size_t len;
  char *result;

  if (!lcl_std_chk_argc(interp, "String::trim", argc, 1, 1)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &src) != LCL_OK) {
    return LCL_RC_ERR;
  }
  start = src;

  while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
    start++;
  }

  if (*start == '\0') {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  end = start + strlen(start) - 1;

  while (end > start &&
         (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
    end--;
  }

  len = (size_t)(end - start + 1);
  result = malloc(len + 1);

  if (!result) {
    LCL_ERR_MSG(interp, "String::trim: out of memory");
    return LCL_RC_ERR;
  }

  memcpy(result, start, len);
  result[len] = '\0';

  *out = lcl_string_new(result);
  free(result);

  return LCL_RC_OK;
}

void lcl_std_register_string(lcl_interp *interp) {
  lcl_value *string_ns;

  lcl_register_proc(interp, "string?", c_is_string);
  string_ns = lcl_ns_new("String");
  lcl_define_take(interp, "String", string_ns);
  lcl_ns_def_take(string_ns, "from",
                  lcl_c_proc_new("String::from", c_string_from));
  lcl_ns_def_take(string_ns, "upper",
                  lcl_c_proc_new("String::upper", c_string_upper));
  lcl_ns_def_take(string_ns, "lower",
                  lcl_c_proc_new("String::lower", c_string_lower));
  lcl_ns_def_take(string_ns, "find",
                  lcl_c_proc_new("String::find", c_string_find));
  lcl_ns_def_take(string_ns, "replace",
                  lcl_c_proc_new("String::replace", c_string_replace));
  lcl_ns_def_take(string_ns, "split", lcl_c_proc_new("String::split", c_split));
  lcl_ns_def_take(string_ns, "join", lcl_c_proc_new("String::join", c_join));
  lcl_ns_def_take(string_ns, "length",
                  lcl_c_proc_new("String::length", c_string_length));
  lcl_ns_def_take(string_ns, "index",
                  lcl_c_proc_new("String::index", c_string_index));
  lcl_ns_def_take(string_ns, "range",
                  lcl_c_proc_new("String::range", c_string_range));
  lcl_ns_def_take(string_ns, "trim",
                  lcl_c_proc_new("String::trim", c_string_trim));
}
