# lcl-random

Seeded pseudo-random streams for LCL: **xoshiro128\*\***, exposed under
the engine's own name, `xoshiro::`.

> **Not cryptographically secure.** Do not use `xoshiro::` for keys,
> tokens, nonces, salts, or any other security-sensitive value. Use
> `crypto::random_bytes` (lcl-crypto, OpenSSL `RAND_bytes`).

## Requirements

- LCL core engine
- ISO C only (`<stdlib.h>`, `<time.h>`, `<float.h>`, `<limits.h>`)
- **Portability:** everywhere the core builds. The generator runs on
  `unsigned long` with explicit modulo-2³² arithmetic, so it is the
  same generator on 32- and 64-bit `long`.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_RANDOM=ON
cmake --build build
```

## Usage

```tcl
let rng [xoshiro::new 12345]       ;; deterministic stream
let rng [xoshiro::new]             ;; weak time-based seed

xoshiro::int $rng 1 6              ;; integer in [1, 6], inclusive
xoshiro::float $rng                ;; float in [0, 1)
xoshiro::shuffle $rng (a b c d)    ;; new list, Fisher-Yates

;; Minesweeper: n distinct mines out of all cells
let mines [List::slice [xoshiro::shuffle $rng $cells] 0 $n]
```

## API

| Function | Description |
|----------|-------------|
| `xoshiro::new ?seed?` | New stream. `seed` is any Lcl integer, reduced modulo 2³². Without a seed: `time(NULL)`, `clock()` and a per-process counter — *weak*, fine for games, not for anything that needs unpredictability. |
| `xoshiro::int $rng lo hi` | Uniform integer in `[lo, hi]` (inclusive). Whole `long` range supported. Rejection sampling, no modulo bias. Error if `lo > hi`. |
| `xoshiro::float $rng` | Uniform float in `[0, 1)`. |
| `xoshiro::shuffle $rng list` | New list, uniformly shuffled (Fisher–Yates). Element types preserved. Refuses text (`{a b c}`) like every list operation. |

## Test

```bash
./build/lcl packages/lcl-random/test/test.lcl
```
