# lcl-expect

Expect-style automation for Lcl. Provides pattern-based matching for automating interactive programs, leveraging Lcl's lexical scoping for clean handler closures.

## Overview

lcl-expect provides a Tcl Expect-like interface for automating interactive programs via PTY or pipes. It combines low-level C primitives for pattern matching with a high-level Lcl convenience layer.

## Quick Start

```tcl
;; Spawn an interactive shell
let s [expect::spawn (bash)]

;; Wait for prompt, send command
expect::wait-for $s "$ "
expect::send-line $s "echo hello"

;; Match and handle patterns
expect::interact $s (
    ("password:" [lambda {m} { expect::send-line $s "secret"; continue }])
    ("$ " [lambda {m} { get $m data }])
)

;; Clean up
expect::close $s
```

## API Reference

### Configuration

#### `expect::set-timeout ms`
Set the default timeout in milliseconds (default: 10000).

#### `expect::get-timeout`
Get the current default timeout.

---

### Spawning

#### `expect::spawn cmd ?opts?`
Spawn a process for automation.

**Options:**
| Option | Default | Description |
|--------|---------|-------------|
| `pty` | 1 | Use PTY mode (1) or pipe mode (0) |
| `timeout` | 10000 | Default timeout for this session |
| `rows` | 24 | PTY rows |
| `cols` | 80 | PTY cols |
| `env` | `#{}` | Environment variables dict |
| `cwd` | `""` | Working directory |

**Returns:** Session dict `#{handle <h> timeout <t>}`

```tcl
;; PTY mode (default) - for interactive programs
let s [expect::spawn (bash)]

;; Pipe mode - for simple command output
let s [expect::spawn (ls -la) #{pty 0}]

;; Custom PTY size
let s [expect::spawn (vim) #{rows 40 cols 120}]
```

---

### Sending

#### `expect::send session str`
Send a string to the process.

#### `expect::send-line session str`
Send a string followed by newline.

#### `expect::send-ctrl session char`
Send a control character. `char` is a-z for Ctrl-A through Ctrl-Z.

```tcl
expect::send $s "hello"           ;; Send raw text
expect::send-line $s "ls -la"     ;; Send with newline
expect::send-ctrl $s "c"          ;; Send Ctrl-C
expect::send-ctrl $s "d"          ;; Send Ctrl-D (EOF)
```

---

### Simple Waiting

#### `expect::wait-for session pattern ?opts?`
Wait for a single pattern to appear.

**Options:**
| Option | Default | Description |
|--------|---------|-------------|
| `timeout` | session timeout | Timeout in ms |

**Returns:** Match result dict or empty on timeout.

```tcl
let r [expect::wait-for $s "$ "]
if [get $r matched] {
    puts "Got prompt!"
}
```

#### `expect::expect session patterns ?opts?`
Wait for any of multiple patterns.

**Arguments:**
- `patterns` - string or list of strings/patterns

**Returns:** Match result dict.

```tcl
let r [expect::expect $s ("password:" "$ " "# ")]
puts "Matched pattern [get $r index]"
```

---

### Handler-Based Matching

#### `expect::interact session pairs ?opts?`
Match patterns and call handlers. Single-shot matching.

**Arguments:**
- `pairs` - list of `(pattern handler)` pairs

**Returns:** Result of the matched handler.

```tcl
let result [expect::interact $s (
    ("password:" [lambda {m} { expect::send-line $s "secret"; "sent password" }])
    ("$ " [lambda {m} { get $m data }])
    ([expect::timeout] [lambda {m} { error "timed out" }])
)]
```

#### `expect::interact-loop session pairs ?opts?`
Loop with pattern matching until `break` or normal return.

Handlers should:
- Call `continue` to keep matching
- Call `break` to exit the loop
- Return normally to exit with a value

```tcl
expect::interact-loop $s (
    ("More--" [lambda {m} { expect::send $s " "; continue }])
    ("password:" [lambda {m} { expect::send-line $s $password; continue }])
    ("$ " [lambda {m} { break }])
)
```

---

### Match Result Dict

Match operations return a dict with these fields:

| Field | Description |
|-------|-------------|
| `matched` | 1 if matched, 0 otherwise |
| `index` | Index of matched pattern (0-based) |
| `data` | All data read (for `read-match`) |
| `before` | Text before the match (for `match-buffer`) |
| `match` | The matched text (for `match-buffer`) |
| `after` | Text after the match (for `match-buffer`) |
| `timeout` | 1 if timed out |
| `eof` | 1 if EOF reached |

---

### Lifecycle

#### `expect::alive? session`
Check if the process is still running.

#### `expect::wait session ?opts?`
Wait for the process to exit.

#### `expect::kill session ?opts?`
Send a signal to the process.

