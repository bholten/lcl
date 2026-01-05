# lcl-process

Process spawning and management for LCL.

## Features

- **Safe by default**: Commands use argv lists, not shell parsing
- **Synchronous and asynchronous**: `process::run` for simple capture, `process::spawn` for interactive control
- **Stream operations**: Send to stdin, read from stdout/stderr
- **Resource management**: Handles auto-cleanup via finalizers

## API

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

## Building

As a subproject:
```cmake
add_subdirectory(packages/lcl-process)
target_link_libraries(your_target PRIVATE lcl_process)
```

Standalone:
```bash
cd packages/lcl-process
cmake -B build && cmake --build build
./build/lcl-process-test test/process_test.lcl
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
