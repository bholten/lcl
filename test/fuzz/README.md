# Fuzzing lcl

Two libFuzzer targets, one grammar:

| target | source | surface |
|--------|--------|---------|
| `fuzz-compile` | `fuzz-compile.c` | `lcl_program_compile_bytes_ex`: scanner + compiler, length-delimited (embedded NUL reaches it). There is no separate lexer target on purpose — the scanner recurses into the compiler for braced words and `[...]`, so lexer and parser are one surface. |
| `fuzz-eval` | `fuzz-eval.c` | Compile + evaluate on a fresh interpreter per input. Core stdlib only, hermetic: `puts` muted, `load`/`require` disabled, and a 100 000-command step budget so `while {1} {}` is a clean error, not a hang. |

Both are built with `-fsanitize=fuzzer,address,undefined` and
`-fno-sanitize-recover=all`, so the first ASan/UBSan report aborts the
input.

## The commands

```sh
test/fuzz/run.sh build          # CC=clang → build-fuzz/  (Dag: `fuzzers`)
test/fuzz/run.sh compile 600    # 10 min on the compiler
test/fuzz/run.sh eval 3600      # 1 h on compile+eval — the overnight one is 8h+
test/fuzz/run.sh merge          # minimize both corpora against the seeds
test/fuzz/run.sh replay test/fuzz/artifacts/eval/timeout-<sha>   # alone, fresh process
```

`run.sh` pins the flags so a run is reproducible: `-dict=lcl.dict
-max_len=4096 -timeout=30 -rss_limit_mb=2048`, `-jobs`/`-workers` =
`nproc` (override with `FUZZ_JOBS=n`), artifacts under
`test/fuzz/artifacts/<target>/` (git-ignored), per-worker logs as
`fuzz-N.log` in the current directory (git-ignored). Run from
anywhere; the script `cd`s to the repo root. A run needs `build-fuzz/`
from `run.sh build`; rebuild after any core change, the coverage map
is compiled in.

## Corpus layout

- `seeds/` — curated, committed. `seed.sh` regenerates the
  `seed-<path>` entries from every `.lcl` file in the repo up to 8 KB
  (conformance parts, package and lib tests, bench workloads, docs);
  re-run it after adding tests or syntax. Fixed-bug reproducers are
  added by hand as `regression-<issue>-<what>` and never deleted: they
  are the regression suite the fuzzer keeps re-checking.
- `corpus/compile/`, `corpus/eval/` — what the fuzzers found,
  SHA-1-named. Committed after `run.sh merge`, which keeps only
  entries that add coverage beyond the seeds (a merge is also how a
  corpus from another machine gets folded in: copy its files into
  `corpus/<t>/` and merge).
- `lcl.dict` — syntax tokens plus every registered command name. Add
  new builtins here (`repeat`, `put!`, ... are there); a missing
  keyword is the difference between the fuzzer finding a form in
  minutes and never.

The fuzzers read `seeds/` as a second, read-only corpus directory and
write discoveries to `corpus/<target>/`.

## Triage

1. **Replay alone first.** `run.sh replay <artifact>` re-runs one
   input in a fresh process with the same limits. A `timeout-*` or
   `slow-unit-*` that finishes in a few seconds alone was worker load,
   not a bug — every overnight run so far produced dozens of these
   (catch-recompile loops and TCO self-tail-calls that redefine a
   large proc each iteration are the two known benign families).  Only
   an input that still trips the limit alone is a finding.
2. `crash-*` and `oom-*` are always findings. Read the sanitizer
   report in the replay output; the interesting frame is the first one
   inside `src/`.
3. Minimize before filing: `build-fuzz/fuzz-<t> -minimize_crash=1
   -runs=100000 <artifact>` writes `minimized-from-*`.
4. Fix, then copy the minimized input to
   `seeds/regression-<issue>-<what>` and add the language-level
   regression to the conformance suite or `test/lcl-test.c` as usual.

Known open class: single-command resource bombs (`List::range 0
1000000000` and friends) show as `oom-*`; the step budget cannot see
inside one command. The self-referential DAG `var ps (); repeat n {
List::push! ps $ps }` is the same class in two guises: its canonical
string is `2^n` bytes (`oom-*` in `lcl_reify_str_list`), and the
`push!`  cycle check costs `O(edges) = O(n^2)` per push, so `n ≈ 1800`
still takes ~35 s in the instrumented build (`timeout-*` in
`lcl_value_would_cycle`; ~3 s in a release build).
