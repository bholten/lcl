/* Exercises a package with a transitive find_dependency: lcl::lcl_crypto
 * pulls OpenSSL through lclConfig.cmake's find_dependency(OpenSSL). */
#include <lcl.h>
#include <lcl-crypto.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    lcl_interp *interp = lcl_interp_new();
    lcl_value *result = NULL;
    const char *as_str = NULL;
    /* sha256("hello") */
    static const char expected[] =
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

    if (!interp) return 1;
    lcl_register_core(interp);
    lcl_register_crypto(interp);

    if (lcl_eval_string(interp, "crypto::sha256 hello", &result) != LCL_OK ||
        lcl_value_to_cstring(interp, result, &as_str) != LCL_OK) {
        fprintf(stderr, "crypto::sha256 failed\n");
        lcl_ref_dec(result);
        lcl_interp_free(interp);
        return 1;
    }
    if (strcmp(as_str, expected) != 0) {
        fprintf(stderr, "unexpected hash: %s\n", as_str);
        lcl_ref_dec(result);
        lcl_interp_free(interp);
        return 1;
    }
    printf("ok: crypto::sha256 hello = %s\n", as_str);
    lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
