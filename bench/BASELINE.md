# Benchmark baselines

Numbers from `bench/run.lcl` (see `lib/bench`), recorded with the
Release build (`build-release/`: `-DCMAKE_BUILD_TYPE=Release`, no
sanitizers -- the ASan debug build inflates everything roughly
tenfold). Re-record with

    ./build-release/lcl bench/run.lcl --save bench/baseline.lcl

and compare a change against the last recording with `--baseline
bench/baseline.lcl`. Quick runs (`--min-ms 20 --batches 2`) wander by
about +/-10%; treat anything inside that as noise and re-run with the
defaults (200 ms batches, median of 5) before believing a delta.

Every later perf phase should show its win here before it merges;
add a dated section rather than overwriting.

## Pre-history (from FINDINGS.md, 2026-08-28, before the `!` forms)

Ad-hoc micro-benchmarks in the raylib game, Release build:
`List::push` into a 500-element list 34 us/push (33 ms to build 1000);
`put` on a 5-key dict 1.1 us, on 5000 keys 448 us; `get` ~750 ns at
any size; `update_enemy` (5 vector ops + 3 gets + 2 puts) 49 us;
generic `Vec::norm` 12.7 us vs 3.7 us unrolled; primitives: `+`
145 ns, `get` 104 ns, proc call 170-210 ns, `apply` 214 ns, lambda
creation 438 ns, `List::range 0 2` 261 ns, `List::map` of 2 elements
423 ns, dict `get` 418 ns / `put` 581 ns.

## 2026-08-28 -- first recording (after the `!` mutation family)

Machine: 12th Gen Intel Core i7-1260P, Linux 6.12 x86_64, gcc 14.2.0.
Branch `bholten/weft-gamedev-improvements`. Each row: median and
minimum ns per iteration over 5 batches of >= 200 ms, copy-on-write
clones per iteration, live values left behind per batch.

Highlights: the copying accumulation idioms are 12x (`push`, 1000
items), 34x (`pop`), 69x (`put` on 500 keys) and 24x (`put` on 5000
keys) slower than their in-place forms, and the clones/iter column
names the cause directly. Everything else is where the primitives
put it: a proc call plus a 5-key record update is ~12 us
(`proc call x10000`), the generic 2D vector idiom is 2-3.5x the
unrolled one, and a 3-level nested `put` chain costs 3 clones.

