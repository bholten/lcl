# lcl-time

Time, clock, and calendar functions for LCL.

## Requirements

- LCL core engine
- C standard library `<time.h>` (`time`, `clock`, `localtime`, `gmtime`, `mktime`, `strftime`, `difftime`, `ctime`)
- POSIX `nanosleep` for `time::sleep`
- `clock_gettime(CLOCK_MONOTONIC, ...)` for `time::monotonic_us`
- **Portability:** Wide. The bulk of the API uses ISO C `<time.h>` and builds on Linux, macOS, BSD, and Windows. The monotonic clock and sub-second sleep rely on POSIX features that may be unavailable on older Windows toolchains.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_TIME=ON
cmake --build build
```

## Usage

```tcl
;; Unix timestamp (seconds since epoch)
let now [time::time]
puts "epoch: $now"

;; Format with strftime
let tm [time::localtime $now]
puts [time::strftime "%Y-%m-%d %H:%M:%S" $tm]

;; Monotonic timing
let t0 [time::monotonic_us]
do-some-work
let elapsed [- [time::monotonic_us] $t0]
puts "elapsed: $elapsed us"

;; Sleep
time::sleep 0.25     ;; 250 ms
```

## Profiling

```tcl
proc update {e} { ... }
let rows [time::profile { foreach e $enemies { update $e } }]
puts [time::profile_format $rows]
;;      calls      incl_us      excl_us  name
;;       1492        61230        61230  update
```

`time::profile {body}` runs `body` in the caller's scope and returns
one `#{name calls incl_us excl_us}` row per user proc that ran, sorted
by exclusive time (body's own value is dropped; an error in the body
propagates). `time::profile_start!` / `time::profile_stop!`  bracket a
region from outside -- e.g. around a game loop -- and `profile_stop!`
returns the same rows. `time::profile_format $rows` renders them as an
aligned table.

Inclusive time is wall time between a proc's entry and exit; exclusive
subtracts the inclusive time of the user procs it called, so the cost
of C builtins is attributed to the proc that called them.  Tail
self-calls count as a single call. Timing uses the monotonic clock;
the hook itself costs well under a microsecond per call.

## API Reference

All functions live in the `time::` namespace.

### Wall-clock and CPU time

| Function | Description |
|----------|-------------|
| `time::time` | Current Unix timestamp (seconds since epoch, integer) |
| `time::clock` | Process CPU clock ticks since program start |
| `time::monotonic_us` | Monotonic clock reading in microseconds (POSIX `CLOCK_MONOTONIC`) |

### Profiling

| Function | Description |
|----------|-------------|
| `time::profile {body}` | Run `body`, return per-proc `#{name calls incl_us excl_us}` rows sorted by exclusive time |
| `time::profile_start!` | Start collecting (error if already running) |
| `time::profile_stop!` | Stop and return the rows (error if not running) |
| `time::profile_format $rows` | Render rows as an aligned text table |

### Calendar conversion

| Function | Description |
|----------|-------------|
| `time::localtime $epoch` | Break down a timestamp into local-time fields (dict) |
| `time::gmtime $epoch` | Break down a timestamp into UTC fields (dict) |
| `time::mktime $tm` | Convert a broken-down local-time dict back to a Unix timestamp |
| `time::ctime $epoch` | Format a timestamp as a human-readable string (`asctime` style) |

### Formatting and arithmetic

| Function | Description |
|----------|-------------|
| `time::strftime $fmt $tm` | Format a broken-down time dict using `strftime(3)` |
| `time::difftime $t1 $t0` | Difference between two timestamps (in seconds) |

### Sleeping

| Function | Description |
|----------|-------------|
| `time::sleep $seconds` | Sleep for the given number of seconds (fractional values supported) |

## Tests

Tests live in `packages/lcl-time/test/`. Run via ctest:

```bash
cmake -S . -B build \
  -DLCL_BUILD_TIME=ON \
  -DLCL_BUILD_TESTS=ON \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_TEST_LIB=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
ctest --test-dir build -R lcl-time
```

The `LCL_BUILD_IO` and `LCL_BUILD_TEST_LIB` flags are required because the test suite uses `puts` (lcl-io) and the `Test::suite` framework (Test lib).
