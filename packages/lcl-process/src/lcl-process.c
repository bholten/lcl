#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||      \
    defined(__NetBSD__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#endif

#include <lcl.h>

#define PROCESS_HANDLE_TYPE_TAG "process_handle"
#define PROCESS_NS "process"

#define INITIAL_BUF_SIZE 4096
#define MAX_BUF_SIZE (4 * 1024 * 1024)

typedef struct {
  pid_t pid;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
  int exited;
  int status;
  int signal_num;

  int pty_master;
  int is_pty;

  char *stdout_buf;
  size_t stdout_len;
  size_t stdout_cap;
  char *stderr_buf;
  size_t stderr_len;
  size_t stderr_cap;
} process_handle;

static void process_handle_finalizer(void *ptr);
static process_handle *get_handle(lcl_value *v);
static int set_nonblocking(int fd);

static process_handle *process_handle_new(void) {
  process_handle *h = (process_handle *)calloc(1, sizeof(process_handle));

  if (!h) {
    return NULL;
  }

  h->pid = -1;
  h->stdin_fd = -1;
  h->stdout_fd = -1;
  h->stderr_fd = -1;
  h->exited = 0;
  h->status = 0;
  h->signal_num = 0;

  h->pty_master = -1;
  h->is_pty = 0;

  h->stdout_buf = NULL;
  h->stdout_len = 0;
  h->stdout_cap = 0;
  h->stderr_buf = NULL;
  h->stderr_len = 0;
  h->stderr_cap = 0;

  return h;
}

static void process_handle_finalizer(void *ptr) {
  process_handle *h = (process_handle *)ptr;
  if (!h) {
    return;
  }

  if (h->is_pty) {
    if (h->pty_master >= 0) {
      close(h->pty_master);
    }
  } else {
    if (h->stdin_fd >= 0) {
      close(h->stdin_fd);
    }

    if (h->stdout_fd >= 0) {
      close(h->stdout_fd);
    }

    if (h->stderr_fd >= 0) {
      close(h->stderr_fd);
    }
  }

  free(h->stdout_buf);
  free(h->stderr_buf);
  free(h);
}

static process_handle *get_handle(lcl_value *v) {
  void *ptr = NULL;

  if (lcl_opaque_get(v, PROCESS_HANDLE_TYPE_TAG, &ptr) != LCL_OK) {
    return NULL;
  }

  return (process_handle *)ptr;
}

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);

  if (flags < 0) {
    return -1;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static char *read_all(int fd, size_t *out_len, size_t limit) {
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  char tmp[4096];
  ssize_t n;

  while ((n = read(fd, tmp, sizeof(tmp))) > 0) {
    if (len + (size_t)n > limit) {
      n = (ssize_t)(limit - len);

      if (n <= 0) {
        break;
      }
    }

    if (len + (size_t)n >= cap) {
      size_t newcap = cap ? cap * 2 : INITIAL_BUF_SIZE;
      char *newbuf;

      while (newcap < len + (size_t)n + 1) {
        newcap *= 2;
      }

      if (newcap > limit) {
        newcap = limit + 1;
      }

      newbuf = (char *)realloc(buf, newcap);

      if (!newbuf) {
        free(buf);
        return NULL;
      }

      buf = newbuf;
      cap = newcap;
    }

    memcpy(buf + len, tmp, (size_t)n);
    len += (size_t)n;
  }

  if (buf) {
    buf[len] = '\0';
  } else {
    buf = (char *)malloc(1);

    if (buf) {
      buf[0] = '\0';
    }
  }

  *out_len = len;

  return buf;
}

static int get_opt_int(lcl_value *opts, const char *key, int def) {
  lcl_value *v = NULL;
  long n;
  int result;

  if (!opts) {
    return def;
  }

  if (lcl_dict_get(opts, key, &v) != LCL_OK) {
    return def;
  }

  if (lcl_value_to_int(v, &n) != LCL_OK) {
    lcl_ref_dec(v);
    return def;
  }

  result = (int)n;
  lcl_ref_dec(v);

  return result;
}

static const char *get_opt_str(lcl_value *opts, const char *key,
                               const char *def) {
  lcl_value *v = NULL;
  const char *result;

  if (!opts) {
    return def;
  }

  if (lcl_dict_get(opts, key, &v) != LCL_OK) {
    return def;
  }

  result = lcl_value_to_string(v);
  lcl_ref_dec(v);

  if (!result) {
    return def;
  }

  return result;
}

static lcl_value *get_opt_val(lcl_value *opts, const char *key) {
  lcl_value *v = NULL;

  if (!opts) {
    return NULL;
  }

  if (lcl_dict_get(opts, key, &v) != LCL_OK) {
    return NULL;
  }

  return v;
}

/*
 * process::run argv ?opts?
 *
 * Options (dict):
 *   stdin   - string to send to stdin
 *   timeout - ms before giving up (0 = no timeout) [not yet implemented]
 *   env     - dict of environment vars
 *   cwd     - working directory
 *   merge   - bool, merge stderr into stdout
 *   throw   - bool, throw on non-zero exit
 *   limit   - max bytes to capture per stream
 *   shell   - bool, run through /bin/sh -c
 *
 * Returns: #{status N stdout "..." stderr "..."}
 */
static lcl_return_code c_process_run(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lcl_value *argv_list;
  lcl_value *opts = NULL;
  size_t argv_len;
  char **exec_argv = NULL;
  size_t i;
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  pid_t pid;
  int status = 0;
  char *stdout_data = NULL;
  char *stderr_data = NULL;
  size_t stdout_len = 0;
  size_t stderr_len = 0;
  lcl_value *result;
  lcl_value *tmp;
  const char *stdin_str;
  const char *cwd;
  lcl_value *env_dict;
  int merge;
  int do_throw;
  int use_shell;
  size_t limit;
  lcl_return_code rc = LCL_RC_OK;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  argv_list = argv[0];

  if (argc >= 2) {
    opts = argv[1];
  }

  stdin_str = get_opt_str(opts, "stdin", NULL);
  cwd = get_opt_str(opts, "cwd", NULL);
  env_dict = get_opt_val(opts, "env");
  merge = get_opt_int(opts, "merge", 0);
  do_throw = get_opt_int(opts, "throw", 0);
  use_shell = get_opt_int(opts, "shell", 0);
  limit = (size_t)get_opt_int(opts, "limit", (int)MAX_BUF_SIZE);
  argv_len = lcl_list_len(argv_list);

  if (argv_len == 0) {
    return LCL_RC_ERR;
  }

  if (use_shell) {
    char *cmd = NULL;
    size_t cmd_len = 0;
    size_t j;

    for (j = 0; j < argv_len; j++) {
      lcl_value *arg = NULL;
      const char *s;
      size_t slen;

      lcl_list_get(argv_list, j, &arg);
      s = lcl_value_to_string(arg);

      if (!s) {
        lcl_ref_dec(arg);
        free(cmd);
        lcl_set_error(interp, "out of memory");
        return LCL_RC_ERR;
      }

      slen = strlen(s);

      {
        char *new_cmd = (char *)realloc(cmd, cmd_len + slen + 2);

        if (!new_cmd) {
          lcl_ref_dec(arg);
          free(cmd);
          return LCL_RC_ERR;
        }

        cmd = new_cmd;
      }

      if (j > 0) {
        cmd[cmd_len++] = ' ';
      }
      memcpy(cmd + cmd_len, s, slen);
      cmd_len += slen;
      lcl_ref_dec(arg);
    }

    cmd[cmd_len] = '\0';

    exec_argv = (char **)malloc(4 * sizeof(char *));

    if (!exec_argv) {
      free(cmd);
      return LCL_RC_ERR;
    }

    exec_argv[0] = strdup("/bin/sh");
    exec_argv[1] = strdup("-c");
    exec_argv[2] = cmd;
    exec_argv[3] = NULL;
  } else {
    exec_argv = (char **)malloc((argv_len + 1) * sizeof(char *));

    if (!exec_argv) {
      return LCL_RC_ERR;
    }

    for (i = 0; i < argv_len; i++) {
      lcl_value *arg = NULL;
      const char *s;
      lcl_list_get(argv_list, i, &arg);
      s = lcl_value_to_string(arg);

      if (!s) {
        size_t k;

        for (k = 0; k < i; k++) {
          free(exec_argv[k]);
        }

        free(exec_argv);
        lcl_ref_dec(arg);
        lcl_set_error(interp, "out of memory");

        return LCL_RC_ERR;
      }

      exec_argv[i] = strdup(s);
      lcl_ref_dec(arg);
    }

    exec_argv[argv_len] = NULL;
  }

  if (pipe(stdout_pipe) < 0) {
    goto cleanup;
  }

  if (!merge && pipe(stderr_pipe) < 0) {
    goto cleanup;
  }

  if (stdin_str && pipe(stdin_pipe) < 0) {
    goto cleanup;
  }

  pid = fork();

  if (pid < 0) {
    goto cleanup;
  }

  if (pid == 0) {
    if (cwd && chdir(cwd) < 0) {
      _exit(127);
    }

    if (stdin_str) {
      close(stdin_pipe[1]);
      dup2(stdin_pipe[0], STDIN_FILENO);
      close(stdin_pipe[0]);
    } else {
      close(STDIN_FILENO);
    }

    close(stdout_pipe[0]);
    dup2(stdout_pipe[1], STDOUT_FILENO);

    if (merge) {
      dup2(stdout_pipe[1], STDERR_FILENO);
    }
    close(stdout_pipe[1]);

    if (!merge) {
      close(stderr_pipe[0]);
      dup2(stderr_pipe[1], STDERR_FILENO);
      close(stderr_pipe[1]);
    }

    if (env_dict) {
      lcl_value *keys = NULL;

      if (lcl_dict_keys(env_dict, &keys) == LCL_OK && keys) {
        size_t ki;
        size_t klen = lcl_list_len(keys);

        for (ki = 0; ki < klen; ki++) {
          lcl_value *key_val = NULL;
          lcl_value *val = NULL;

          if (lcl_list_get(keys, ki, &key_val) == LCL_OK) {
            const char *key = lcl_value_to_string(key_val);

            if (key && lcl_dict_get(env_dict, key, &val) == LCL_OK) {
              const char *val_s = lcl_value_to_string(val);

              if (val_s) {
                setenv(key, val_s, 1);
              }

              lcl_ref_dec(val);
            }

            lcl_ref_dec(key_val);
          }
        }

        lcl_ref_dec(keys);
      }
    }

    execvp(exec_argv[0], exec_argv);
    _exit(127);
  }

  close(stdout_pipe[1]);
  stdout_pipe[1] = -1;

  if (!merge) {
    close(stderr_pipe[1]);
    stderr_pipe[1] = -1;
  }

  if (stdin_str) {
    close(stdin_pipe[0]);
    stdin_pipe[0] = -1;
  }

  if (stdin_str) {
    size_t len = strlen(stdin_str);
    ssize_t written = write(stdin_pipe[1], stdin_str, len);
    (void)written;
    close(stdin_pipe[1]);
    stdin_pipe[1] = -1;
  }

  stdout_data = read_all(stdout_pipe[0], &stdout_len, limit);

  if (!merge) {
    stderr_data = read_all(stderr_pipe[0], &stderr_len, limit);
  }

  waitpid(pid, &status, 0);
  result = lcl_dict_new();

  if (WIFEXITED(status)) {
    tmp = lcl_int_new(WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    tmp = lcl_int_new(-WTERMSIG(status));
  } else {
    tmp = lcl_int_new(-1);
  }

  lcl_dict_put(&result, "status", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_string_new(stdout_data);
  lcl_dict_put(&result, "stdout", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_string_new(stderr_data);
  lcl_dict_put(&result, "stderr", tmp);
  lcl_ref_dec(tmp);

  if (do_throw && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
    lcl_ref_dec(result);
    rc = LCL_RC_ERR;
    goto cleanup;
  }

  *out = result;
  rc = LCL_RC_OK;

cleanup:
  if (exec_argv) {
    for (i = 0; exec_argv[i]; i++) {
      free(exec_argv[i]);
    }
    free(exec_argv);
  }

  if (stdin_pipe[0] >= 0) {
    close(stdin_pipe[0]);
  }

  if (stdin_pipe[1] >= 0) {
    close(stdin_pipe[1]);
  }

  if (stdout_pipe[0] >= 0) {
    close(stdout_pipe[0]);
  }

  if (stdout_pipe[1] >= 0) {
    close(stdout_pipe[1]);
  }

  if (stderr_pipe[0] >= 0) {
    close(stderr_pipe[0]);
  }

  if (stderr_pipe[1] >= 0) {
    close(stderr_pipe[1]);
  }

  free(stdout_data);
  free(stderr_data);

  if (env_dict) {
    lcl_ref_dec(env_dict);
  }

  return rc;
}

/*
 * process::spawn argv ?opts?
 *
 * Options (dict):
 *   env   - dict of environment vars
 *   cwd   - working directory
 *   merge - bool, merge stderr into stdout
 *   pty   - bool, use PTY mode for terminal emulation
 *   rows  - initial PTY rows (default: 24)
 *   cols  - initial PTY cols (default: 80)
 *
 * Returns: handle (opaque)
 */
lcl_return_code c_process_spawn(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lcl_value *argv_list;
  lcl_value *opts = NULL;
  size_t argv_len;
  char **exec_argv = NULL;
  size_t i;
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  int pty_master = -1;
  int pty_slave = -1;
  pid_t pid;
  process_handle *h = NULL;
  const char *cwd;
  lcl_value *env_dict;
  int merge;
  int use_pty;
  int pty_rows;
  int pty_cols;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  argv_list = argv[0];

  if (argc >= 2) {
    opts = argv[1];
  }

  cwd = get_opt_str(opts, "cwd", NULL);
  env_dict = get_opt_val(opts, "env");
  merge = get_opt_int(opts, "merge", 0);
  use_pty = get_opt_int(opts, "pty", 0);
  pty_rows = get_opt_int(opts, "rows", 24);
  pty_cols = get_opt_int(opts, "cols", 80);
  argv_len = lcl_list_len(argv_list);

  if (argv_len == 0) {
    return LCL_RC_ERR;
  }

  exec_argv = (char **)malloc((argv_len + 1) * sizeof(char *));

  if (!exec_argv) {
    return LCL_RC_ERR;
  }

  for (i = 0; i < argv_len; i++) {
    lcl_value *arg = NULL;
    const char *s;
    lcl_list_get(argv_list, i, &arg);
    s = lcl_value_to_string(arg);

    if (!s) {
      size_t k;

      for (k = 0; k < i; k++) {
        free(exec_argv[k]);
      }

      free(exec_argv);
      lcl_ref_dec(arg);
      lcl_set_error(interp, "out of memory");

      return LCL_RC_ERR;
    }

    exec_argv[i] = strdup(s);
    lcl_ref_dec(arg);
  }

  exec_argv[argv_len] = NULL;

  if (use_pty) {
    struct winsize ws;
    ws.ws_row = (unsigned short)pty_rows;
    ws.ws_col = (unsigned short)pty_cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (openpty(&pty_master, &pty_slave, NULL, NULL, &ws) < 0) {
      goto error;
    }
  } else {
    if (pipe(stdin_pipe) < 0) {
      goto error;
    }

    if (pipe(stdout_pipe) < 0) {
      goto error;
    }

    if (!merge && pipe(stderr_pipe) < 0) {
      goto error;
    }
  }

  pid = fork();

  if (pid < 0) {
    goto error;
  }

  if (pid == 0) {
    if (cwd && chdir(cwd) < 0) {
      _exit(127);
    }

    if (use_pty) {
      close(pty_master);
      setsid();
#ifdef TIOCSCTTY
      ioctl(pty_slave, TIOCSCTTY, 0);
#endif
      dup2(pty_slave, STDIN_FILENO);
      dup2(pty_slave, STDOUT_FILENO);
      dup2(pty_slave, STDERR_FILENO);

      if (pty_slave > STDERR_FILENO) {
        close(pty_slave);
      }
    } else {
      close(stdin_pipe[1]);
      dup2(stdin_pipe[0], STDIN_FILENO);
      close(stdin_pipe[0]);
      close(stdout_pipe[0]);
      dup2(stdout_pipe[1], STDOUT_FILENO);

      if (merge) {
        dup2(stdout_pipe[1], STDERR_FILENO);
      }
      close(stdout_pipe[1]);

      if (!merge) {
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);
      }
    }

    if (env_dict) {
      lcl_value *keys = NULL;

      if (lcl_dict_keys(env_dict, &keys) == LCL_OK && keys) {
        size_t ki;
        size_t klen = lcl_list_len(keys);

        for (ki = 0; ki < klen; ki++) {
          lcl_value *key_val = NULL;
          lcl_value *val = NULL;

          if (lcl_list_get(keys, ki, &key_val) == LCL_OK) {
            const char *key = lcl_value_to_string(key_val);

            if (key && lcl_dict_get(env_dict, key, &val) == LCL_OK) {
              const char *val_s = lcl_value_to_string(val);

              if (val_s) {
                setenv(key, val_s, 1);
              }

              lcl_ref_dec(val);
            }

            lcl_ref_dec(key_val);
          }
        }

        lcl_ref_dec(keys);
      }
    }

    execvp(exec_argv[0], exec_argv);
    _exit(127);
  }

  h = process_handle_new();

  if (!h) {
    goto error;
  }

  if (use_pty) {
    close(pty_slave);
    set_nonblocking(pty_master);

    h->pid = pid;
    h->pty_master = pty_master;
    h->is_pty = 1;
    h->stdin_fd = pty_master;
    h->stdout_fd = pty_master;
    h->stderr_fd = -1;
  } else {
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    if (!merge) {
      close(stderr_pipe[1]);
    }

    set_nonblocking(stdout_pipe[0]);

    if (!merge) {
      set_nonblocking(stderr_pipe[0]);
    }

    h->pid = pid;
    h->stdin_fd = stdin_pipe[1];
    h->stdout_fd = stdout_pipe[0];
    h->stderr_fd = merge ? -1 : stderr_pipe[0];
  }

  for (i = 0; exec_argv[i]; i++) {
    free(exec_argv[i]);
  }

  free(exec_argv);

  if (env_dict) {
    lcl_ref_dec(env_dict);
  }

  *out = lcl_opaque_new(h, PROCESS_HANDLE_TYPE_TAG, process_handle_finalizer);

  return LCL_RC_OK;

error:
  if (exec_argv) {
    for (i = 0; exec_argv[i]; i++) {
      free(exec_argv[i]);
    }
    free(exec_argv);
  }

  if (pty_master >= 0) {
    close(pty_master);
  }

  if (pty_slave >= 0) {
    close(pty_slave);
  }

  if (stdin_pipe[0] >= 0) {
    close(stdin_pipe[0]);
  }

  if (stdin_pipe[1] >= 0) {
    close(stdin_pipe[1]);
  }

  if (stdout_pipe[0] >= 0) {
    close(stdout_pipe[0]);
  }

  if (stdout_pipe[1] >= 0) {
    close(stdout_pipe[1]);
  }

  if (stderr_pipe[0] >= 0) {
    close(stderr_pipe[0]);
  }

  if (stderr_pipe[1] >= 0) {
    close(stderr_pipe[1]);
  }

  free(h);

  if (env_dict) {
    lcl_ref_dec(env_dict);
  }

  return LCL_RC_ERR;
}

static lcl_return_code c_process_send(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  process_handle *h;
  const char *data;
  size_t len;
  ssize_t written;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (h->stdin_fd < 0) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  len = strlen(data);

  written = write(h->stdin_fd, data, len);

  if (written < 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)written);

  return LCL_RC_OK;
}

static lcl_return_code c_process_close_stdin(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  process_handle *h;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (h->stdin_fd >= 0) {
    close(h->stdin_fd);
    h->stdin_fd = -1;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/*
 * process::read handle ?opts?
 *
 * Options:
 *   n       - max bytes to read (default: all available)
 *   stderr  - read from stderr instead of stdout
 *   timeout - ms to wait (0 = non-blocking)
 */
static lcl_return_code c_process_read(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  process_handle *h;
  lcl_value *opts = NULL;
  int fd;
  int use_stderr;
  int timeout_ms;
  int max_bytes;
  char *buf;
  ssize_t n;
  fd_set rfds;
  struct timeval tv;
  int ret;

  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    opts = argv[1];
  }

  use_stderr = get_opt_int(opts, "stderr", 0);
  timeout_ms = get_opt_int(opts, "timeout", 0);
  max_bytes = get_opt_int(opts, "n", 4096);

  fd = use_stderr ? h->stderr_fd : h->stdout_fd;

  if (fd < 0) {
    *out = lcl_string_new("");
    return LCL_RC_OK;
  }

  if (timeout_ms > 0) {
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (suseconds_t)(long)(timeout_ms % 1000) * 1000;

    ret = select(fd + 1, &rfds, NULL, NULL, &tv);

    if (ret <= 0) {
      *out = lcl_string_new("");
      return LCL_RC_OK;
    }
  }

  buf = (char *)malloc((size_t)max_bytes + 1);

  if (!buf) {
    return LCL_RC_ERR;
  }

  n = read(fd, buf, (size_t)max_bytes);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      n = 0;
    } else {
      free(buf);
      return LCL_RC_ERR;
    }
  }

  buf[n] = '\0';
  *out = lcl_string_new(buf);
  free(buf);

  return LCL_RC_OK;
}

/*
 * process::read-until handle patterns ?opts?
 *
 * Read from process until one of the patterns is found.
 *
 * patterns: string (single) or list (multiple patterns to match)
 *
 * Options:
 *   timeout - ms to wait total (0 = block forever)
 *   stderr  - read from stderr instead of stdout
 *
 * Returns: #{data "..." matched 0/1 pattern "..." index N}
 *   data    - all data read (including matched pattern if any)
 *   matched - 1 if pattern found, 0 on timeout/EOF
 *   pattern - the pattern that matched (empty if not matched)
 *   index   - index of matched pattern in list (0 if single pattern)
 */
static lcl_return_code c_process_read_until(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  process_handle *h;
  lcl_value *patterns;
  lcl_value *opts = NULL;
  int fd;
  int use_stderr;
  int timeout_ms;
  int elapsed_ms;
  size_t num_patterns;
  const char **pattern_strs = NULL;
  size_t *pattern_lens = NULL;
  char *buf = NULL;
  size_t buf_size = 4096;
  size_t buf_len = 0;
  int matched = 0;
  size_t matched_idx = 0;
  const char *matched_pattern = "";
  lcl_value *result;
  lcl_value *tmp;
  size_t i;
  (void)interp;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  patterns = argv[1];

  if (argc >= 3) {
    opts = argv[2];
  }

  use_stderr = get_opt_int(opts, "stderr", 0);
  timeout_ms = get_opt_int(opts, "timeout", 0);

  fd = use_stderr ? h->stderr_fd : h->stdout_fd;

  if (fd < 0) {
    result = lcl_dict_new();
    tmp = lcl_string_new("");
    lcl_dict_put(&result, "data", tmp);
    lcl_ref_dec(tmp);
    tmp = lcl_int_new(0);
    lcl_dict_put(&result, "matched", tmp);
    lcl_ref_dec(tmp);
    tmp = lcl_string_new("");
    lcl_dict_put(&result, "pattern", tmp);
    lcl_ref_dec(tmp);
    tmp = lcl_int_new(0);
    lcl_dict_put(&result, "index", tmp);
    lcl_ref_dec(tmp);
    *out = result;
    return LCL_RC_OK;
  }

  if (lcl_value_type_of(patterns) == LCL_LIST) {
    num_patterns = lcl_list_len(patterns);
  } else {
    num_patterns = 1;
  }

  if (num_patterns == 0) {
    return LCL_RC_ERR;
  }

  pattern_strs = (const char **)malloc(num_patterns * sizeof(char *));
  pattern_lens = (size_t *)malloc(num_patterns * sizeof(size_t));

  if (!pattern_strs || !pattern_lens) {
    free(pattern_strs);
    free(pattern_lens);
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(patterns) == LCL_LIST) {
    for (i = 0; i < num_patterns; i++) {
      lcl_value *p = NULL;

      if (lcl_list_get(patterns, i, &p) == LCL_OK) {
        const char *ps = lcl_value_to_string(p);

        if (!ps) {
          ps = "";
        }

        pattern_strs[i] = ps;
        pattern_lens[i] = strlen(ps);
        lcl_ref_dec(p);
      } else {
        pattern_strs[i] = "";
        pattern_lens[i] = 0;
      }
    }
  } else {
    const char *ps = lcl_value_to_string(patterns);

    if (!ps) {
      ps = "";
    }

    pattern_strs[0] = ps;
    pattern_lens[0] = strlen(ps);
  }

  buf = (char *)malloc(buf_size);

  if (!buf) {
    free(pattern_strs);
    free(pattern_lens);
    return LCL_RC_ERR;
  }

  buf[0] = '\0';
  elapsed_ms = 0;

  while (!matched) {
    fd_set rfds;
    struct timeval tv;
    int ret;
    ssize_t n;
    int wait_ms;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    if (timeout_ms > 0) {
      wait_ms = timeout_ms - elapsed_ms;

      if (wait_ms <= 0) {
        break;
      }

      if (wait_ms > 100) {
        wait_ms = 100;
      }
    } else {
      wait_ms = 100;
    }

    tv.tv_sec = wait_ms / 1000;
    tv.tv_usec = (suseconds_t)(long)(wait_ms % 1000) * 1000;

    ret = select(fd + 1, &rfds, NULL, NULL, &tv);
    elapsed_ms += wait_ms;

    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    if (ret == 0) {
      if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
        break;
      }
      continue;
    }

    if (buf_len + 1024 >= buf_size) {
      size_t new_size = buf_size * 2;
      char *new_buf = (char *)realloc(buf, new_size);

      if (!new_buf) {
        break;
      }
      buf = new_buf;
      buf_size = new_size;
    }

    n = read(fd, buf + buf_len, 1024);

    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      break;
    }

    buf_len += (size_t)n;
    buf[buf_len] = '\0';

    for (i = 0; i < num_patterns; i++) {
      if (pattern_lens[i] > 0 && pattern_lens[i] <= buf_len) {
        char *found = strstr(buf, pattern_strs[i]);

        if (found) {
          matched = 1;
          matched_idx = i;
          matched_pattern = pattern_strs[i];
          buf_len = (size_t)(found - buf) + pattern_lens[i];
          buf[buf_len] = '\0';
          break;
        }
      }
    }
  }

  result = lcl_dict_new();

  tmp = lcl_string_new(buf);
  lcl_dict_put(&result, "data", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_int_new(matched);
  lcl_dict_put(&result, "matched", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_string_new(matched_pattern);
  lcl_dict_put(&result, "pattern", tmp);
  lcl_ref_dec(tmp);

  tmp = lcl_int_new((long)matched_idx);
  lcl_dict_put(&result, "index", tmp);
  lcl_ref_dec(tmp);

  *out = result;

  free(buf);
  free(pattern_strs);
  free(pattern_lens);

  return LCL_RC_OK;
}

/*
 * process::wait handle ?opts?
 *
 * Options:
 *   timeout - ms to wait (0 = block forever)
 *
 * Returns: #{exited 1 status N} or #{exited 0} on timeout
 */
static lcl_return_code c_process_wait(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  process_handle *h;
  lcl_value *opts = NULL;
  int timeout_ms;
  int status;
  pid_t ret;
  lcl_value *result;
  lcl_value *tmp;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    opts = argv[1];
  }

  timeout_ms = get_opt_int(opts, "timeout", 0);

  if (h->exited) {
    result = lcl_dict_new();
    tmp = lcl_int_new(1);
    lcl_dict_put(&result, "exited", tmp);
    lcl_ref_dec(tmp);
    tmp = lcl_int_new(h->status);
    lcl_dict_put(&result, "status", tmp);
    lcl_ref_dec(tmp);

    if (h->signal_num) {
      tmp = lcl_int_new(h->signal_num);
      lcl_dict_put(&result, "signal", tmp);
      lcl_ref_dec(tmp);
    }

    *out = result;
    return LCL_RC_OK;
  }

  if (timeout_ms > 0) {
    int elapsed = 0;
    int interval = 10;

    while (elapsed < timeout_ms) {
      ret = waitpid(h->pid, &status, WNOHANG);

      if (ret > 0) {
        break;
      }

      if (ret < 0) {
        return LCL_RC_ERR;
      }

      {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = (suseconds_t)(long)interval * 1000;
        select(0, NULL, NULL, NULL, &tv);
      }

      elapsed += interval;
    }

    if (ret == 0) {
      result = lcl_dict_new();
      tmp = lcl_int_new(0);
      lcl_dict_put(&result, "exited", tmp);
      lcl_ref_dec(tmp);
      *out = result;
      return LCL_RC_OK;
    }
  } else {
    ret = waitpid(h->pid, &status, 0);

    if (ret < 0) {
      return LCL_RC_ERR;
    }
  }

  h->exited = 1;

  if (WIFEXITED(status)) {
    h->status = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    h->status = -1;
    h->signal_num = WTERMSIG(status);
  }

  result = lcl_dict_new();
  tmp = lcl_int_new(1);
  lcl_dict_put(&result, "exited", tmp);
  lcl_ref_dec(tmp);
  tmp = lcl_int_new(h->status);
  lcl_dict_put(&result, "status", tmp);
  lcl_ref_dec(tmp);

  if (h->signal_num) {
    tmp = lcl_int_new(h->signal_num);
    lcl_dict_put(&result, "signal", tmp);
    lcl_ref_dec(tmp);
  }

  *out = result;

  return LCL_RC_OK;
}

