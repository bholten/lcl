#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>

#define FILE_HANDLE_TYPE_TAG "file_handle_type_tag"
#define IO_NS "Io"

static void cat_bounded(char *buf, size_t cap, const char *s) {
  size_t n = strlen(buf);
  size_t m = strlen(s);

  if (n >= cap - 1) {
    return;
  }

  if (m > cap - 1 - n) {
    m = cap - 1 - n;
  }

  memcpy(buf + n, s, m);
  buf[n + m] = '\0';
}

static lcl_return_code io_fail(lcl_interp *interp, const char *who,
                               const char *what, const char *subject, int err) {
  char buf[512];

  buf[0] = '\0';
  cat_bounded(buf, sizeof buf, who);
  cat_bounded(buf, sizeof buf, ": ");
  cat_bounded(buf, sizeof buf, what);

  if (subject) {
    cat_bounded(buf, sizeof buf, " '");
    cat_bounded(buf, sizeof buf, subject);
    cat_bounded(buf, sizeof buf, "'");
  }

  if (err) {
    cat_bounded(buf, sizeof buf, ": ");
    cat_bounded(buf, sizeof buf, strerror(err));
  }

  lcl_set_error(interp, buf);
  return LCL_RC_ERR;
}

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

static lcl_return_code c_io_open_file(lcl_interp *interp, int argc, lcl_value **argv,
                                      lcl_value **out) {
  const char *filename;
  const char *file_perm;
  FILE *handle;
  lcl_value *handle_value;

  if (argc != 2) {
    return io_fail(interp, "Io::open_file", "expected 2 arguments (path mode)",
                   NULL, 0);
  }

  if (lcl_value_to_cstring(interp, argv[0], &filename) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &file_perm) != LCL_OK) {
    return LCL_RC_ERR;
  }

  handle = fopen(filename, file_perm);

  if (!handle) {
    return io_fail(interp, "Io::open_file", "cannot open", filename, errno);
  }

  handle_value = lcl_opaque_new(handle, FILE_HANDLE_TYPE_TAG, NULL);

  *out = handle_value;

  return LCL_RC_OK;
}

