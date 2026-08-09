# Lcl - Lexical Command Language

[![CI](https://github.com/bholten/lcl/actions/workflows/ci.yml/badge.svg)](https://github.com/bholten/lcl/actions/workflows/ci.yml)

> **Pre-alpha software**: This project is in early development. APIs, syntax, and semantics may change substantially.

Lcl is a Tcl-inspired scripting language with **lexical scoping**. It provides the simplicity and extensibility of Tcl while adding modern features like closures, immutable bindings, and namespaces.

Lcl is implemented in **C89** with no dependencies except for a hosted C standard library.

## Why?

Tcl is woefully unappreciated. It is one of the best languages for creating DSLs and embedding them in C/C++ projects. It's been described as "Lisp for C programmers" and it definitely hits that same itch: homoiconicity ("everything is a string") and simple metaprogramming by passing around literal blocks of code and operating on them as data.

However, Tcl's scoping rules are... awkward. Whenever I've worked with Tcl, I often find myself fighting the scoping, and wishing it was simply lexical.

Hence, Lcl.

The intent for Lcl is therefore focused on DSL embeddings and scripting for C/C++ projects, not as a complete replacement for -- or even any compatability with -- Tcl. This constrains the design and scope of the project: I'm not particularly interested in making a full language with an independent runtime. I'm focused on making embedding into C/C++ projects easy and fun; a way to use a Tcl-like extentions language with more Scheme-like semantics.

## Key Differences from Tcl

| Feature    | Tcl                              | Lcl                                                            |
|------------|----------------------------------|----------------------------------------------------------------|
| Scoping    | Dynamic (`upvar`, `uplevel`)     | Lexical (closures)                                             |
| Bindings   | Mutable by default               | Immutable by default (`let`), explicit mutation (`var`/`set!`) |
| Memory     | Garbage collected                | Reference counted                                              |
| Closures   | Limited                          | First-class (flat closures)                                    |
| Namespaces | `proc ns::foo` anywhere          | Must define inside `namespace` block (see below)               |
| Comments   | `#` at line start                | `;;` anywhere (Lisp-style)                                     |
| `if`       | `if {expr} {body} elseif ...`    | `if $cond {then} else {else}` (Scheme-style, value-based)      |
| Branching  | `elseif`/`elsif` keywords        | Nested `if` only (no elseif)                                   |
| Quoting    | `expr` command for expressions   | Expressions are just commands                                  |
| Dispatch   | `[$x]` runtime-dispatches if `$x` names a command | `[$x]` returns the value of `$x`; `[apply $x]` dispatches (see below) |
| Philosophy | "Everything is a string"         | "Everything is a string... inside a closure"                   |

Lcl uses a more unified API, does not use the ensemble pattern for dictionaries, and does not use the prefix-convention for list operations.

| Tcl            | Lcl            |
|----------------|----------------|
| llength x      | len x          |
| dict get d k   | get d k        |
| lindex x i     | get x i        |
| dict set d k v | put d k v      |
| lappend x v    | List::push x v |
| dict keys d    | Dict::keys d   |

And many others.

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
```

## Quick Start

```tcl
;; Variables and immutable bindings
let x 10
puts "x = $x"

;; Mutable bindings use cells
var counter 0
set! counter [+ $counter 1]
puts "counter = $counter"

;; Procedures
proc greet {name} {
    return "Hello, $name!"
}

puts [greet "World"]

;; Closures capture their environment
proc make_counter {start} {
    var n $start

    return [lambda {} {
        set! n [+ $n 1]
        $n
    }]
}

let c [make_counter 10]
puts [apply $c]  ;; 11
puts [apply $c]  ;; 12
;; Note: [$c] does NOT call the closure -- it returns the closure value.
;; `apply` is the explicit way to invoke a callable held in a variable.
;; See the "Dispatch and apply" section below for the design rationale.
```

## Language Features

### Data Types

```tcl
;; Strings (the fundamental type)
let s "hello world"
let empty ""           ;; empty string (same as {})

;; Numbers (integers and floats)
let n 42
let f 3.14

;; Lists - constructor or literal syntax
let lst [list a b c d e]
let lst2 (a b c d e)   ;; list literal (same as above)
let empty_list ()      ;; empty list
puts [len $lst]        ;; 5
puts [get $lst 2]      ;; c

;; Dictionaries - constructor or literal syntax
let d [dict name "Alice" age 30]
let d2 #{name "Alice" age 30}  ;; dict literal (same as above)
let empty_dict #{}     ;; empty dict
puts [get $d name]     ;; Alice
puts [has? $d age]     ;; 1
```

### Generic Operations

These operations work across multiple types:

```tcl
;; len - get length/size
puts [len (1 2 3)]             ;; 3
puts [len #{a 1 b 2}]          ;; 2
puts [len "hello"]             ;; 5
puts [len $ns]                 ;; number of namespace bindings

;; get - access by index/key
puts [get $list 0]             ;; first element
puts [get $dict key]           ;; value for key
puts [get "hello" 1]           ;; "e"
puts [get $ns name]            ;; namespace binding
puts [get $ns name fallback]   ;; default when key/binding is absent

;; put - functional update (returns new value)
let lst2 [put $lst 0 replaced]
let d2 [put $d newkey value]

;; del - functional delete
let d3 [del $d key]

;; has? - membership test
puts [has? $lst value]         ;; 1 if value in list (deep equality)
puts [has? $d key]             ;; 1 if key exists
puts [has? "hello" "ell"]      ;; 1 if substring present
puts [has? $ns name]           ;; 1 if namespace binding exists

;; empty? - check if empty
puts [empty? ()]               ;; 1
puts [empty? $lst]             ;; 0
```

### Type Predicates

```tcl
puts [list? $lst]      ;; 1
puts [dict? $d]        ;; 1
puts [string? "hi"]    ;; 1
puts [number? 42]      ;; 1
puts [proc? $greet]    ;; 1
puts [cell? [ref 0]]   ;; 1
```

### Namespaced Functions

Type-specific operations are organized into namespaces:

```tcl
;; List operations
puts [List::push $lst newitem]     ;; append item
puts [List::pop $lst]              ;; remove last (returns list)
puts [List::reverse $lst]          ;; reverse
puts [List::slice $lst 1 3]        ;; slice [1,3)
puts [List::concat $lst1 $lst2]    ;; concatenate

;; Dict operations
puts [Dict::keys $d]               ;; list of keys
puts [Dict::values $d]             ;; list of values
puts [Dict::merge $d1 $d2]         ;; merge dicts

;; String operations
puts [String::upper "hello"]       ;; HELLO
puts [String::lower "HELLO"]       ;; hello
puts [String::find "hello" "ll"]   ;; 2
puts [String::replace "hello" "l" "L"]  ;; heLLo
puts [String::split "a,b,c" ","]   ;; list: a b c
puts [String::join $lst "-"]       ;; a-b-c-d-e
```

### Reader Reflection (Lex)

`Lex::commands` reads text with the language's own reader without
evaluating any of it, and returns the lexical structure: a list of
`#{line N words (...)}` records, one per statement.  Each word record
carries `text` (the decoded literal value: escapes processed,
quotes/braces stripped), `dynamic` (1 when evaluation would compute
part of the word -- a `$var` or `[subcommand]` piece, including `()`
and `#{}` literals), the `quoted`/`braced`/`expand` flags, and
`span` -- the word's half-open `(start end)` byte range in the input.
Spans let tooling recover any word's original bytes verbatim
(quoting, escapes, and `@` intact) with `String::range`, instead of
re-quoting decoded text; quoting style is semantic in lcl (`while`
dispatches on braced-ness, bare identifiers on quoting/shape), so
slicing is the safe way to re-emit source.
Dynamic words have `text ""` plus a `pieces` list
(`#{kind lit text ...}` / `#{kind var name ...}` / `#{kind sub}`)
describing why.  Malformed input errors with the compiler's message.
This exists so embedders can classify interactive text -- lcl call,
external command, shell handoff -- against the real grammar instead
of a lookalike parser.

```tcl
let r [Lex::commands {ls -lsa}]
let head [get [get [get $r 0] words] 0]
puts [get $head text]      ;; ls
puts [get $head dynamic]   ;; 0
puts [get $head span]      ;; 0 2
puts [String::range {ls -lsa} @[get $head span]]  ;; ls
```

### Control Flow

```tcl
;; if - takes a VALUE (not a block), Scheme-style
;; Note: Unlike Tcl, there is no elseif/elsif - use nested if instead
if $condition {
    puts "true"
} else {
    puts "false"
}

;; Nested if for multiple branches
if [< $x 0] {
    puts "negative"
} else {
    if [== $x 0] {
        puts "zero"
    } else {
        puts "positive"
    }
}

;; cond - multi-branch conditional (like Scheme/Lisp)
;; Evaluates conditions in order, runs first truthy branch
;; Use 'else' for default case, errors if no match without else
let result [cond
             [< $x 0] {negative}
             [== $x 0] {zero}
             else {positive}]

;; case - value dispatch (like switch/match)
;; Compares value against keys, runs matching expression
;; Keys are evaluated, so $variables work
let msg [case $op
         {add} [+ $a $b]
         {sub} [- $a $b]
         {mul} [* $a $b]
         else {unknown op}]

;; while
var i 5

while {$i} {
    puts $i
    set! i [- $i 1]
}

;; for
for {var j 10} {$j} {set! j [- $j 1]} {
    puts $j
}

;; foreach
foreach item $lst {
    puts $item
}

;; break and continue work as expected
foreach x (1 2 3 4 5) {
    if [== $x 3] { continue }
    if [== $x 5] { break }
    puts $x
}
```

### Threading Operators (Clojure-style)

Thread values through a series of operations:

```tcl
;; -> threads as FIRST argument
let result [-> $data {get key} {String::upper}]
;; Equivalent to: String::upper [get $data key]

;; ->> threads as LAST argument
let result [->> $value {transform a b}]
;; Equivalent to: transform a b $value

;; Chain multiple operations
let d #{a 1 b 2 c 3}
let d2 [-> $d {put d 4} {del a}]  ;; add d, remove a

;; Works with lambdas
let inc [lambda {x} {+ $x 1}]
puts [-> 10 {$inc} {$inc}]        ;; 12

;; Inline lambdas too
puts [-> 10 {[lambda {x} {+ $x 100}]}]  ;; 110
```

### Namespaces

Lcl namespaces work **very differently from Tcl**. In Lcl, namespaces are **first-class module values** created with a builder pattern. This is closer to ML modules or Scheme libraries than Tcl namespaces.

```tcl
;; Define a namespace - all definitions use UNQUALIFIED names inside the block
namespace math {
    let pi 3.14159

    proc double {x} { + $x $x }

    ;; Mutable state works too
    var counter 0

    proc increment {} {
        set! counter [+ $counter 1]
        $counter
    }
}

;; Access namespace members with :: syntax
puts $math::pi              ;; 3.14159
puts [math::double 21]      ;; 42
puts [math::increment]      ;; 1
puts [math::increment]      ;; 2
```

**Key differences from Tcl:**

1. **`namespace eval` does not exist** - Namespaces are values and more like modules, not evaluations blocks that change scope.

```tcl
;; Named namespace
namespace foo {
    proc bar {} { ... }
}

;; This desugars to:
let foo [namespace {
    proc bar {} { ... }
}]
```

2. **No qualified definitions outside `namespace`** - You cannot write `proc math::double {x} {...}` at the top level. This is a hard error:
```tcl
;; ERROR: qualified name not allowed here
proc math::double {x} { + $x $x }
```

Why? Lcl has lexical scoping, and qualified defintions outside of `namespace` bring up all kinds of unsound lexical scope issues.

3. **Re-entering a namespace extends it** - Calling `namespace` on an existing namespace gives access to its bindings and allows adding new ones:
```tcl
namespace utils { let x 1 }
namespace utils { let y 2 }    ;; Can access $x here

puts "$utils::x $utils::y"          ;; 1 2
```

4. **Nested namespaces are compositional** - Nested `namespace` creates bindings in the parent:
```tcl
namespace outer {
    let x 1

    namespace inner {
        let y 2

        proc greet {} { return "hello" }
    }
}

puts $outer::inner::y              ;; 2
puts [outer::inner::greet]         ;; hello
```

5. **Nested paths as shorthand** - You can create deep namespace hierarchies directly:
```tcl
namespace a::b::c { let deep 42 }

puts $a::b::c::deep                ;; 42
```

6. **Closures capture namespace variables** - Procs defined in a namespace capture cells for `set!`:

```tcl
namespace counter {
    var n 0

    proc inc {} { set! n [+ $n 1]; $n }

    proc dec {} { set! n [- $n 1]; $n }
}

puts [counter::inc]    ;; 1
puts [counter::inc]    ;; 2
puts [counter::dec]    ;; 1
```

### Import

The `import` command copies bindings from a namespace into the current scope, allowing you to use them without qualification:

```tcl
namespace math {
    let pi 3.14159

    proc square {x} { * $x $x }
}

;; Import all bindings
import math

puts $pi              ;; 3.14159
puts [square 5]       ;; 25

;; Import specific bindings
namespace utils {
    let a 1
    let b 2
    let c 3
}

import utils a c      ;; Only import a and c

puts $a               ;; 1
puts $c               ;; 3
```

**Key behaviors:**

1. **Shared mutation** - Imported cells (mutable bindings) share state with the original:
```tcl
namespace counter {
    var n 0

    proc incr {} { set! n [+ $n 1] }
}

import counter n incr

incr
puts $n             ;; 1
puts $counter::n    ;; 1 (same cell)
```

2. **Conflict detection** - Import errors if a name already exists in the current scope:
```tcl
let x 1

namespace ns { let x 99 }

import ns x         ;; ERROR: 'x' already exists in current scope
```

3. **Works with nested namespaces** - Use qualified paths:
```tcl
namespace outer {
    namespace inner { let deep 42 }
}

import outer::inner deep

puts $deep          ;; 42
```

4. **Works inside namespace builders** - Import bindings while defining a namespace:
```tcl
namespace helpers { proc helper {} { 42 } }

namespace app {
    import helpers helper

    proc run {} { helper }
}

puts [app::run]     ;; 42
```

### Dispatch and `apply`

Lcl's dispatch rule is a deliberate departure from Tcl that is worth
calling out explicitly. Tcl users in particular should read this
section before being surprised by the behavior.

**The rule:** A one-word command (a subcommand `[word]` or a
standalone statement) **dispatches** only when its sole word is a
**bare identifier** -- a name like `foo` with no `$`, no `[...]`, no
braces, no quotes, no special syntax. Every other form -- `$var`,
`[expr]`, `{lit}`, `(list)`, `#{dict}`, literal numbers, multi-piece
concatenation -- is a **value form**, and yields its value when used as
a one-word command. No command lookup is attempted.

To dispatch a value-form (e.g. a closure held in a variable, or a
command name stored as a string), use `apply`.

```tcl
;; Value forms -- these never dispatch:
let val "GET"
puts [$val]              ;; -> "GET" (variable substitution, NOT a call to GET)

let c [lambda {} { 42 }]
puts [proc? [$c]]        ;; -> 1 (the closure value, not a call)

puts [42]                ;; -> 42
puts [{hello world}]     ;; -> "hello world"

;; Bare identifiers -- these dispatch:
proc GET {} { return "called" }
puts [GET]               ;; -> "called"

;; ...and an unknown bare identifier is an ERROR, never a silent
;; fallback to its own text. Bare words meant as data must be value
;; forms -- quoted or braced:
let c [if $cond { "red" } else { "blue" }]   ;; ok
;; let c [if $cond { red } else { blue }]    ;; error if `red` is not a command

;; Explicit dispatch via apply:
puts [apply $c]          ;; -> 42 (call the closure stored in $c)
puts [apply "GET"]       ;; -> "called" (call by name)
puts [apply ${+} 1 2 3]  ;; -> 6 (apply a builtin)

;; Spread a list as args via the @ operator
proc sum3 {a b c} { + $a $b $c }
let args (10 20 30)
puts [apply ${sum3} @$args]  ;; -> 60
```

**Why this design?**

Tcl's one-word dispatch rule is *runtime-decided*: at the head of a
one-word program, Tcl evaluates the word, then looks the result up as
a command. If the value's string form happens to match a registered
command, Tcl dispatches. This is convenient -- `eval $cmd` works for
stored command names -- but it leaks. Anywhere a value's string form
might coincide with a proc name (cached values, macro template
substitutions, anaphoric conditions), Tcl will silently dispatch
instead of returning the value. The same source line can mean two
different things depending on data flowing through it.

Lcl makes the dispatch decision at **parse time**. The shape of the
source determines whether a one-word command dispatches; the data
flowing through it doesn't. A `$var` is always a variable lookup,
never a call. An `[expr]` always yields whatever `expr` returned. The
Tcl idiom of dispatching a stored name survives via `eval $cmd` --
`eval` compiles `$cmd`'s value as source, and a one-word source like
`GET` is a bare identifier that dispatches. Use `apply` when you want
value-dispatch instead.

**`apply` vs `eval`** -- these are complementary, not overlapping:

- `eval str` is *source-evaluation*: compile `str` as Lcl source and run it.
- `apply value args…` is *value-dispatch*: take a callable value and call it.

`apply` resolves:
- `PROC` (non-macro) -- call directly with the args.
- `CPROC` normal -- call directly.
- `CPROC` special -- error ("cannot apply special form"; specials want raw
  unevaluated words, which `apply` doesn't carry).
- `PROC` with `is_macro=1` -- error ("cannot apply macro"; use
  `macroexpand` + `eval` for programmatic macro use).
- `STRING` -- look up as a command name and recurse.
- Anything else -- error ("not callable").

**Tcl-to-Lcl migration cheat sheet:**

| Tcl                 | Lcl                |
|---------------------|--------------------|
| `eval $cmd`         | `apply $cmd` (or `eval $cmd` if `$cmd` is source text) |
| `[$closure]`        | `[apply $closure]` |
| `[[expr]]`          | `[apply [expr]]`   |
| `$closure $a $b`    | `apply $closure $a $b` |
| `apply $fn $args` (list-splat) | `apply $fn @$args` (`@` spreads the list) |

### Eval and Subst

```tcl
;; eval - execute string as code in current scope
eval {puts "hello"}
let code "puts world"
eval $code

;; subst - substitute variables and commands in string
let x 42
puts [subst {x is $x, sum is [+ 1 2]}]
;; Output: x is 42, sum is 3
```

## Operators

```tcl
;; Arithmetic -- integer-preserving when the result is exact,
;; otherwise float.
puts [+ 1 2 3]         ;; 6
puts [- 10 3]          ;; 7
puts [* 2 3 4]         ;; 24
puts [/ 10 2]          ;; 5    (exact integer result)
puts [/ 10 3]          ;; 3.333... (non-integer result -> float)

;; Comparison
puts [== $a $b]        ;; value equality (deep for lists/dicts)
puts [!= $a $b]        ;; value inequality
puts [< $a $b]         ;; less than
puts [> $a $b]         ;; greater than

;; Identity
puts [same? $a $b]     ;; same object?
puts [not-same? $a $b]
```

## Metaprogramming

### Quasiquote

Quasiquote provides Lisp-style template syntax for code generation:

```tcl
;; Basic quasiquote with unquote
let name "Alice"
let code [quasiquote { puts ,$name }]
eval $code  ;; prints: Alice

;; Unquote syntax:
;;   ,$var     - substitute variable value
;;   ,[cmd]    - evaluate command and insert result
;;   ,@$list   - splice list elements (space-separated)
;;   ,{lit}    - insert literal value
;;   \,        - literal comma (escaped)
;;
;; Note: Commas inside "quoted strings" are literal, not unquotes.
;; Use string concatenation or build strings outside quasiquote.

;; Command substitution in templates
let x 10
let y 20
let expr [quasiquote { + ,$x ,$y }]
puts [eval $expr]  ;; 30

;; Splice-unquote for lists
let args (1 2 3)
let code [quasiquote { + ,@$args }]
puts [eval $code]  ;; 6

;; Building a macro
;; Note: wrap params and body in literal braces so the expansion
;; produces a well-formed `proc` call. Quasiquote's unquote inlines
;; values directly -- it doesn't re-quote them.
proc defun {name params body} {
    quasiquote {
        proc ,$name {,@$params} {,$body}
    }
}

eval [defun greet {who} { puts "Hello, $who!" }]
greet "World"  ;; Hello, World!

;; Note: macro form is essentially but identical, but will eval
;; at the call-site. Hence, this is equivalent:
macro defn {name params body} {
    quasiquote {
        proc ,$name {,@$params} {,$body}
    }
}

defn greet2 {who} { puts "Hello2, $who!" } ;; no eval needed!
greet2 "world" ;; Hello2, world!
```

### Nested Quasiquote (Macro-Writing-Macros)

For writing macros that generate other macros, use `,,` (double comma):

```tcl
;; At depth 2, ,,$var evaluates NOW and produces ,<value> in output
;; Single ,$var is preserved for later evaluation

macro make-adder-macro {name amount} {
    quasiquote {
        proc ,$name {x} {
            eval [quasiquote {
                + ,$x ,,$amount
            }]
        }
    }
}

;; Generate an add10 macro
make-adder-macro add10 10

;; Use it -- produces code: + 5 10
puts [add10 5] ;; 15
```

## Syntax Reference

### Quoting

```tcl
;; Braces - literal, no substitution
let x {$a [+ 1 2]}     ;; literally: $a [+ 1 2]

;; Quotes - substitution happens
let y "$a [+ 1 2]"     ;; substitutes $a and evaluates [+ 1 2]

;; Brackets - command substitution
let z [+ 1 2]          ;; evaluates to 3

;; Parens - list literal
let lst (a b c)        ;; same as [list a b c]

;; Hash-braces - dict literal
let d #{a 1 b 2}       ;; same as [dict a 1 b 2]
```

**Data belongs in braces.** This is the deliberate division of labor:
`{...}` is a true literal, `"..."` is a substitution template, and
`[subst {...}]` turns a literal into a template explicitly when that
is what you mean. The practical consequence -- and a classic trap for
anyone arriving from languages where double quotes are the default
string syntax -- is that any string whose *content* legitimately
contains `[`, `$`, or `\` must be brace-quoted, or substitution will
silently rewrite it:

```tcl
;; Regex character classes are command substitutions inside quotes:
regex::find "([a-z]+):([0-9]+)" $text   ;; WRONG: [a-z]+ etc. run as
                                        ;; commands; pattern mangled,
                                        ;; typically matching nothing
regex::find {([a-z]+):([0-9]+)} $text   ;; RIGHT: braced literal

;; Same for shell snippets, code fragments, printf-style templates:
let cmd {awk '{print $1}' data.txt}     ;; $1 stays literal
```

Rule of thumb: quote (`"..."`) only when you *want* interpolation;
brace everything else. This also applies inside dict literals --
`#{pattern {[0-9]+}}` -- where a quoted value substitutes exactly as
it would anywhere else.

### Comments

```tcl
;; This is a comment (Lisp-style, anywhere on line)
puts "hello"  ;; This is also a comment
```

### Line Continuation

```tcl
dict key1 value1 \
     key2 value2
```

### Multiline Subcommands

Inside `[...]` brackets, newlines and semicolons are treated as ordinary whitespace. This allows multiline expressions without explicit line continuation:

```tcl
;; Multiline subcommand - no backslashes needed inside [...]
let result [list
    [list a b]
    [list c d]
    [list e f]
]

;; Also works with list literals inside brackets
let nested ((item1 item2)
            (item3 item4))
```

**Important:** This only applies to code *inside* brackets. Bare commands (not wrapped in `[...]`) still follow normal Tcl-like rules and require backslash continuation for multilines:

```tcl
;; Bare command - backslashes REQUIRED for line continuation
case $cmd \
  {add}  [+ $a $b] \
  {sub}  [- $a $b] \
  {mul}  [* $a $b] \
  else   {unknown}

;; Same command wrapped in brackets - backslashes not needed inside
let result [case $cmd
             {add}  [+ $a $b]
             {sub}  [- $a $b]
             {mul}  [* $a $b]
             else   {unknown}]
```

Semicolons and newlines inside `{...}` braces and `"..."` quotes are always preserved:

```tcl
;; Shell commands with semicolons work correctly
let output [sh::run {echo "hello"; echo "world"}]

;; Quoted strings preserve newlines
let text "line1
line2"
```

### Functional Programming

  | Function              | Description                                                           |
  |-----------------------|-----------------------------------------------------------------------|
  | List::map f l         | Apply f to each element, return new list                              |
  | List::filter f l      | Keep elements where f returns true                                    |
  | List::reduce init f l | Fold list with f(acc, elem)                                           |
  | Dict::map f d         | Apply f(key, value) to each entry, return new dict with mapped values |
  | Dict::filter f d      | Keep entries where f(key, value) returns true                         |
  | Dict::reduce init f d | Fold dict with f(acc, key, value)                                     |

## Embedding

Lcl is designed to be embedded in C applications:

```c
#include <lcl.h>

int main(void) {
    lcl_interp *interp = lcl_interp_new();
    lcl_register_core(interp);

    lcl_value *result = NULL;
    lcl_eval_string(interp, "puts {Hello from Lcl!}", &result);

    if (result) lcl_ref_dec(result);
    lcl_interp_free(interp);
    return 0;
}
```

For a fuller tour -- registering a C function as a Lcl command,
defining a Lcl variable from C, extracting the result, and
surfacing error location -- see [`examples/embed_example.c`](examples/embed_example.c).
Build it from the project's CMake with:

```bash
cmake -S . -B build -DLCL_BUILD_EXAMPLES=ON
cmake --build build
./build/examples/embed_example
```

To consume Lcl from your own CMake project after `cmake --install`:

```cmake
find_package(lcl REQUIRED)

add_executable(myapp myapp.c)
target_link_libraries(myapp PRIVATE lcl::lcl)
```

Or link directly: `gcc myapp.c -o myapp -llcl`.

## Packages

Core Lcl is strict C89 with no dependencies beyond a hosted C library,
and aims for absolute maximum portability. The optional extension
libraries under `packages/` are allowed to break both rules. A package
may require a system library (OpenSSL, libcurl) or bind to a platform
(POSIX `fork`, pseudo-terminals). Every package is off by default
(`-DLCL_BUILD_<NAME>=ON` to opt in), and each one declares exactly
what it needs in the Requirements section of its own README.

Packages are thin bindings: a package exposes what its underlying
library actually does, under a namespace named for what it is. The
regex package provides `regex::` and is POSIX extended `regex.h` and
nothing else. If, say, we made a new regular expressions package based
on PCRE2, it would be in `pcre2::` package. Lcl is not trying to ship
a batteries-included standard library or an engine-agnostic regex
framework.

| Package | Namespace | Depends on |
|---------|-----------|------------|
| lcl-io | `io::` | POSIX (`dirent.h`, `sys/stat.h`, `glob.h`) |
| lcl-math | `math::` | libm |
| lcl-time | `time::` | ISO C `<time.h>`; POSIX for monotonic clock / sleep |
| lcl-json | `json::` | cJSON (C99) |
| lcl-regex | `regex::` | POSIX `regex.h` |
| lcl-process | `process::` | POSIX (`fork`/`exec`, pipes, PTYs) |
| lcl-expect | `expect::` | lcl-process, POSIX PTYs |
| lcl-crypto | `crypto::` | OpenSSL |
| lcl-curl | `curl::` | libcurl |

## Known Limitations

### Mutual Recursion

Lcl uses **reference counting** for memory management (no garbage collector). This is a deliberate design choice for embeddability--GC adds complexity, unpredictable pauses, and makes integration with host applications harder.

Sequential `proc` definitions support mutual recursion without issues:

```tcl
;; This works -- no reference cycle:
proc even? {n} { if [== $n 0] {1} else {odd? [- $n 1]} }
proc odd? {n} { if [== $n 0] {0} else {even? [- $n 1]} }
puts [odd? 13]   ;; 1
```

This is safe because when `even?` is defined, `odd?` doesn't exist yet, so it isn't captured as an upvalue. At runtime, `even?` finds `odd?` through the caller's frame. The reference graph is one-way (a DAG), not a cycle.

However, **mutual recursion is not tail-call optimized**. Lcl's TCO only applies to self-recursive calls (where the callee is the same proc as the caller). Mutually recursive procs consume a stack frame per call and will hit the maximum recursion depth (1024) for large inputs:

```tcl
puts [odd? 13]     ;; works fine
puts [odd? 2000]   ;; ERROR: maximum recursion depth exceeded
```

Additionally, reference counting cannot handle reference cycles. When two **mutable cells** each hold a procedure that captures the other cell, they form a cycle that can never be freed:

```tcl
;; This is REJECTED -- would create a reference cycle:
var is_even_fn {}
var is_odd_fn {}
set! is_even_fn [lambda {n} { [$is_odd_fn [- $n 1]] }]
set! is_odd_fn [lambda {n} { [$is_even_fn [- $n 1]] }]  ;; ERROR
```

Lcl detects this at `set!` time and raises an error rather than silently leaking memory.

Self-recursion works fine (a proc calling itself), and Lcl includes tail call optimization for self-recursive procedures.

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
