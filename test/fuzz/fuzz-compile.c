#include <stddef.h>
#include <stdint.h>

#include <lcl-lex.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  lcl_compile_err err;
  lcl_program *p;

  err.msg = NULL;
  err.line = 0;

  p = lcl_program_compile_bytes_ex((const char *)data, size, "<fuzz>", &err);

  if (p != NULL) {
    lcl_program_free(p);
  }

  return 0;
}
