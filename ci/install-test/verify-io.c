/* Exercises a simple-package install: lcl::lcl_io + lcl_register_io(). */
#include <lcl.h>
#include <lcl-io.h>
#include <stdio.h>

int main(void) {
    lcl_interp *interp = lcl_interp_new();
    lcl_value *result = NULL;
    const char *as_str = NULL;

    if (!interp) return 1;
    lcl_register_core(interp);
    lcl_register_io(interp);

    if (lcl_eval_string(interp, "Io::getenv PATH", &result) != LCL_OK ||
        lcl_value_to_cstring(interp, result, &as_str) != LCL_OK) {
        fprintf(stderr, "Io::getenv failed\n");
        lcl_ref_dec(result);
        lcl_interp_free(interp);
        return 1;
    }
    printf("ok: Io::getenv PATH = %s\n", as_str);
    lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