#### `expect::close session`
Close the session and clean up.

#### `expect::close-stdin session`
Close stdin to signal EOF to the process.

---

### PTY Operations

#### `expect::pty? session`
Check if session is using PTY mode.

#### `expect::set-winsize session rows cols`
Set terminal size (sends SIGWINCH).

#### `expect::get-winsize session`
Get current terminal size.

---

### Reading

#### `expect::read session ?opts?`
Read available output (non-blocking).

#### `expect::drain session ?opts?`
Read all available output until no more data.

```tcl
expect::send-line $s "ls"
;; Wait a bit, then drain all output
let output [expect::drain $s]
```

---

### Patterns

Patterns can be:
- **Strings** - literal substring match
- **Pattern objects** - created with helper functions

#### `expect::pattern str ?opts?` (C primitive)
Create a literal pattern object.

**Options:** `nocase` - case insensitive (1/0)

#### `expect::regex str ?opts?` (C primitive)
Create a regex pattern object (POSIX extended regex).

**Options:** `nocase` - case insensitive (1/0)

#### `expect::timeout` (C primitive)
Create a timeout sentinel pattern.

#### `expect::eof` (C primitive)
Create an EOF sentinel pattern.

#### Pattern Helpers (`expect::pat::` namespace)

```tcl
expect::pat::literal "hello"           ;; Literal pattern
expect::pat::regex {[0-9]+}            ;; Regex pattern
expect::pat::timeout                   ;; Timeout sentinel
expect::pat::eof                       ;; EOF sentinel
expect::pat::prompt                    ;; Common shell prompt regex
expect::pat::prompt #{type bash}       ;; Bash-specific prompt
```

---

### Convenience Combinators

#### `expect::send-expect session str pattern ?opts?`
Send string, then wait for pattern.

#### `expect::send-line-expect session str pattern ?opts?`
Send line, then wait for pattern.

```tcl
let r [expect::send-line-expect $s "whoami" "$ "]
```

---

### Script Runner

#### `expect::run session script`
Run a script (list of actions).

**Action types:**
- `(send "text")` - send text
- `(send-line "text")` - send line
- `(expect "pattern")` - wait for pattern
- `(expect "pattern" timeout_ms)` - wait with custom timeout
- `(sleep ms)` - pause

**Returns:** List of expect results.

```tcl
let results [expect::run $s (
    (expect "login:")
    (send-line "admin")
    (expect "password:")
    (send-line "secret")
    (expect "$ ")
)]
```

#### `expect::script cmd actions ?opts?`
Spawn, run script, and close in one call.

```tcl
let results [expect::script (bash) (
    (expect "$ ")
    (send-line "echo hello")
    (expect "$ ")
)]
```

---

## Low-Level C Primitives

These are available but typically you'd use the higher-level Lcl wrappers:

| Function | Description |
|----------|-------------|
| `expect::pattern?` | Check if value is a pattern |
| `expect::pattern-kind` | Get pattern kind ("literal", "regex", "timeout", "eof") |
| `expect::match-buffer` | Match patterns against a string buffer |
| `expect::read-match` | Read from handle and match patterns |
| `expect::match` | Pattern/handler matching (single) |
| `expect::loop` | Pattern/handler loop |

---

## Examples

### SSH Login Automation

```tcl
let s [expect::spawn (ssh user@host)]

expect::interact-loop $s (
    ("yes/no" [lambda {m} {
        expect::send-line $s "yes"
        continue
    }])
    ("password:" [lambda {m} {
        expect::send-line $s $password
        continue
    }])
    ("$ " [lambda {m} { break }])
    ([expect::timeout] [lambda {m} {
        error "SSH connection timed out"
    }])
)

;; Now at shell prompt
expect::send-line $s "hostname"
let r [expect::wait-for $s "$ "]
puts [get $r data]

expect::close $s
```

### Interactive Menu Navigation

```tcl
let s [expect::spawn (./menu-app)]

expect::interact-loop $s (
    ("Press any key" [lambda {m} {
        expect::send $s " "
        continue
    }])
    (">" [lambda {m} {
        ;; At menu, select option 2
        expect::send-line $s "2"
        continue
    }])
    ("Done" [lambda {m} { break }])
)

expect::close $s
```

### Handling Pagers

```tcl
let s [expect::spawn (man ls)]

expect::interact-loop $s (
    ;; Handle "more" style pagers
    ("--More--" [lambda {m} { expect::send $s " "; continue }])
    ;; Handle "less" style pagers
    (":" [lambda {m} { expect::send $s "q"; break }])
    ([expect::eof] [lambda {m} { break }])
)

expect::close $s
```

## Dependencies

- `lcl-process` - Process spawning and PTY support
