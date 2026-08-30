# Embedding Lcl

Lcl is designed to be embedded in C applications. The public API is
the single installed header `include/lcl.h`; everything under `src/`
is internal and may change without notice.

```c
#include <lcl.h>

int main(void) {
    lcl_interp *interp = lcl_interp_new();
    lcl_register_core(interp);

    lcl_value *result = NULL;
    lcl_eval_string(interp, "puts {Hello from Lcl!}", &result);

    if (result) lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
```

For a fuller tour -- registering a C function as a Lcl command,
defining a Lcl variable from C, extracting the result, and surfacing
error location -- see
[`examples/embed_example.c`](https://github.com/bholten/lcl/blob/master/examples/embed_example.c)
in the repository.

## Ownership

Ownership is refcounted and spelled out per function in `lcl.h`: every
`**out` accessor (`lcl_dict_get`, `lcl_list_get`, ...) hands you a +1
reference you must `lcl_ref_dec`. When you only need to read a
container element during a call, the `_peek` twins (`lcl_dict_peek`,
`lcl_list_peek`, `lcl_cell_peek`, `lcl_ns_peek`) return the
container's own reference with nothing to release.

Build the example from the project's CMake with:

```bash
cmake -S . -B build -DLCL_BUILD_EXAMPLES=ON
cmake --build build
./build/examples/embed_example
```

## Host hooks

Two host hooks cover budgets and observability. `lcl_set_step_hook`
runs a callback every N commands and can abort a runaway script
(sticky: `catch` cannot swallow it). `lcl_set_call_hook` fires on
every user-proc entry and exit with the invoked name and evaluated
arguments -- `Time::profile` and the CLI's `LCL_TRACE` are both
clients of it, and `lcl_get_call_hook` lets a temporary installer
restore the previous one. `lcl_get_stats` returns the process-wide
value/clone counters behind `Interp::stats`. `lcl_interp_abort`
triggers the same sticky abort from inside a C procedure -- it is how
the CLI implements `exit`.

Two more hooks shape the module loader. `lcl_set_module_source_fn` is
where `require` and `load` get their text: with it installed the core
reads no files -- every candidate path is offered to the hook, which
returns a malloc'd source or NULL, so a host with no filesystem (a
browser page, an application with modules in a bundle) serves modules
itself; a hook that sets an error with `lcl_set_error` has that
message reported instead of the list of paths tried.
`lcl_set_module_key_fn` derives the require cache/cycle key from the
resolved lexical path (physical-file deduplication on POSIX, for
instance) and affects identity only. `lcl_add_require_root` registers
the directories bare `require` names are looked up under.

## Linking

To consume Lcl from your own CMake project after `cmake --install`:

```cmake
find_package(lcl REQUIRED)

add_executable(myapp myapp.c)
target_link_libraries(myapp PRIVATE lcl::lcl)
```

Or link directly: `gcc myapp.c -o myapp -llcl`.
