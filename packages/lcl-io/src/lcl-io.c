#include <errno.h>
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

  if (!f) return NULL;

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

int c_io_open_file(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  const char *filename;
  const char *file_perm;
  FILE *handle;
  lcl_value *handle_value;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  filename = lcl_value_to_string(argv[0]);
  file_perm = lcl_value_to_string(argv[1]);
  handle = fopen(filename, file_perm);

  if (!handle) {
    return LCL_RC_ERR;
  }

  handle_value = lcl_opaque_new(handle, FILE_HANDLE_TYPE_TAG, (void *)NULL);

  *out = handle_value;

  return LCL_RC_OK;
}

int c_io_close_file(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  FILE *handle = NULL;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void**)&handle) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  /* Don't close standard streams */
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

int c_io_fgets(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  FILE *handle = NULL;
  long buff_size;
  char *buff;

  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &buff_size) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void**)&handle) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  buff = malloc(sizeof(char) * buff_size);

  if (fgets(buff, buff_size, handle) == NULL) {
    free(buff);
    return LCL_RC_BREAK;
  }

  *out = lcl_string_new(buff);

  free(buff);
  return LCL_RC_OK;
}

int c_io_fputs(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  FILE *handle = NULL;
  const char *str;

  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void**)&handle) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  str = lcl_value_to_string(argv[1]);

  if (fputs(str, handle) == EOF) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

int c_io_read_file(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  char *contents = NULL;
  const char *path;

  (void)interp;
  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[0]);
  contents = read_file(path);

  if (!contents) {
    free(contents);
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(contents);
  free(contents);

  return LCL_RC_OK;
}

int c_io_write_file(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  const char *path;
  const char *contents;
  size_t len;
  FILE *f;

  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[0]);
  contents = lcl_value_to_string(argv[1]);
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

int c_io_stdout(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdout, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

int c_io_stderr(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stderr, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

int c_io_stdin(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdin, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

int c_io_flush(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  FILE *handle = NULL;

  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void**)&handle) != LCL_OK) {
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

void lcl_register_io(lcl_interp *interp) {
  lcl_value *io_ns = lcl_ns_new(IO_NS);
  lcl_define_take(interp, IO_NS, io_ns);

  lcl_ns_def(io_ns, "open_file", lcl_c_proc_new("io::open_file", c_io_open_file));
  lcl_ns_def(io_ns, "close_file", lcl_c_proc_new("io::close_file", c_io_close_file));
  lcl_ns_def(io_ns, "fgets", lcl_c_proc_new("io::fgets", c_io_fgets));
  lcl_ns_def(io_ns, "fputs", lcl_c_proc_new("io::fputs", c_io_fputs));
  lcl_ns_def(io_ns, "read_file", lcl_c_proc_new("io::read_file", c_io_read_file));
  lcl_ns_def(io_ns, "write_file", lcl_c_proc_new("io::write_file", c_io_write_file));
  lcl_ns_def(io_ns, "stdout", lcl_c_proc_new("io::stdout", c_io_stdout));
  lcl_ns_def(io_ns, "stderr", lcl_c_proc_new("io::stderr", c_io_stderr));
  lcl_ns_def(io_ns, "stdin", lcl_c_proc_new("io::stdin", c_io_stdin));
  lcl_ns_def(io_ns, "flush", lcl_c_proc_new("io::flush", c_io_flush));
}