```
Bench: harness overhead 24 ns/iter (repeat of an empty body; not subtracted)

Bench: list
  push copying x1000                        3250.5 us  min 3233.3 us  clones/iter   1000.0  live      0
  push! x1000                                262.6 us  min  256.6 us  clones/iter      0.0  live      0
  pop copying x1000                         6113.7 us  min 6072.6 us  clones/iter      0.0  live      0
  pop! x1000                                 180.0 us  min  176.3 us  clones/iter      1.0  live      0
  del copying index 0 x200                   321.6 us  min  320.6 us  clones/iter      0.0  live      0
  del! index 0 x200                           53.8 us  min   51.8 us  clones/iter      0.0  live      0
  List::map 1000                             350.4 us  min  331.3 us  clones/iter      0.0  live      0
  List::filter 1000                          290.0 us  min  285.0 us  clones/iter      0.0  live      0
  foreach sum 1000                           334.3 us  min  328.2 us  clones/iter      0.0  live      0
  spread concat 1000+1000                     18.5 us  min   18.2 us  clones/iter      0.0  live      0
  ok: push! x1000 (262.6 us) vs push copying x1000 (3250.5 us)
  ok: pop! x1000 (180.0 us) vs pop copying x1000 (6113.7 us)
  ok: del! index 0 x200 (53.8 us) vs del copying index 0 x200 (321.6 us)

Bench: dict
  build+discard 5-key dict x1000             759.0 us  min  745.2 us  clones/iter      0.0  live      0
  put copying 5 keys x1000                   594.9 us  min  593.1 us  clones/iter   1000.0  live      0
  put! 5 keys x1000                          199.8 us  min  197.6 us  clones/iter      1.0  live      0
  put copying 500 keys x100                 3925.3 us  min 3888.0 us  clones/iter    100.0  live      0
  put! 500 keys x100                          57.2 us  min   56.7 us  clones/iter      1.0  live      0
  put copying 5000 keys x20                   10.2 ms  min   10.0 ms  clones/iter     20.0  live      0
  put! 5000 keys x20                         427.5 us  min  421.5 us  clones/iter      1.0  live      0
  get in 5000 keys x1000                     147.2 us  min  145.1 us  clones/iter      0.0  live      0
  counter idiom 100 keys x10                 586.5 us  min  572.1 us  clones/iter      0.0  live      0
  Dict::map 500                              283.4 us  min  275.7 us  clones/iter      0.0  live      0
  Dict::items 5000 + foreach                3271.4 us  min 3069.7 us  clones/iter      0.0  live      0
  ok: put! 500 keys x100 (57.2 us) vs put copying 500 keys x100 (3925.3 us)
  ok: put! 5000 keys x20 (427.5 us) vs put copying 5000 keys x20 (10.2 ms)

Bench: records
  update 1000 records via List::map           15.3 ms  min   15.2 ms  clones/iter   1000.0  live      0
  update 1000 records via foreach + push!     15.6 ms  min   15.3 ms  clones/iter   1000.0  live      0
  two puts on a 5-key record x1000           609.5 us  min  596.5 us  clones/iter   1000.0  live      0
  float arith loop 10000                      14.4 ms  min   14.3 ms  clones/iter      0.0  live      0
  proc call x10000                           119.8 ms  min  117.8 ms  clones/iter  10000.0  live      0

Bench: nested
  3-level get x1000                          424.3 us  min  392.4 us  clones/iter      0.0  live      0
  3-level put chain x1000                   1403.6 us  min 1350.4 us  clones/iter   3000.0  live      0
  tick every effect (items + put!) x1000    4820.8 us  min 4726.6 us  clones/iter   4000.0  live      0

Bench: keys
  string key cx,cy x1000                    2564.2 us  min 2471.5 us  clones/iter      0.0  live      0
  list key (cx cy) x1000                    2380.4 us  min 2342.5 us  clones/iter      0.0  live      0
  spatial hash build 1000 (put! + push)     3556.9 us  min 3490.9 us  clones/iter    853.0  live      0
  spatial hash build 1000 (copying)           27.4 ms  min   27.2 ms  clones/iter   1853.0  live      0
  ok: spatial hash build 1000 (put! + push) (3556.9 us) vs spatial hash build 1000 (copying) (27.4 ms)

Bench: sort
  sort_by int field 2000 (lambda)            872.0 us  min  850.2 us  clones/iter      0.0  live      0
  sort 2000 ints                             245.0 us  min  237.0 us  clones/iter      0.0  live      0
  sort 2000 numeric strings                 2915.0 us  min 2894.3 us  clones/iter      0.0  live      0

Bench: string
  find 50 needles                             32.8 us  min   32.3 us  clones/iter      0.0  live      0
  scan all fox via range slicing            1313.5 us  min 1282.9 us  clones/iter      0.0  live      0
  split + join                               562.1 us  min  558.4 us  clones/iter      0.0  live      0
  length x1000                               378.9 us  min  362.6 us  clones/iter      0.0  live      0
  concat 1000 pieces                         264.8 us  min  256.1 us  clones/iter      0.0  live      0

Bench: vec2
  add generic x1000                         5233.8 us  min 5150.0 us  clones/iter      0.0  live      0
  add unrolled x1000                        1777.1 us  min 1719.0 us  clones/iter      0.0  live      0
  scale generic x1000                       2951.2 us  min 2948.4 us  clones/iter      0.0  live      0
  scale unrolled x1000                      1562.0 us  min 1538.9 us  clones/iter      0.0  live      0
  dot generic x1000                         7433.0 us  min 7386.0 us  clones/iter      0.0  live      0
  dot unrolled x1000                        2098.8 us  min 2029.5 us  clones/iter      0.0  live      0
  lambda creation x1000                      496.1 us  min  481.9 us  clones/iter      0.0  live      0
  apply x1000                                207.4 us  min  204.0 us  clones/iter      0.0  live      0
  List::range 0 2 x1000                      262.7 us  min  260.0 us  clones/iter      0.0  live      0
  get + put on a 2-list x1000                237.2 us  min  233.5 us  clones/iter   1000.0  live      0
```
