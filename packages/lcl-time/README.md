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

## API Reference

All functions live in the `time::` namespace.

### Wall-clock and CPU time

| Function | Description |
|----------|-------------|
| `time::time` | Current Unix timestamp (seconds since epoch, integer) |
| `time::clock` | Process CPU clock ticks since program start |
| `time::monotonic_us` | Monotonic clock reading in microseconds (POSIX `CLOCK_MONOTONIC`) |

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
