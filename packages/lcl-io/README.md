# lcl-io

File system and I/O operations for LCL.

## Requirements

- LCL core engine
- POSIX system headers: `dirent.h`, `sys/stat.h`, `glob.h`
- **Portability:** POSIX (Linux, macOS, BSD). Not portable to Windows as-is due to use of `dirent.h`, `sys/stat.h` mode bits, and `glob.h`.

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
    io::puts $line
}
io::close_file $f

# Directory listing
let files [io::readdir "."]
foreach f $files {
    io::puts $f
}
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
| `io::fgets $handle $size` | Read line (up to size bytes) |
| `io::fputs $handle $data` | Write string to file |
| `io::puts $data` | Write string to stdout |
| `io::flush $handle` | Flush file buffer |

### Standard Streams

| Function | Description |
|----------|-------------|
| `io::stdin` | Get stdin file handle |
| `io::stdout` | Get stdout file handle |
| `io::stderr` | Get stderr file handle |

### Directory Operations

| Function | Description |
|----------|-------------|
| `io::mkdir $path ?mode?` | Create directory (default mode: 0755) |
| `io::rmdir $path` | Remove empty directory |
| `io::readdir $path` | List directory contents |
| `io::getcwd` | Get current working directory |
| `io::chdir $path` | Change working directory |

### File Operations

| Function | Description |
|----------|-------------|
| `io::remove $path` | Delete file |
| `io::rename $old $new` | Rename/move file or directory |
| `io::glob $pattern` | Match files by glob pattern |

### File Information

| Function | Description |
|----------|-------------|
| `io::exists? $path` | Check if path exists (returns 0/1) |
| `io::file? $path` | Check if path is a regular file |
| `io::dir? $path` | Check if path is a directory |
| `io::file_size $path` | Get file size in bytes |
| `io::file_mtime $path` | Get modification time (Unix timestamp) |

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

The `LCL_BUILD_TEST_LIB` flag is required because the test suite uses the `Test::suite` framework. `LCL_BUILD_IO` is the package under test, which itself provides `puts`.
