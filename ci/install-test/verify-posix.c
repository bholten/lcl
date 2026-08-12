/* Exercises a simple-package install: lcl::lcl_posix + lcl_register_posix(). */
#include <lcl.h>
#include <lcl-posix.h>
#include <stdio.h>

int main(void) {
    lcl_interp *interp = lcl_interp_new();
    lcl_value *result = NULL;
    const char *as_str = NULL;

    if (!interp) return 1;
    lcl_register_core(interp);
    lcl_register_posix(interp);

    if (lcl_eval_string(interp, "posix::getcwd", &result) != LCL_OK ||
        lcl_value_to_cstring(interp, result, &as_str) != LCL_OK) {
        fprintf(stderr, "posix::getcwd failed\n");
        lcl_ref_dec(result);
        lcl_interp_free(interp);
        return 1;
    }
    printf("ok: posix::getcwd = %s\n", as_str);
    lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
