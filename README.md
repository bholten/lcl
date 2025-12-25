# Lcl - Lexical Command Language

> **Pre-alpha software**: This project is in early development. APIs, syntax, and semantics may change substantially.

Lcl is a Tcl-inspired scripting language with **lexical scoping**. It provides the simplicity and extensibility of Tcl's "everything is a string" philosophy while adding modern features like closures, immutable bindings, and namespaces.

## Key Differences from Tcl

| Feature | Tcl | Lcl |
|---------|-----|-----|
| Scoping | Dynamic (`upvar`, `uplevel`) | Lexical (closures) |
| Bindings | Mutable by default | Immutable by default (`let`), explicit mutation (`var`/`set!`) |
| Memory | Garbage collected | Reference counted |
| Closures | Limited | First-class (flat closures) |

## Build

```sh
# Debug build with sanitizers (recommended for development)
make debug

# Run tests
./lcl test/conformance_smoke.lcl

# Or use CMake
cmake -B build && cmake --build build
cmake -B build -DLCL_ENABLE_ASAN=ON && cmake --build build  # with sanitizers
```

## Quick Start

```tcl
# Variables and immutable bindings
let x 10
puts "x = $x"

# Mutable bindings use cells
var counter 0
set! counter [+ $counter 1]
puts "counter = $counter"

# Procedures
proc greet {name} {
    return "Hello, $name!"
}
puts [greet "World"]

# Closures capture their environment
proc make_counter {start} {
    var n $start
    return [lambda {} {
        set! n [+ $n 1]
        $n
    }]
}
let c [make_counter 10]
puts [$c]  ;# 11
puts [$c]  ;# 12
```

## Language Features

### Data Types

```tcl
# Strings (the fundamental type)
let s "hello world"

# Numbers (integers and floats)
let n 42
let f 3.14

# Lists
let lst [list a b c d e]
puts [len $lst]        ;# 5
puts [get $lst 2]      ;# c

# Dictionaries
let d [dict name "Alice" age 30]
puts [get $d name]     ;# Alice
puts [has? $d age]     ;# 1
```

### Generic Operations

These operations work across multiple types:

```tcl
# len - get length/size
puts [len [list 1 2 3]]        ;# 3
puts [len [dict a 1 b 2]]      ;# 2
puts [len "hello"]             ;# 5

# get - access by index/key
puts [get $list 0]             ;# first element
puts [get $dict key]           ;# value for key
puts [get "hello" 1]           ;# "e"

# put - functional update (returns new value)
let lst2 [put $lst 0 replaced]
let d2 [put $d newkey value]

# del - functional delete
let d3 [del $d key]

# has? - check existence
puts [has? $lst value]         ;# 1 if value in list
puts [has? $d key]             ;# 1 if key exists

# empty? - check if empty
puts [empty? [list]]           ;# 1
puts [empty? $lst]             ;# 0
```

### Type Predicates

```tcl
puts [list? $lst]      ;# 1
puts [dict? $d]        ;# 1
puts [string? "hi"]    ;# 1
puts [number? 42]      ;# 1
puts [proc? $greet]    ;# 1
puts [cell? [ref 0]]   ;# 1
```

### Namespaced Functions

Type-specific operations are organized into namespaces:

```tcl
# List operations
puts [List::push $lst newitem]     ;# append item
puts [List::pop $lst]              ;# remove last (returns list)
puts [List::reverse $lst]          ;# reverse
puts [List::slice $lst 1 3]        ;# slice [1,3)
puts [List::concat $lst1 $lst2]    ;# concatenate

# Dict operations
puts [Dict::keys $d]               ;# list of keys
puts [Dict::values $d]             ;# list of values
puts [Dict::merge $d1 $d2]         ;# merge dicts

# String operations
puts [String::upper "hello"]       ;# HELLO
puts [String::lower "HELLO"]       ;# hello
puts [String::find "hello" "ll"]   ;# 2
puts [String::replace "hello" "l" "L"]  ;# heLLo
puts [String::split "a,b,c" ","]   ;# list: a b c
puts [String::join $lst "-"]       ;# a-b-c-d-e
```

### Control Flow

```tcl
# if/elseif/else
if $condition {
    puts "true"
} elseif $other {
    puts "other"
} else {
    puts "false"
}

# while
var i 5
while $i {
    puts $i
    set! i [+ $i -1]
}

# for
for {var j 0} {$j} {set! j [+ $j -1]} {
    puts $j
}

# foreach
foreach item $lst {
    puts $item
}

# break and continue work as expected
foreach x [list 1 2 3 4 5] {
    if [== $x 3] { continue }
    if [== $x 5] { break }
    puts $x
}
```

### Threading Operators (Clojure-style)

Thread values through a series of operations:

```tcl
# -> threads as FIRST argument
let result [-> $data {get key} {String::upper}]
# Equivalent to: String::upper [get $data key]

# ->> threads as LAST argument
let result [->> $value {transform a b}]
# Equivalent to: transform a b $value

# Chain multiple operations
let d [dict a 1 b 2 c 3]
let d2 [-> $d {put d 4} {del a}]  ;# add d, remove a

# Works with lambdas
let inc [lambda {x} {+ $x 1}]
puts [-> 10 {$inc} {$inc}]        ;# 12

# Inline lambdas too
puts [-> 10 {[lambda {x} {+ $x 100}]}]  ;# 110
```

### Namespaces

```tcl
# Define a namespace
namespace eval math {
    let pi 3.14159
    proc double {x} { + $x $x }
}

# Access namespace members
puts $math::pi
puts [math::double 21]
```

### Eval and Subst

```tcl
# eval - execute string as code
eval {puts "hello"}
let code "puts world"
eval $code

# subst - substitute variables and commands in string
let x 42
puts [subst {x is $x, sum is [+ 1 2]}]
# Output: x is 42, sum is 3
```

## Operators

```tcl
# Arithmetic
puts [+ 1 2 3]         ;# 6
puts [- 10 3]          ;# 7
puts [* 2 3 4]         ;# 24
puts [/ 10 3]          ;# 3

# Comparison
puts [== $a $b]        ;# value equality
puts [!= $a $b]        ;# value inequality
puts [< $a $b]         ;# less than
puts [> $a $b]         ;# greater than

# Identity
puts [same? $a $b]     ;# same object?
puts [not-same? $a $b]
```

## Syntax Reference

### Quoting

```tcl
# Braces - literal, no substitution
let x {$a [+ 1 2]}     ;# literally: $a [+ 1 2]

# Quotes - substitution happens
let y "$a [+ 1 2]"     ;# substitutes $a and evaluates [+ 1 2]

# Brackets - command substitution
let z [+ 1 2]          ;# evaluates to 3
```

### Comments

```tcl
# This is a comment (must be at start of line or after ;)
puts "hello"  ;# This is also a comment
```

### Line Continuation

```tcl
dict create \
    key1 value1 \
    key2 value2
```

## Embedding

Lcl is designed to be embedded in C applications:

```c
#include "lcl.h"

int main() {
    lcl_interp *interp = lcl_interp_new();
    lcl_register_core(interp);

    lcl_value *result = NULL;
    lcl_eval_string(interp, "puts {Hello from Lcl!}", &result);

    if (result) lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
```

Link with `-llcl` or include the source files directly.

## Project Status

Lcl is **pre-alpha** software. While the core language is functional, expect:

- API changes
- Missing features
- Bugs and edge cases
- Limited documentation

Contributions and feedback are welcome!

## License

MIT
