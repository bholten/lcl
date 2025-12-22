/*
 * LCL Embedding Example
 *
 * This example demonstrates how to embed LCL into a C application.
 *
 * Build:
 *   gcc -I../include -o embed_example embed_example.c ../src/*.c
 *
 * Or link against liblcl if built as a library.
 */

#include <stdio.h>
#include "lcl.h"

/* Example C function to register with LCL */
int c_multiply(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
    long a, b;
    (void)interp;

    if (argc != 2) {
        return LCL_RC_ERR;
    }

    if (lcl_value_to_int(argv[0], &a) != LCL_OK ||
        lcl_value_to_int(argv[1], &b) != LCL_OK) {
        return LCL_RC_ERR;
    }

    *out = lcl_int_new(a * b);
    return *out ? LCL_RC_OK : LCL_RC_ERR;
}

int main(void) {
    lcl_interp *interp;
    lcl_value *result = NULL;
    int rc;

    /* Create interpreter */
    interp = lcl_interp_new();
    if (!interp) {
        fprintf(stderr, "Failed to create interpreter\n");
        return 1;
    }

    /* Register core stdlib */
    lcl_register_core(interp);

    /* Register our custom multiply function */
    lcl_register_proc(interp, "*", c_multiply);

    /* Define a variable from C */
    lcl_define_take(interp, "greeting", lcl_string_new("Hello from C!"));

    /* Run some LCL code */
    rc = lcl_eval_string(interp,
        "puts $greeting\n"
        "puts [* 6 7]\n"
        "proc square {n} { return [* $n $n] }\n"
        "square 5",
        &result);

    if (rc == LCL_RC_OK) {
        printf("Result: %s\n", lcl_value_to_string(result));
    } else {
        fprintf(stderr, "Error at %s:%d\n",
                lcl_interp_error_file(interp) ? lcl_interp_error_file(interp) : "<unknown>",
                lcl_interp_error_line(interp));
    }

    /* Cleanup */
    if (result) lcl_ref_dec(result);
    lcl_interp_free(interp);

    return rc == LCL_RC_OK ? 0 : 1;
}
