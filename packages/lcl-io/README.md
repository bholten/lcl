# lcl-io

File I/O for LCL, bound to the ANSI C standard library and nothing else.

## Requirements

- LCL core engine
- A hosted ANSI C89 library (`stdio.h`, `getenv`)
- **Portability:** everywhere core Lcl builds, including Windows.
  POSIX filesystem operations (directory listing, glob, stat
  predicates, working-directory control) live in the separate
  `lcl-posix` package.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_IO=ON
cmake --build build
```

## Usage

```tcl
# Read entire file
let contents [io::read_file "config.txt"]

# Write file
io::write_file output.txt "Hello, World!"

# Stream-based I/O
let f [io::open_file data.txt r]

while 1 {
    let line [io::fgets $f 1024]
    if [== [len $line] 0] { break }
    puts $line
}
io::close_file $f
```

## API Reference

### File I/O (Whole File)

| Function | Description |
|----------|-------------|
| `io::read_file $path` | Read entire file contents |
| `io::write_file $path $data` | Write data to file (overwrites) |
| `io::copy $src $dst` | Copy file contents |

### File I/O (Streaming)

| Function | Description |
|----------|-------------|
| `io::open_file $path $mode` | Open file (mode: r, w, rb, wb, etc.) |
| `io::close_file $handle` | Close file handle |
| `io::fgets $handle $size` | Read line (up to size-1 bytes; size ≥ 2). Returns `""` at EOF |
| `io::fputs $handle $data` | Write string to file |
| `io::flush $handle` | Flush file buffer |
| `io::eof? $handle` | 1 if the handle has hit end-of-file, else 0 |

`io::fgets` keeps the trailing newline, so a blank line reads as `"\n"` —
an empty result always means end-of-file. Read errors raise an error;
EOF never does (use `io::eof?` or test the length, as in the loop above).

(For stdout writes, use bare `puts` from the core stdlib.)

### Standard Streams

| Function | Description |
|----------|-------------|
| `io::stdin` | Get stdin file handle |
| `io::stdout` | Get stdout file handle |
| `io::stderr` | Get stderr file handle |

### File Operations

| Function | Description |
|----------|-------------|
| `io::remove $path` | Delete file |
| `io::rename $old $new` | Rename/move file or directory |

### Environment

| Function | Description |
|----------|-------------|
| `io::getenv $name` | Get environment variable |

## Tests

Tests live in `packages/lcl-io/test/`. Run via ctest:

```bash
cmake -S . -B build \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_TESTS=ON \
  -DLCL_BUILD_TEST_LIB=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
ctest --test-dir build -R lcl-io
```

The `LCL_BUILD_TEST_LIB` flag is required because the test suite uses the `Test::suite` framework. `LCL_BUILD_IO` is the package under test.
