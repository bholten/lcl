# Lcl - Lexical Command Language

[![CI](https://github.com/bholten/lcl/actions/workflows/ci.yml/badge.svg)](https://github.com/bholten/lcl/actions/workflows/ci.yml)

> **Pre-alpha software**: This project is in early development. APIs, syntax, and semantics may change substantially.

Lcl is a Tcl-inspired scripting language with **lexical scoping**. It provides the simplicity and extensibility of Tcl while adding modern features like closures, immutable bindings, and namespaces.

Lcl is implemented in **C89** with no dependencies except for a hosted C standard library.

> ⚠️⚠️⚠️ **Coming from Tcl? Read this first.** ⚠️⚠️⚠️
>
> Lcl *looks* like Tcl:
>
> commands, words, `$var`, `[...]`, braces -- but it is **not** Tcl.
>
> - **`{a b c}` is text, not a list.** Lists are `(a b c)`, dicts are
>   `#{k v}`. `len {a b c}` is 5. `foreach x {a b c}` is an error.
>   Nothing ever reparses a string as a list -- use `String::split`.
> - **No `set`.** `let` binds immutably; `var` makes a mutable cell,
>   `set!` mutates it. No `upvar`, `uplevel`, `global`.
> - **Lexical scoping and closures.** Procs capture where they were
>   defined, not who called them.
> - **No `expr`.** Arithmetic and comparison are prefix commands:
>   `[+ 1 2]`, `[< $i 10]`. `if [< $i 10] {...}` -- brackets, not
>   braces, on the condition (a braced condition is a non-empty
>   string, i.e. always true). `while {[< $i 10]} {...}` is the
>   exception: braces there mean "re-evaluate each iteration".
> - **`42` is an int, `"42"` is a string.** Literals are typed at
>   compile time; interpolation preserves types.
> - **`$a::b` is invalid.** Qualified names are `${a::b}`.
> - **Extended names need braces.** `let foo-bar` and `proc empty?`
>   are fine, and `${foo-bar}`/`${empty?}` substitute them; bare
>   `$name` is POSIX-shaped, so `-`, `?`, `!` end it.
> - **`[$x]` does not dispatch.** It yields `$x`; use `[apply $x]`.
> - **Namespaces are values** built with `namespace name { ... }`;
>   `proc a::b` outside one is an error. **No** `namespace eval`!
> - **Comments are `;;` anywhere**; `#` mid-line is literal.
> - **Reference counting, not GC** -- mutual recursion between procs
>   is rejected at definition time.
>

## Examples

```tcl
;; Immutable bindings, and mutable cells
let x 10
var counter 0
set! counter [+ $counter $x]
puts "counter = $counter"          ;; counter = 10

;; Procedures
proc greet {name} {
    return "Hello, $name!"
}

puts [greet "World"]

;; Closures capture their environment
proc make_counter {start} {
    var n $start
    lambda {} { set! n [+ $n 1]; $n }
}

let c [make_counter 10]
puts [apply $c]  ;; 11 -- `[$c]` would just return the closure
puts [apply $c]  ;; 12
```

## Build

```sh
cmake -B build && cmake --build build
cmake -B build -DLCL_ENABLE_ASAN=ON && cmake --build build  # with sanitizers
```

## Command Line

```sh
# Run a script file
lcl script.lcl

# Execute code directly with -c
lcl -c 'puts [+ 1 2 3]'

# Read script from stdin
echo 'puts hello' | lcl -
cat script.lcl | lcl -

# Arguments after the script (or after the -c code) are the list $argv;
# `exit ?status?` ends the script with that status (0 by default)
lcl tool.lcl --verbose input.txt      # $argv = (--verbose input.txt)
lcl -c 'exit [len $argv]' a b         # exit status 2
```

`exit` is provided by the CLI, not the core: it aborts evaluation the
way a host budget does (`catch` cannot resume the script), frees the
interpreter normally, and returns the status. Test runners end with
`exit [Test::run]` so a failing suite fails the process.

## Documentation

The documentation is a static site rendered from sources in this
repository.

- See full [documentation](https://bholten.github.io/lcl/)
- See [tools/docs.lcl](tools/docs.lcl) for how its generated

## Packages

Core Lcl is strict C89 and maximally portable. Optional packages under
`packages/` (`Io::`, `Posix::`, `Json::`, `Regex::`, `Curl::`, ...) are
thin bindings that may depend on a system library or a platform; each is
off by default and enabled with `-DLCL_BUILD_<NAME>=ON`. See the
[overview](docs/index.lcl) for the full table and the philosophy.

## Project Status

Lcl is **pre-alpha** software. While the core language is functional, expect:

- API changes
- Missing features
- Bugs and edge cases
- Limited documentation

Contributions and feedback are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md)
for how to build, test, and submit a PR.

## License

MIT