static lcl_return_code c_io_close_file(lcl_interp *interp, int argc, lcl_value **argv,
                                       lcl_value **out) {
  FILE *handle = NULL;

  if (argc != 1) {
    return io_fail(interp, "Io::close_file", "expected 1 argument (handle)",
                   NULL, 0);
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return io_fail(interp, "Io::close_file", "expected a file handle", NULL, 0);
  }

  if (!handle) {
    return io_fail(interp, "Io::close_file", "file is closed", NULL, 0);
  }

  if (handle == stdin || handle == stdout || handle == stderr) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  if (fclose(handle) == EOF) {
    return io_fail(interp, "Io::close_file", "close failed", NULL, errno);
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static lcl_return_code c_io_is_eof(lcl_interp *interp, int argc, lcl_value **argv,
                                   lcl_value **out) {
  FILE *handle = NULL;

  if (argc != 1) {
    return io_fail(interp, "Io::eof?", "expected 1 argument (handle)", NULL, 0);
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return io_fail(interp, "Io::eof?", "expected a file handle", NULL, 0);
  }

  if (!handle) {
    return io_fail(interp, "Io::eof?", "file is closed", NULL, 0);
  }

  *out = lcl_int_new(feof(handle) ? 1 : 0);
  return LCL_RC_OK;
}

lcl_return_code c_io_fgets(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  FILE *handle = NULL;
  lcl_int buff_size;
  char *buff;

  if (argc < 2) {
    lcl_set_error(interp, "Io::fgets: expected 2 arguments (handle size)");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &buff_size) != LCL_OK) {
    lcl_set_error(interp, "Io::fgets: size must be an int");
    return LCL_RC_ERR;
  }

  if (buff_size < 2 || buff_size > INT_MAX) {
    lcl_set_error(interp, "Io::fgets: size must be between 2 and INT_MAX");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
          LCL_OK ||
      !handle) {
    lcl_set_error(interp, "Io::fgets: expected an open file handle");
    return LCL_RC_ERR;
  }

  buff = malloc((size_t)buff_size);

  if (!buff) {
    lcl_set_error(interp, "Io::fgets: out of memory");
    return LCL_RC_ERR;
  }

  if (fgets(buff, (int)buff_size, handle) == NULL) {
    free(buff);

    if (ferror(handle)) {
      lcl_set_error(interp, "Io::fgets: read error");
      return LCL_RC_ERR;
    }

    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  *out = lcl_string_new(buff);

  free(buff);
  return LCL_RC_OK;
}

static lcl_return_code c_io_fputs(lcl_interp *interp, int argc, lcl_value **argv,
                                  lcl_value **out) {
  FILE *handle = NULL;
  const char *str;

  if (argc != 2) {
    return io_fail(interp, "Io::fputs", "expected 2 arguments (handle text)",
                   NULL, 0);
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return io_fail(interp, "Io::fputs", "expected a file handle", NULL, 0);
  }

  if (!handle) {
    return io_fail(interp, "Io::fputs", "file is closed", NULL, 0);
  }

  if (lcl_value_to_cstring(interp, argv[1], &str) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (fputs(str, handle) == EOF) {
    return io_fail(interp, "Io::fputs", "write failed", NULL, errno);
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static lcl_return_code c_io_read_file(lcl_interp *interp, int argc, lcl_value **argv,
                                      lcl_value **out) {
  char *contents = NULL;
  const char *path;

  if (argc != 1) {
    lcl_set_error(interp, "Io::read_file: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  contents = read_file(path);

  if (!contents) {
    char msg[433];
    sprintf(msg, "Io::read_file: could not read \"%.400s\"", path);
    lcl_set_error(interp, msg);
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(contents);
  free(contents);

  return LCL_RC_OK;
}

static lcl_return_code c_io_write_file(lcl_interp *interp, int argc, lcl_value **argv,
                                       lcl_value **out) {
  const char *path;
  const char *contents;
  size_t len;
  FILE *f;

  if (argc != 2) {
    lcl_set_error(interp, "Io::write_file: expected 2 arguments");
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
    char msg[434];
    sprintf(msg, "Io::write_file: could not open \"%.400s\"", path);
    lcl_set_error(interp, msg);
    return LCL_RC_ERR;
  }

  if (fwrite(contents, 1, len, f) != len) {
    char msg[434];
    fclose(f);
    sprintf(msg, "Io::write_file: short write to \"%.400s\"", path);
    lcl_set_error(interp, msg);
    return LCL_RC_ERR;
  }

  fclose(f);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static lcl_return_code c_io_stdout(lcl_interp *interp, int argc, lcl_value **argv,
                                   lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdout, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

static lcl_return_code c_io_stderr(lcl_interp *interp, int argc, lcl_value **argv,
                                   lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stderr, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

static lcl_return_code c_io_stdin(lcl_interp *interp, int argc, lcl_value **argv,
                                  lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdin, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

static lcl_return_code c_io_flush(lcl_interp *interp, int argc, lcl_value **argv,
                                  lcl_value **out) {
  FILE *handle = NULL;

  if (argc != 1) {
    return io_fail(interp, "Io::flush", "expected 1 argument (handle)", NULL,
                   0);
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void **)&handle) !=
      LCL_OK) {
    return io_fail(interp, "Io::flush", "expected a file handle", NULL, 0);
  }

  if (!handle) {
    return io_fail(interp, "Io::flush", "file is closed", NULL, 0);
  }

  if (fflush(handle) == EOF) {
    return io_fail(interp, "Io::flush", "flush failed", NULL, errno);
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

static lcl_return_code c_io_getenv(lcl_interp *interp, int argc, lcl_value **argv,
                                   lcl_value **out) {
  const char *env;
  const char *env_val;

  if (argc != 1) {
    return io_fail(interp, "Io::getenv", "expected 1 argument (name)", NULL, 0);
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

/* Io::remove path - remove file */
static lcl_return_code c_io_remove(lcl_interp *interp, int argc, lcl_value **argv,
                                   lcl_value **out) {
  const char *path;

  if (argc != 1) {
    return io_fail(interp, "Io::remove", "expected 1 argument (path)", NULL, 0);
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (remove(path) != 0) {
    return io_fail(interp, "Io::remove", "cannot remove", path, errno);
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* Io::rename old new - rename/move file or directory */
static lcl_return_code c_io_rename(lcl_interp *interp, int argc, lcl_value **argv,
                                   lcl_value **out) {
  const char *old_path;
  const char *new_path;

  if (argc != 2) {
    return io_fail(interp, "Io::rename", "expected 2 arguments (old new)", NULL,
                   0);
  }

  if (lcl_value_to_cstring(interp, argv[0], &old_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &new_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (rename(old_path, new_path) != 0) {
    return io_fail(interp, "Io::rename", "cannot rename", old_path, errno);
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* Io::copy src dst - copy file contents */
static lcl_return_code c_io_copy(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  const char *src_path;
  const char *dst_path;
  FILE *src;
  FILE *dst;
  char buf[8192];
  size_t n;

  if (argc != 2) {
    return io_fail(interp, "Io::copy", "expected 2 arguments (src dst)", NULL,
                   0);
  }

  if (lcl_value_to_cstring(interp, argv[0], &src_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &dst_path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  src = fopen(src_path, "rb");

  if (!src) {
    return io_fail(interp, "Io::copy", "cannot open", src_path, errno);
  }

  dst = fopen(dst_path, "wb");

  if (!dst) {
    fclose(src);
    return io_fail(interp, "Io::copy", "cannot create", dst_path, errno);
  }

  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) {
      int err = errno;

      fclose(src);
      fclose(dst);
      return io_fail(interp, "Io::copy", "write failed", dst_path, err);
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
                  lcl_c_proc_new("Io::open_file", c_io_open_file));
  lcl_ns_def_take(io_ns, "close_file",
                  lcl_c_proc_new("Io::close_file", c_io_close_file));
  lcl_ns_def_take(io_ns, "eof?", lcl_c_proc_new("Io::eof?", c_io_is_eof));
  lcl_ns_def_take(io_ns, "fgets", lcl_c_proc_new("Io::fgets", c_io_fgets));
  lcl_ns_def_take(io_ns, "fputs", lcl_c_proc_new("Io::fputs", c_io_fputs));

  lcl_ns_def_take(io_ns, "read_file",
                  lcl_c_proc_new("Io::read_file", c_io_read_file));
  lcl_ns_def_take(io_ns, "write_file",
                  lcl_c_proc_new("Io::write_file", c_io_write_file));
  lcl_ns_def_take(io_ns, "stdout", lcl_c_proc_new("Io::stdout", c_io_stdout));
  lcl_ns_def_take(io_ns, "stderr", lcl_c_proc_new("Io::stderr", c_io_stderr));
  lcl_ns_def_take(io_ns, "stdin", lcl_c_proc_new("Io::stdin", c_io_stdin));
  lcl_ns_def_take(io_ns, "flush", lcl_c_proc_new("Io::flush", c_io_flush));
  lcl_ns_def_take(io_ns, "getenv", lcl_c_proc_new("Io::getenv", c_io_getenv));

  /* File operations */
  lcl_ns_def_take(io_ns, "remove", lcl_c_proc_new("Io::remove", c_io_remove));
  lcl_ns_def_take(io_ns, "rename", lcl_c_proc_new("Io::rename", c_io_rename));
  lcl_ns_def_take(io_ns, "copy", lcl_c_proc_new("Io::copy", c_io_copy));
}
