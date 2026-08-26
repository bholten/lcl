/*
 * Minimal LCL embedding example.
 *
 * Demonstrates the host-side patterns embedders care about most:
 *   1. Create an interpreter and register the core stdlib.
 *   2. Expose a C function as a Lcl command.
 *   3. Define a Lcl variable from C.
 *   4. Evaluate a Lcl script and extract the result.
 *   5. Surface errors with file and line information.
 *
 * Build via the project's CMake (the convenient path):
 *   cmake -S . -B build -DLCL_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/examples/embed_example
 *
 * Build standalone against an installed liblcl:
 *   gcc embed_example.c -o embed_example -llcl
 *
 * Or from another CMake project:
 *   find_package(lcl REQUIRED)
 *   add_executable(myapp myapp.c)
 *   target_link_libraries(myapp PRIVATE lcl::lcl)
 */

#include <stdio.h>

#include <lcl.h>

/* A custom command exposed to scripts as `mul`. CPROCs receive argv
 * as pre-evaluated values (ownership stays with the caller) and write
 * their result into *out with a +1 refcount on success. */
static lcl_return_code c_mul(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
    long a, b;

    if (argc != 2) {
        lcl_set_error(interp, "mul: expected 2 integer arguments");
        return LCL_RC_ERR;
    }

    if (lcl_value_to_int(argv[0], &a) != LCL_OK ||
        lcl_value_to_int(argv[1], &b) != LCL_OK) {
        lcl_set_error(interp, "mul: arguments must be integers");
        return LCL_RC_ERR;
    }

    *out = lcl_int_new(a * b);
    return *out ? LCL_RC_OK : LCL_RC_ERR;
}

int main(void) {
    lcl_interp *interp = lcl_interp_new();
    lcl_value *result = NULL;
    const char *result_str = NULL;
    lcl_return_code rc;

    if (!interp) {
        fprintf(stderr, "lcl: failed to allocate interpreter\n");
        return 1;
    }

    lcl_register_core(interp);
    lcl_register_proc(interp, "mul", c_mul);

    /* lcl_define_take consumes the +1 refcount from lcl_string_new,
     * so the caller does not need to ref_dec it. */
    lcl_define_take(interp, "greeting", lcl_string_new("Hello from C!"));

    rc = lcl_eval_string(interp,
        "puts $greeting\n"
        "puts [mul 6 7]\n"
        "proc square {n} { mul $n $n }\n"
        "square 9",
        &result);

    if (rc == LCL_RC_OK) {
        if (lcl_value_to_cstring(interp, result, &result_str) == LCL_OK) {
            printf("script returned: %s\n", result_str);
        }
    } else {
        const char *msg  = lcl_interp_error_msg(interp);
        const char *file = lcl_interp_error_file(interp);
        fprintf(stderr, "lcl error at %s:%d: %s\n",
                file ? file : "<input>",
                lcl_interp_error_line(interp),
                msg  ? msg  : "(no message)");
    }

    if (result) {
        lcl_ref_dec(result);
    }
    lcl_interp_free(interp);
    return rc == LCL_RC_OK ? 0 : 1;
}
