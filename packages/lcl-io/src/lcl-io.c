#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>

#define FILE_HANDLE_TYPE_TAG "file_handle_type_tag"
#define IO_NS "io"

static char *read_file(const char *path) {
  FILE *f;
  long len;
  char *buf;
  size_t nread;

  f = fopen(path, "rb");

  if (!f) {
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  len = ftell(f);
  if (len < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  buf = malloc((size_t)len + 1);

  if (!buf) {
    fclose(f);
    return NULL;
  }

  nread = fread(buf, 1, (size_t)len, f);
  fclose(f);

  if ((long)nread != len) {
    free(buf);
    return NULL;
  }

  buf[len] = '\0';

  return buf;
}

lcl_return_code c_io_open_file(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  const char *filename;
  const char *file_perm;
  FILE *handle;
  lcl_value *handle_value;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &filename) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &file_perm) != LCL_OK) {
    return LCL_RC_ERR;
  }

  handle = fopen(filename, file_perm);

  if (!handle) {
    return LCL_RC_ERR;
  }

  handle_value = lcl_opaque_new(handle, FILE_HANDLE_TYPE_TAG, (void *)NULL);

  *out = handle_value;

  return LCL_RC_OK;
}

lcl_return_code c_io_close_file(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  FILE *handle = NULL;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  if (handle == stdin || handle == stdout || handle == stderr) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  if (fclose(handle) == EOF) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

lcl_return_code c_io_is_eof(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  FILE *handle = NULL;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(feof(handle) ? 1 : 0);
  return LCL_RC_OK;
}

lcl_return_code c_io_fgets(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  FILE *handle = NULL;
  long buff_size;
  char *buff;

  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &buff_size) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (buff_size < 2 || buff_size > INT_MAX) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  buff = malloc((size_t)buff_size);

  if (!buff) {
    return LCL_RC_ERR;
  }

  if (fgets(buff, (int)buff_size, handle) == NULL) {
    free(buff);

    if (ferror(handle)) {
      return LCL_RC_ERR;
    }

    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  *out = lcl_string_new(buff);

  free(buff);
  return LCL_RC_OK;
}

lcl_return_code c_io_fputs(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  FILE *handle = NULL;
  const char *str;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (fputs(str, handle) == EOF) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

lcl_return_code c_io_read_file(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  char *contents = NULL;
  const char *path;

  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }
  contents = read_file(path);

  if (!contents) {
    free(contents);
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(contents);
  free(contents);

  return LCL_RC_OK;
}

lcl_return_code c_io_write_file(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  const char *path;
  const char *contents;
  size_t len;
  FILE *f;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &contents) != LCL_OK) {
    return LCL_RC_ERR;
  }

  len = strlen(contents);

  f = fopen(path, "wb");

  if (!f) {
    return LCL_RC_ERR;
  }

  if (fwrite(contents, 1, len, f) != len) {
    fclose(f);
    return LCL_RC_ERR;
  }

  fclose(f);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

lcl_return_code c_io_stdout(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdout, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

lcl_return_code c_io_stderr(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stderr, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

lcl_return_code c_io_stdin(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdin, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

lcl_return_code c_io_flush(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  FILE *handle = NULL;

  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  if (fflush(handle) == EOF) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

lcl_return_code c_io_getenv(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  const char *env;
  const char *env_val;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &env) != LCL_OK) {
    return LCL_RC_ERR;
  }

  env_val = getenv(env);

  if (!env_val) {
    *out = lcl_string_new("");
  } else {
    *out = lcl_string_new(env_val);
  }

  return LCL_RC_OK;
}

/* io::remove path - remove file */
lcl_return_code c_io_remove(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  const char *path;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (remove(path) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* io::rename old new - rename/move file or directory */
lcl_return_code c_io_rename(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  const char *old_path;
  const char *new_path;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &old_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &new_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (rename(old_path, new_path) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* io::copy src dst - copy file contents */
lcl_return_code c_io_copy(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  const char *src_path;
  const char *dst_path;
  FILE *src;
  FILE *dst;
  char buf[8192];
  size_t n;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &src_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &dst_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  src = fopen(src_path, "rb");

  if (!src) {
    return LCL_RC_ERR;
  }

  dst = fopen(dst_path, "wb");

  if (!dst) {
    fclose(src);
    return LCL_RC_ERR;
  }

  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) {
      fclose(src);
      fclose(dst);
      return LCL_RC_ERR;
    }
  }

  fclose(src);
  fclose(dst);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

void lcl_register_io(lcl_interp *interp) {
  lcl_value *io_ns = lcl_ns_new(IO_NS);
  lcl_define_take(interp, IO_NS, io_ns);

  lcl_ns_def_take(io_ns, "open_file",
                  lcl_c_proc_new("io::open_file", c_io_open_file));
  lcl_ns_def_take(io_ns, "close_file",
                  lcl_c_proc_new("io::close_file", c_io_close_file));
  lcl_ns_def_take(io_ns, "eof?", lcl_c_proc_new("io::eof?", c_io_is_eof));
  lcl_ns_def_take(io_ns, "fgets", lcl_c_proc_new("io::fgets", c_io_fgets));
  lcl_ns_def_take(io_ns, "fputs", lcl_c_proc_new("io::fputs", c_io_fputs));

  lcl_ns_def_take(io_ns, "read_file",
                  lcl_c_proc_new("io::read_file", c_io_read_file));
  lcl_ns_def_take(io_ns, "write_file",
                  lcl_c_proc_new("io::write_file", c_io_write_file));
  lcl_ns_def_take(io_ns, "stdout", lcl_c_proc_new("io::stdout", c_io_stdout));
  lcl_ns_def_take(io_ns, "stderr", lcl_c_proc_new("io::stderr", c_io_stderr));
  lcl_ns_def_take(io_ns, "stdin", lcl_c_proc_new("io::stdin", c_io_stdin));
  lcl_ns_def_take(io_ns, "flush", lcl_c_proc_new("io::flush", c_io_flush));
  lcl_ns_def_take(io_ns, "getenv", lcl_c_proc_new("io::getenv", c_io_getenv));

  /* File operations */
  lcl_ns_def_take(io_ns, "remove", lcl_c_proc_new("io::remove", c_io_remove));
  lcl_ns_def_take(io_ns, "rename", lcl_c_proc_new("io::rename", c_io_rename));
  lcl_ns_def_take(io_ns, "copy", lcl_c_proc_new("io::copy", c_io_copy));
}
