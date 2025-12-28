#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <lcl.h>

#define FILE_HANDLE_TYPE_TAG "file_handle_type_tag"
#define IO_NS "io"

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
  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], FILE_HANDLE_TYPE_TAG, (void**)&handle) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (!handle) {
    return LCL_RC_ERR;
  }

  if (fclose(handle) == EOF) {
    return LCL_RC_ERR;
  }

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

void lcl_register_io(lcl_interp *interp) {
  lcl_value *io_ns = lcl_ns_new(IO_NS);
  lcl_define_take(interp, IO_NS, io_ns);

  lcl_ns_def(io_ns, "open_file", lcl_c_proc_new("io::open_file", c_io_open_file));
  lcl_ns_def(io_ns, "close_file", lcl_c_proc_new("io::close_file", c_io_close_file));
  lcl_ns_def(io_ns, "fgets", lcl_c_proc_new("io::fgets", c_io_fgets));
}