static lcl_return_code c_process_alive(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  process_handle *h;
  int status;
  pid_t ret;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (h->exited) {
    *out = lcl_int_new(0);
    return LCL_RC_OK;
  }

  ret = waitpid(h->pid, &status, WNOHANG);

  if (ret == 0) {
    *out = lcl_int_new(1);
  } else if (ret > 0) {
    h->exited = 1;

    if (WIFEXITED(status)) {
      h->status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      h->status = -1;
      h->signal_num = WTERMSIG(status);
    }
    *out = lcl_int_new(0);
  } else {
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/*
 * process::kill handle ?opts?
 *
 * Options:
 *   signal - signal name or number (default: TERM)
 */
static lcl_return_code c_process_kill(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  process_handle *h;
  lcl_value *opts = NULL;
  const char *sig_str;
  int sig = SIGTERM;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (argc >= 2) {
    opts = argv[1];
  }

  sig_str = get_opt_str(opts, "signal", NULL);

  if (sig_str) {
    if (strcmp(sig_str, "TERM") == 0 || strcmp(sig_str, "SIGTERM") == 0) {
      sig = SIGTERM;
    } else if (strcmp(sig_str, "KILL") == 0 ||
               strcmp(sig_str, "SIGKILL") == 0) {
      sig = SIGKILL;
    } else if (strcmp(sig_str, "INT") == 0 || strcmp(sig_str, "SIGINT") == 0) {
      sig = SIGINT;
    } else if (strcmp(sig_str, "HUP") == 0 || strcmp(sig_str, "SIGHUP") == 0) {
      sig = SIGHUP;
    } else {
      long n;
      lcl_value *v = get_opt_val(opts, "signal");

      if (v && lcl_value_to_int(v, &n) == LCL_OK) {
        sig = (int)n;
      }

      if (v) {
        lcl_ref_dec(v);
      }
    }
  }

  if (h->exited) {
    *out = lcl_int_new(0);
    return LCL_RC_OK;
  }

  if (kill(h->pid, sig) < 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(1);

  return LCL_RC_OK;
}

static lcl_return_code c_process_close(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  process_handle *h;

  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (h->is_pty) {
    if (h->pty_master >= 0) {
      close(h->pty_master);
      h->pty_master = -1;
      h->stdin_fd = -1;
      h->stdout_fd = -1;
    }
  } else {
    if (h->stdin_fd >= 0) {
      close(h->stdin_fd);
      h->stdin_fd = -1;
    }

    if (h->stdout_fd >= 0) {
      close(h->stdout_fd);
      h->stdout_fd = -1;
    }

    if (h->stderr_fd >= 0) {
      close(h->stderr_fd);
      h->stderr_fd = -1;
    }
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/*
 * process::pty? handle - check if handle is using PTY mode
 */
static lcl_return_code c_process_is_pty(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  process_handle *h;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);
  if (!h) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(h->is_pty);
  return LCL_RC_OK;
}

/*
 * process::set-winsize handle rows cols
 *
 * Set the terminal window size for PTY handles.
 * Only works on PTY handles; returns error for pipe handles.
 */
static lcl_return_code c_process_set_winsize(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  process_handle *h;
  long rows;
  long cols;
  struct winsize ws;
  (void)interp;

  if (argc < 3) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (!h->is_pty || h->pty_master < 0) {
    lcl_set_error(interp, "set-winsize only works on PTY handles");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &rows) != LCL_OK ||
      lcl_value_to_int(argv[2], &cols) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ws.ws_row = (unsigned short)rows;
  ws.ws_col = (unsigned short)cols;
  ws.ws_xpixel = 0;
  ws.ws_ypixel = 0;

  if (ioctl(h->pty_master, TIOCSWINSZ, &ws) < 0) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/*
 * process::get-winsize handle
 *
 * Get the terminal window size for PTY handles.
 * Returns: #{rows N cols M}
 */
static lcl_return_code c_process_get_winsize(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  process_handle *h;
  struct winsize ws;
  lcl_value *result;
  lcl_value *tmp;
  (void)interp;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  h = get_handle(argv[0]);

  if (!h) {
    return LCL_RC_ERR;
  }

  if (!h->is_pty || h->pty_master < 0) {
    lcl_set_error(interp, "get-winsize only works on PTY handles");
    return LCL_RC_ERR;
  }

  if (ioctl(h->pty_master, TIOCGWINSZ, &ws) < 0) {
    return LCL_RC_ERR;
  }

  result = lcl_dict_new();
  tmp = lcl_int_new(ws.ws_row);
  lcl_dict_put(&result, "rows", tmp);
  lcl_ref_dec(tmp);
  tmp = lcl_int_new(ws.ws_col);
  lcl_dict_put(&result, "cols", tmp);
  lcl_ref_dec(tmp);

  *out = result;
  return LCL_RC_OK;
}

/*
 * lcl-process - Process spawning and management for LCL
 *
 * Provides the process:: namespace with:
 *   process::run        - synchronous execution with capture
 *   process::spawn      - asynchronous execution, returns handle (with PTY
 * support) process::send       - write to stdin process::read       - read from
 * stdout/stderr process::read-until - read until pattern matched (expect-like)
 *   process::wait       - wait for process exit
 *   process::close      - close handle and cleanup
 *   process::alive?     - check if process is still running
 *   process::kill       - send signal to process
 *   process::pty?       - check if handle is using PTY mode
 *   process::set-winsize - set terminal window size (PTY only)
 *   process::get-winsize - get terminal window size (PTY only)
 */
void lcl_register_process(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new(PROCESS_NS);
  lcl_define_take(interp, PROCESS_NS, ns);

  lcl_ns_def_take(ns, "run", lcl_c_proc_new("process::run", c_process_run));
  lcl_ns_def_take(ns, "spawn",
                  lcl_c_proc_new("process::spawn", c_process_spawn));
  lcl_ns_def_take(ns, "send", lcl_c_proc_new("process::send", c_process_send));
  lcl_ns_def_take(
      ns, "close-stdin",
      lcl_c_proc_new("process::close-stdin", c_process_close_stdin));
  lcl_ns_def_take(ns, "read", lcl_c_proc_new("process::read", c_process_read));
  lcl_ns_def_take(ns, "read-until",
                  lcl_c_proc_new("process::read-until", c_process_read_until));
  lcl_ns_def_take(ns, "wait", lcl_c_proc_new("process::wait", c_process_wait));
  lcl_ns_def_take(ns, "alive?",
                  lcl_c_proc_new("process::alive?", c_process_alive));
  lcl_ns_def_take(ns, "kill", lcl_c_proc_new("process::kill", c_process_kill));
  lcl_ns_def_take(ns, "close",
                  lcl_c_proc_new("process::close", c_process_close));
  lcl_ns_def_take(ns, "pty?",
                  lcl_c_proc_new("process::pty?", c_process_is_pty));
  lcl_ns_def_take(
      ns, "set-winsize",
      lcl_c_proc_new("process::set-winsize", c_process_set_winsize));
  lcl_ns_def_take(
      ns, "get-winsize",
      lcl_c_proc_new("process::get-winsize", c_process_get_winsize));
}
