# lcl-process

Process spawning and management for LCL.

## Requirements

- LCL core engine
- POSIX `fork`/`execvp`, `pipe`, `waitpid`
- POSIX pseudo-terminal support (`openpty`/`forkpty`) for PTY-backed spawns
- **Portability:** POSIX (Linux, macOS, BSD). Not portable to Windows due to use of `fork`/`exec` and pseudo-terminals.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_PROCESS=ON
cmake --build build
```

## Features

- **Safe by default**: Commands use argv lists, not shell parsing
- **Synchronous and asynchronous**: `process::run` for simple capture, `process::spawn` for interactive control
- **Stream operations**: Send to stdin, read from stdout/stderr
- **Resource management**: Handles auto-cleanup via finalizers

## Usage

```tcl
;; Synchronous capture
let result [process::run (echo hello)]
puts [get $result stdout]  ;; hello

;; Asynchronous interactive control
let h [process::spawn (cat)]
process::send $h "hello\n"
process::close-stdin $h
let output [process::read $h]
process::wait $h
process::close $h
```

## API Reference

### process::run

Synchronous execution with output capture.

```tcl
;; Basic usage
let result [process::run (echo hello)]
puts [get $result stdout]  ;; hello

;; With options
let result [process::run (cat) #{stdin "input data"}]
let result [process::run (my-cmd) #{env #{VAR value} cwd /tmp}]
let result [process::run (cmd | other) #{shell 1}]
```

**Options (dict):**
- `stdin` - string to send to stdin
- `env` - dict of environment variables
- `cwd` - working directory
- `merge` - merge stderr into stdout (1/0)
- `throw` - error on non-zero exit (1/0)
- `limit` - max bytes to capture per stream
- `shell` - run via /bin/sh -c (1/0)

**Returns:** `#{status N stdout "..." stderr "..."}`

### process::spawn

Asynchronous execution, returns a handle for interactive control.

```tcl
let h [process::spawn (cat)]
process::send $h "hello\n"
process::close-stdin $h
let output [process::read $h]
process::wait $h
process::close $h
```

**Options (dict):**
- `env` - dict of environment variables
- `cwd` - working directory
- `merge` - merge stderr into stdout (1/0)

### Stream Operations

```tcl
process::send $h "data"           ;; Write to stdin
process::close-stdin $h           ;; Half-close stdin
process::read $h                  ;; Read from stdout
process::read $h #{stderr 1}      ;; Read from stderr
process::read $h #{timeout 1000}  ;; Read with timeout (ms)
process::read $h #{n 4096}        ;; Max bytes to read
```

### Lifecycle

```tcl
process::wait $h                  ;; Block until exit
process::wait $h #{timeout 5000}  ;; Wait with timeout
process::alive? $h                ;; Check if still running
process::kill $h                  ;; Send SIGTERM
process::kill $h #{signal KILL}   ;; Send specific signal
process::close $h                 ;; Close handle, free resources
```

## Example: Expect-like Pattern

```tcl
;; Spawn an interactive process
let h [process::spawn (some-interactive-program)]

;; Send commands and read responses
process::send $h "login\n"
let response [process::read $h #{timeout 5000}]

if [String::find $response "Password:"] {
    process::send $h "secret\n"
}

process::wait $h
process::close $h
```

## Tests

Tests live in `packages/lcl-process/test/`. Run via ctest:

```bash
cmake -S . -B build \
  -DLCL_BUILD_PROCESS=ON \
  -DLCL_BUILD_TESTS=ON \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_TEST_LIB=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
ctest --test-dir build -R lcl-process
```

The `LCL_BUILD_IO` and `LCL_BUILD_TEST_LIB` flags are required because the test suite uses `puts` (lcl-io) and the `Test::suite` framework (Test lib).
