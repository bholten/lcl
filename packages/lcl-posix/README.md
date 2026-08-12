# lcl-posix

POSIX filesystem and process-environment bindings for LCL.

## Requirements

- LCL core engine
- POSIX system headers: `dirent.h`, `glob.h`, `sys/stat.h`, `unistd.h`
- **Portability:** POSIX (Linux, macOS, BSD). Deliberately **not**
  portable to Windows — this package exists so that the POSIX-only
  operations have an honestly named home. Portable ANSI C file I/O
  lives in `lcl-io`.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_POSIX=ON
cmake --build build
```

## Usage

```tcl
# Directory listing
let files [posix::readdir "."]
foreach f $files {
    puts $f
}

# Glob matching
foreach f [posix::glob "*.lcl"] {
    puts "script: $f"
}

# Predicates
if [posix::dir? build] {
    puts "configured tree present"
}
```

## API Reference

### Directory Operations

| Function | Description |
|----------|-------------|
| `posix::mkdir $path ?mode?` | Create directory (default mode: 0755) |
| `posix::rmdir $path` | Remove empty directory |
| `posix::readdir $path` | List directory contents |
| `posix::getcwd` | Get current working directory |
| `posix::chdir $path` | Change working directory |

### Pattern Matching

| Function | Description |
|----------|-------------|
| `posix::glob $pattern` | Match files by glob pattern |

### File Information

| Function | Description |
|----------|-------------|
| `posix::exists? $path` | Check if path exists (returns 0/1) |
| `posix::file? $path` | Check if path is a regular file |
| `posix::dir? $path` | Check if path is a directory |
| `posix::file_size $path` | Get file size in bytes |
| `posix::file_mtime $path` | Get modification time (Unix timestamp) |

## History

These commands lived in `lcl-io` under `io::` until 2026-08-12
(ISSUES.md #81). They were split out so `lcl-io` could be pure ANSI C:
`io::glob` → `posix::glob`, and likewise for `readdir`, `mkdir`,
`rmdir`, `exists?`, `file?`, `dir?`, `file_size`, `file_mtime`,
`getcwd`, `chdir`.

## Tests

Tests live in `packages/lcl-posix/test/`. Run via ctest:

```bash
cmake -S . -B build \
  -DLCL_BUILD_POSIX=ON \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_TESTS=ON \
  -DLCL_BUILD_TEST_LIB=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
ctest --test-dir build -R lcl-posix
```

`LCL_BUILD_TEST_LIB` supplies the `Test::suite` framework;
`LCL_BUILD_IO` is needed because the test suite uses `io::write_file`
/ `io::remove` to create and clean up file fixtures.
