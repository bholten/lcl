# Contributing to Lcl

Lcl is maintained as a personal project — closer in spirit to
SQLite's development model than a typical collaborative GitHub
project. Bug reports, small fixes, test coverage, and documentation
improvements are very welcome. Larger features and changes to the
public C API or language semantics are at the maintainer's
discretion and may be declined even when technically sound; please
open an issue to discuss before investing time in anything beyond
a small patch.

Lcl is pre-1.0, so the surface is still moving. The notes below
cover what you need to know to get a change landed.

## Build and test

Lcl uses CMake. The standard development build enables tests + ASan +
UBSan, which is what CI runs:

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLCL_ENABLE_ASAN=ON \
    -DLCL_BUILD_TESTS=ON \
    -DLCL_BUILD_TEST_LIB=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For a full build with every optional package enabled (matches the CI
`linux-full` matrix row):

```bash
mapfile -t FLAGS < <(./ci/package-set.sh flags linux-full)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLCL_ENABLE_ASAN=ON "${FLAGS[@]}"
cmake --build build
ctest --test-dir build --output-on-failure
```

`ci/package-set.sh` is also the right place to learn which apt packages
each set needs (`./ci/package-set.sh apt linux-full`).

Before submitting a PR, please run **at least** `ctest` on the
`linux-full` set. The CI workflow (`.github/workflows/ci.yml`) will
run gcc + clang × Debug + Release × core + linux-full — eight matrix
rows in total — so it's fine to leave the broader compiler/build-type
sweep to CI, but you should have one local green run.

## Code style

- The whole codebase targets **C89** (`-std=c90` with
  `-Wdeclaration-after-statement`).  No VLAs, no `//` comments, no
  mixed declarations after statements.
- A `.clang-format` and `.clang-tidy` are checked in; please run
  `clang-format -i` on touched files before submitting.
- Reference counting is the memory discipline. Returned values from
  `lcl_*` functions via `**out` parameters carry +1; functions named
  `*_take` consume their input's refcount. `lcl_ref_inc(NULL)` and
  `lcl_ref_dec(NULL)` are both safe.
- New public C API goes in `include/lcl.h` with a doc comment. The
  internal headers (`src/lcl-*.h`, `src/lcl-values.h`) are not part of
  the embedder contract.

## Tests

Tests live in three places:

- `test/lcl-test.c` — C-level regression harness with OOM injection
  via `-Wl,--wrap` (calloc, strndup, strdup, etc.). Add a new entry
  here when you fix a memory-safety bug or a crash that would
  otherwise be hard to reproduce.
- `test/conformance_smoke.lcl` and `test/anaphoric_macros.lcl` —
  language-level conformance, run by the CLI. Add a `Test::case`
  to one of these for new language or stdlib behavior.
- `packages/<pkg>/test/*.lcl` — per-package script tests. Wired
  into CTest via `lcl_add_package_test()` (see `cmake/PackageTest.cmake`).

A new fix should always come with the test that fails before it and
passes after.

## Pull requests

For small fixes — bugs, typos, missing test coverage, doc tweaks:

1. Fork and branch from `master`.
2. Make focused commits — one logical change per PR is easiest to
   review and revert.
3. Run `clang-format -i` on touched C files and `ctest` on at least
   `linux-full`.
4. Open the PR against `master`. CI will run the matrix; please make
   sure it's green before asking for review.

For anything larger — new features, additions to the public C API
in `include/lcl.h`, changes to language semantics, new packages —
please open an issue first so we can talk through whether it's a
fit before you invest time in the implementation. See
[`VERSIONING.md`](VERSIONING.md) for what counts as a breaking
change to which surface.

## Working together

Be civil and assume good faith. The reviewer's job is to help you
land the change, not to gatekeep. The contributor's job is to make
the change small enough to review.
