#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <lcl.h>

#define POSIX_NS "Posix"

lcl_return_code c_posix_glob(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  const char *gl;
  glob_t glob_buf;
  int glob_result;
  int flags = 0;
  int a;
  size_t i;
  lcl_value *result;
  lcl_value *item;

  if (argc < 1) {
    lcl_set_error(interp, "Posix::glob: expected at least 1 pattern");
    return LCL_RC_ERR;
  }

  for (a = 0; a < argc; a++) {
    if (lcl_value_to_cstring(interp, argv[a], &gl) != LCL_OK) {
      if (flags & GLOB_APPEND) {
        globfree(&glob_buf);
      }
      return LCL_RC_ERR;
    }

    glob_result = glob(gl, flags, NULL, &glob_buf);

    if (glob_result != 0 && glob_result != GLOB_NOMATCH) {
      globfree(&glob_buf);
      lcl_set_error(interp, "Posix::glob: glob failed");
      return LCL_RC_ERR;
    }

    flags |= GLOB_APPEND;
  }

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

/* Posix::mkdir path ?mode? - create directory with optional mode (default 0755)
 */
lcl_return_code c_posix_mkdir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::rmdir path - remove empty directory */
lcl_return_code c_posix_rmdir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::exists? path - check if file or directory exists */
lcl_return_code c_posix_exists(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::file? path - check if path is a regular file */
lcl_return_code c_posix_is_file(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::dir? path - check if path is a directory */
lcl_return_code c_posix_is_dir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::file_size path - get file size in bytes */
lcl_return_code c_posix_file_size(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
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

/* Posix::file_mtime path - get file modification time (Unix timestamp) */
lcl_return_code c_posix_file_mtime(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
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

/* Posix::readdir path - list directory contents */
lcl_return_code c_posix_readdir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::getcwd - get current working directory */
lcl_return_code c_posix_getcwd(lcl_interp *interp, int argc, lcl_value **argv,
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

/* Posix::chdir path - change current working directory */
lcl_return_code c_posix_chdir(lcl_interp *interp, int argc, lcl_value **argv,
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

lcl_return_code c_posix_realpath(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  const char *filename;
  lcl_value *path_value = NULL;
  char *actual_path = NULL;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &filename) != LCL_OK) {
    return LCL_RC_ERR;
  }

  actual_path = realpath(filename, NULL);

  if (actual_path == NULL) {
    return LCL_RC_ERR;
  }

  path_value = lcl_string_new(actual_path);

  if (path_value == NULL) {
    free(actual_path);
    return LCL_RC_ERR;
  }

  *out = path_value;

  free(actual_path);

  return LCL_RC_OK;
}

/* dirname/basename(3) modify their argument in place, so work on a
 * private copy rather than the value's borrowed string. */
static lcl_return_code posix_path_part(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out,
                                       char *(*part)(char *)) {
  const char *path;
  char buf[PATH_MAX];
  size_t n;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &path) != LCL_OK) {
    return LCL_RC_ERR;
  }

  n = strlen(path);

  if (n >= sizeof buf) {
    return LCL_RC_ERR;
  }

  memcpy(buf, path, n + 1);
  *out = lcl_string_new(part(buf));

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

lcl_return_code c_posix_dirname(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  return posix_path_part(interp, argc, argv, out, dirname);
}

lcl_return_code c_posix_basename(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  return posix_path_part(interp, argc, argv, out, basename);
}

void lcl_register_posix(lcl_interp *interp) {
  lcl_value *posix_ns = lcl_ns_new(POSIX_NS);
  lcl_define_take(interp, POSIX_NS, posix_ns);

  lcl_ns_def_take(posix_ns, "glob",
                  lcl_c_proc_new("Posix::glob", c_posix_glob));

  /* Directory operations */
  lcl_ns_def_take(posix_ns, "mkdir",
                  lcl_c_proc_new("Posix::mkdir", c_posix_mkdir));
  lcl_ns_def_take(posix_ns, "rmdir",
                  lcl_c_proc_new("Posix::rmdir", c_posix_rmdir));
  lcl_ns_def_take(posix_ns, "readdir",
                  lcl_c_proc_new("Posix::readdir", c_posix_readdir));

  /* File/directory info */
  lcl_ns_def_take(posix_ns, "exists?",
                  lcl_c_proc_new("Posix::exists?", c_posix_exists));
  lcl_ns_def_take(posix_ns, "file?",
                  lcl_c_proc_new("Posix::file?", c_posix_is_file));
  lcl_ns_def_take(posix_ns, "dir?",
                  lcl_c_proc_new("Posix::dir?", c_posix_is_dir));
  lcl_ns_def_take(posix_ns, "file_size",
                  lcl_c_proc_new("Posix::file_size", c_posix_file_size));
  lcl_ns_def_take(posix_ns, "file_mtime",
                  lcl_c_proc_new("Posix::file_mtime", c_posix_file_mtime));
  lcl_ns_def_take(posix_ns, "dirname",
                  lcl_c_proc_new("Posix::dirname", c_posix_dirname));
  lcl_ns_def_take(posix_ns, "basename",
                  lcl_c_proc_new("Posix::basename", c_posix_basename));

  /* Working directory */
  lcl_ns_def_take(posix_ns, "getcwd",
                  lcl_c_proc_new("Posix::getcwd", c_posix_getcwd));
  lcl_ns_def_take(posix_ns, "chdir",
                  lcl_c_proc_new("Posix::chdir", c_posix_chdir));

  lcl_ns_def_take(posix_ns, "realpath",
                  lcl_c_proc_new("Posix::realpath", c_posix_realpath));
}
