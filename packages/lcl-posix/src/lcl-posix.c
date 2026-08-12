#include <dirent.h>
#include <glob.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <lcl.h>

#define POSIX_NS "posix"

int c_posix_glob(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
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

/* posix::mkdir path ?mode? - create directory with optional mode (default 0755) */
int c_posix_mkdir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::rmdir path - remove empty directory */
int c_posix_rmdir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::exists? path - check if file or directory exists */
int c_posix_exists(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::file? path - check if path is a regular file */
int c_posix_is_file(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::dir? path - check if path is a directory */
int c_posix_is_dir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::file_size path - get file size in bytes */
int c_posix_file_size(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::file_mtime path - get file modification time (Unix timestamp) */
int c_posix_file_mtime(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::readdir path - list directory contents */
int c_posix_readdir(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::getcwd - get current working directory */
int c_posix_getcwd(lcl_interp *interp, int argc, lcl_value **argv,
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

/* posix::chdir path - change current working directory */
int c_posix_chdir(lcl_interp *interp, int argc, lcl_value **argv,
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

void lcl_register_posix(lcl_interp *interp) {
  lcl_value *posix_ns = lcl_ns_new(POSIX_NS);
  lcl_define_take(interp, POSIX_NS, posix_ns);

  lcl_ns_def(posix_ns, "glob", lcl_c_proc_new("posix::glob", c_posix_glob));

  /* Directory operations */
  lcl_ns_def(posix_ns, "mkdir", lcl_c_proc_new("posix::mkdir", c_posix_mkdir));
  lcl_ns_def(posix_ns, "rmdir", lcl_c_proc_new("posix::rmdir", c_posix_rmdir));
  lcl_ns_def(posix_ns, "readdir",
             lcl_c_proc_new("posix::readdir", c_posix_readdir));

  /* File/directory info */
  lcl_ns_def(posix_ns, "exists?",
             lcl_c_proc_new("posix::exists?", c_posix_exists));
  lcl_ns_def(posix_ns, "file?", lcl_c_proc_new("posix::file?", c_posix_is_file));
  lcl_ns_def(posix_ns, "dir?", lcl_c_proc_new("posix::dir?", c_posix_is_dir));
  lcl_ns_def(posix_ns, "file_size",
             lcl_c_proc_new("posix::file_size", c_posix_file_size));
  lcl_ns_def(posix_ns, "file_mtime",
             lcl_c_proc_new("posix::file_mtime", c_posix_file_mtime));

  /* Working directory */
  lcl_ns_def(posix_ns, "getcwd",
             lcl_c_proc_new("posix::getcwd", c_posix_getcwd));
  lcl_ns_def(posix_ns, "chdir", lcl_c_proc_new("posix::chdir", c_posix_chdir));
}
