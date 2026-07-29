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
let re [regex::compile {^https?://}]
foreach url $urls {
    if [regex::match $re $url] {
        puts "web URL: $url"
    }
}
;; No explicit free - the compiled regex is finalized when $re goes out of scope.

;; Captures: element 0 is the whole match, 1..n the groups
let caps [regex::captures {(issue|bug)-([0-9]+)} "see issue-42"]
;; caps = ("issue-42" "issue" "42"), or () when no match

;; Rules as data: a pattern string is already data
let rules (
    #{name "Issue reference" pattern {(issue|bug)-([0-9]+)} action open_issue}
)

;; Replace with group references
regex::replace {(bug)-([0-9]+)} {\2 (\1)} "bug-42"   ;; -> "42 (bug)"
```

## API Reference

All patterns use POSIX extended regular expressions (`REG_EXTENDED`). Every command that takes a *pattern* argument accepts **either** a pattern string or a compiled handle from `regex::compile` — compile when the same pattern will run repeatedly.

| Function | Description |
|----------|-------------|
| `regex::compile $pattern` | Compile a pattern, returning an opaque regex value |
| `regex::match $pattern $string` | 1/0 — does the pattern match anywhere in the string |
| `regex::find $pattern $string` | `(start end)` byte offsets of the first match (end exclusive), or `()` |
| `regex::captures $pattern $string` | First match's texts: whole match then each group (unmatched optional groups are empty strings); `()` when no match |
| `regex::find_all $pattern $string` | Every non-overlapping whole-match text, left to right |
| `regex::replace $pattern $repl $string` | Replace every match; `\0`…`\9` in the replacement expand to group texts, `\\` is a literal backslash |
| `regex::split $pattern $string` | Substrings between matches (leading/trailing matches contribute empty fields; empty matches are skipped) |
| `regex::regcomp $pattern` | Alias of `regex::compile` (POSIX-flavored name) |
| `regex::regexec $regex $string` | Match a compiled regex against a string (returns 1/0) |

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
