# Contributing to Lcl

Lcl is maintained as a personal project -- closer in spirit to
SQLite's development model than a typical collaborative GitHub
project. Bug reports, small fixes, test coverage, and documentation
improvements are very welcome. Larger features and changes to the
public C API or language semantics are at the maintainer's discretion
and may be declined even when technically sound; please open an issue
to discuss before investing time in anything beyond a small patch.

Lcl is pre-1.0, so the surface is still moving. The notes below cover
what you need to know to get a change landed.

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

## Code style

The whole codebase targets **C89** (`-std=c90 -Wpedantic` with
`-Wdeclaration-after-statement`).  No VLAs, no `//` comments, no mixed
declarations after statements.  Configure with `-DLCL_WERROR=ON` (what
CI does) so a warning can't scroll past unnoticed in an incremental
build.

Please mimic the hybrid Allman-style.

## Pull requests

For small fixes -- bugs, typos, missing test coverage, doc tweaks,
simply fork and make a PR.

Anything larger, please open an issue first so we can talk through
whether it's a fit before you invest time in the implementation.

## Working together

Be civil and assume good faith. The reviewer's job is to help you land
the change, not to gatekeep. The contributor's job is to make the
change small enough to review.
