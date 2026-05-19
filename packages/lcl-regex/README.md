# lcl-regex

POSIX extended regular expressions for LCL.

## Requirements

- LCL core engine
- POSIX `regex.h` (`regcomp`, `regexec`, `regfree`)
- **Portability:** POSIX (Linux, macOS, BSD). Not portable to Windows without a `regex.h` shim (e.g. via a third-party POSIX regex library).

## Build

```bash
cmake -S . -B build -DLCL_BUILD_REGEX=ON
cmake --build build
```

## Usage

```tcl
;; One-shot match
if [regex::match {^[0-9]+$} $input] {
    puts "input is digits"
}

;; Compile once, match many times
let re [regex::regcomp {^https?://}]
foreach url $urls {
    if [regex::regexec $re $url] {
        puts "web URL: $url"
    }
}
;; No explicit free - the compiled regex is finalized when $re goes out of scope.
```

## API Reference

All patterns use POSIX extended regular expressions (`REG_EXTENDED`) with no captures (`REG_NOSUB`).

| Function | Description |
|----------|-------------|
| `regex::regcomp $pattern` | Compile a pattern, returning an opaque regex value |
| `regex::regexec $regex $string` | Match a compiled regex against a string (returns 1/0) |
| `regex::match $pattern $string` | Compile and match in one step (convenience; no caching) |

Compiled regex values are reference-counted opaque values; their underlying `regex_t` is freed automatically by the finalizer when the last reference drops.

## Tests

Tests live in `packages/lcl-regex/test/`. Run via ctest:

```bash
cmake -S . -B build \
  -DLCL_BUILD_REGEX=ON \
  -DLCL_BUILD_TESTS=ON \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_TEST_LIB=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
ctest --test-dir build -R lcl-regex
```

The `LCL_BUILD_IO` and `LCL_BUILD_TEST_LIB` flags are required because the test suite uses `puts` (lcl-io) and the `Test::suite` framework (Test lib).
