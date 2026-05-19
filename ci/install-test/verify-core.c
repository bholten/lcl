/* Exercises the core install: lcl::lcl alias + find_package(lcl). */
#include <lcl.h>
#include <lcl-version.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    lcl_interp *interp;
    lcl_value *result = NULL;
    const char *as_str = NULL;
    int rc;

    printf("linked against lcl %s\n", lcl_version());

    interp = lcl_interp_new();
    if (!interp) return 1;
    lcl_register_core(interp);

    rc = lcl_eval_string(interp, "* 6 7", &result);
    if (rc != LCL_OK) {
        fprintf(stderr, "eval failed: rc=%d\n", rc);
        lcl_interp_free(interp);
        return 1;
    }
    if (lcl_value_to_cstring(interp, result, &as_str) != LCL_OK) {
        lcl_ref_dec(result);
        lcl_interp_free(interp);
        return 1;
    }

    if (strcmp(as_str, "42") != 0) {
        fprintf(stderr, "unexpected result: %s\n", as_str);
        lcl_ref_dec(result);
        lcl_interp_free(interp);
        return 1;
    }

    printf("ok: 6 * 7 = %s\n", as_str);
    lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
