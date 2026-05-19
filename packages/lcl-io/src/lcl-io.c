#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

int c_io_open_file(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_close_file(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_fgets(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
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

  if (buff_size <= 0 || buff_size > INT_MAX) {
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

  if (fgets(buff, (int)buff_size, handle) == NULL) {
    free(buff);
    return LCL_RC_BREAK;
  }

  *out = lcl_string_new(buff);

  free(buff);
  return LCL_RC_OK;
}

int c_io_fputs(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_read_file(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_write_file(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_stdout(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdout, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

int c_io_stderr(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stderr, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

int c_io_stdin(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  *out = lcl_opaque_new(stdin, FILE_HANDLE_TYPE_TAG, NULL);
  return LCL_RC_OK;
}

int c_io_flush(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_getenv(lcl_interp *interp, int argc, lcl_value **argv,
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

int c_io_glob(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  const char *gl;
  glob_t glob_buf;
  int err;
  size_t i;
  lcl_value *result;
  lcl_value *item;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &gl) != LCL_OK) {
    return LCL_RC_ERR;
  }

  err = glob(gl, 0, NULL, &glob_buf);

  if (err == 0) {
    result = lcl_list_new();

    for (i = 0; i < glob_buf.gl_pathc; i++) {
      item = lcl_string_new(glob_buf.gl_pathv[i]);
      lcl_list_push(&result, item);
      lcl_ref_dec(item);
    }

    globfree(&glob_buf);
    *out = result;

    return LCL_RC_OK;
  }

  return LCL_RC_ERR;
}

/* io::mkdir path ?mode? - create directory with optional mode (default 0755) */
int c_io_mkdir(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  const char *path;
  long mode = 0755;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    if (lcl_value_to_int(argv[1], &mode) != LCL_OK) {
      return LCL_RC_ERR;
    }
  }

  if (mkdir(path, (mode_t)mode) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* io::rmdir path - remove empty directory */
int c_io_rmdir(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  const char *path;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (rmdir(path) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* io::remove path - remove file */
int c_io_remove(lcl_interp *interp, int argc, lcl_value **argv,
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
int c_io_rename(lcl_interp *interp, int argc, lcl_value **argv,
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

/* io::exists? path - check if file or directory exists */
int c_io_exists(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  const char *path;
  struct stat st;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (stat(path, &st) == 0) {
    *out = lcl_int_new(1);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

/* io::is_file? path - check if path is a regular file */
int c_io_is_file(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  const char *path;
  struct stat st;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
    *out = lcl_int_new(1);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

/* io::is_dir? path - check if path is a directory */
int c_io_is_dir(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  const char *path;
  struct stat st;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
    *out = lcl_int_new(1);
  } else {
    *out = lcl_int_new(0);
  }

  return LCL_RC_OK;
}

/* io::file_size path - get file size in bytes */
int c_io_file_size(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  const char *path;
  struct stat st;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (stat(path, &st) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)st.st_size);
  return LCL_RC_OK;
}

/* io::readdir path - list directory contents */
int c_io_readdir(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  const char *path;
  DIR *dir;
  struct dirent *entry;
  lcl_value *result;
  lcl_value *item;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  dir = opendir(path);

  if (!dir) {
    return LCL_RC_ERR;
  }

  result = lcl_list_new();

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    item = lcl_string_new(entry->d_name);
    lcl_list_push(&result, item);
    lcl_ref_dec(item);
  }

  closedir(dir);
  *out = result;
  return LCL_RC_OK;
}

/* io::getcwd - get current working directory */
int c_io_getcwd(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  char *cwd;
  char buf[4096];
  (void)interp;
  (void)argc;
  (void)argv;

  cwd = getcwd(buf, sizeof(buf));

  if (!cwd) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(cwd);
  return LCL_RC_OK;
}

/* io::chdir path - change current working directory */
int c_io_chdir(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  const char *path;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (chdir(path) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* io::file_mtime path - get file modification time (Unix timestamp) */
int c_io_file_mtime(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  const char *path;
  struct stat st;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (stat(path, &st) != 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)st.st_mtime);
  return LCL_RC_OK;
}

/* io::copy src dst - copy file contents */
int c_io_copy(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
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

  lcl_ns_def(io_ns, "open_file",
             lcl_c_proc_new("io::open_file", c_io_open_file));
  lcl_ns_def(io_ns, "close_file",
             lcl_c_proc_new("io::close_file", c_io_close_file));
  lcl_ns_def(io_ns, "fgets", lcl_c_proc_new("io::fgets", c_io_fgets));
  lcl_ns_def(io_ns, "fputs", lcl_c_proc_new("io::fputs", c_io_fputs));

  lcl_ns_def(io_ns, "read_file",
             lcl_c_proc_new("io::read_file", c_io_read_file));
  lcl_ns_def(io_ns, "write_file",
             lcl_c_proc_new("io::write_file", c_io_write_file));
  lcl_ns_def(io_ns, "stdout", lcl_c_proc_new("io::stdout", c_io_stdout));
  lcl_ns_def(io_ns, "stderr", lcl_c_proc_new("io::stderr", c_io_stderr));
  lcl_ns_def(io_ns, "stdin", lcl_c_proc_new("io::stdin", c_io_stdin));
  lcl_ns_def(io_ns, "flush", lcl_c_proc_new("io::flush", c_io_flush));
  lcl_ns_def(io_ns, "getenv", lcl_c_proc_new("io::getenv", c_io_getenv));
  lcl_ns_def(io_ns, "glob", lcl_c_proc_new("io::glob", c_io_glob));

  /* Directory operations */
  lcl_ns_def(io_ns, "mkdir", lcl_c_proc_new("io::mkdir", c_io_mkdir));
  lcl_ns_def(io_ns, "rmdir", lcl_c_proc_new("io::rmdir", c_io_rmdir));
  lcl_ns_def(io_ns, "readdir", lcl_c_proc_new("io::readdir", c_io_readdir));

  /* File operations */
  lcl_ns_def(io_ns, "remove", lcl_c_proc_new("io::remove", c_io_remove));
  lcl_ns_def(io_ns, "rename", lcl_c_proc_new("io::rename", c_io_rename));
  lcl_ns_def(io_ns, "copy", lcl_c_proc_new("io::copy", c_io_copy));

  /* File/directory info */
  lcl_ns_def(io_ns, "exists?", lcl_c_proc_new("io::exists?", c_io_exists));
  lcl_ns_def(io_ns, "file?", lcl_c_proc_new("io::file?", c_io_is_file));
  lcl_ns_def(io_ns, "dir?", lcl_c_proc_new("io::dir?", c_io_is_dir));
  lcl_ns_def(io_ns, "file_size",
             lcl_c_proc_new("io::file_size", c_io_file_size));
  lcl_ns_def(io_ns, "file_mtime",
             lcl_c_proc_new("io::file_mtime", c_io_file_mtime));

  /* Working directory */
  lcl_ns_def(io_ns, "getcwd", lcl_c_proc_new("io::getcwd", c_io_getcwd));
  lcl_ns_def(io_ns, "chdir", lcl_c_proc_new("io::chdir", c_io_chdir));
}
