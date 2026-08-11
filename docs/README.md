# docs/ — Doc sources

This directory holds **doc sources** for the `Doc` library
(`lib/doc`). Two kinds of file live here:

1. **The project module file** — `lcl.lcl`. Module-level prose about
   the language itself; the seed for generating README.md.
2. **Companion doc files** — one per namespace whose implementation
   is in C (or anywhere else the `;;;` convention can't reach).

## Companion file convention

**One file per top-level namespace, named after it, case-exact:**
`String::` is documented by `docs/String.lcl`. **Nested namespaces
get subfolders:** `weft::repl` is documented by `docs/weft/repl.lcl`.

**The file's content is authoritative; the path is for humans.** Each
file contains a single namespace builder whose name is the *fully
qualified* namespace:

```tcl
;;; One-line namespace summary.
namespace String { ... }            ;# docs/String.lcl

;;; One-line namespace summary.
namespace weft::repl { ... }        ;# docs/weft/repl.lcl
```

`Doc::extract` takes the builder name verbatim, so a qualified
builder yields correctly qualified entries (`weft::repl::history`)
without any path-derived logic. A namespace with only small children
may keep them as nested builders in one file; give a child its own
file (and subfolder) when it deserves one.

**Bodies are stubs; files are never evaluated.** Companion files are
input to the reader-based extractor only — `load`/`require` on one
would shadow the real C commands with empty stubs. Write real
parameter lists (they become the documented signature, including
optional-arg syntax like `(sep " ")`) and empty bodies:

```tcl
;;; Return the index of the first occurrence of `sub` in `s`, or -1.
;;;
;;; Examples:
;;; >> String::find "banana" "na"
;;; 2
proc find {s sub} {}
```

Other extractor rules that apply as usual: a `;;;` run directly
above a definition documents it; a run separated by a blank line is
module/namespace prose; `let`/`var` stubs are included only when
documented; names starting with `_` are skipped (don't document
private helpers).

**Doctests run against the real implementation.** Because the file
is never evaluated, its `>>` examples execute in an interpreter
where the *actual* C commands are registered. Companion doctests are
therefore genuine tests of the C code:

```tcl
load lib/doc/src/Doc.lcl
Doc::report [Doc::doctest [Doc::extract $src]]        ;# src = file text
;# or, with the io package available:
Doc::report [Doc::doctest_file docs/String.lcl]
```

Remember doctest expectations compare against `repr`: strings appear
quoted (`"abc"`), ints bare (`3`), lists as `("a" "b")`.

**Multi-line examples.** Consecutive `>>` lines join into one
command while the accumulated text fails to parse (an open brace,
quote, or bracket); a line that parses complete closes the command.
No continuation marker is needed, and single-line examples behave
exactly as before. A bare `>>` is a visual separator:

```tcl
;;; Examples:
;;; >> proc add {a b} {
;;; >>   + $a $b
;;; >> }
;;; >>
;;; >> add 1 2
;;; 3
```

**Authoring gotcha:** inside a braced body (a namespace builder, a
proc), `;;`/`;;;` comment text still participates in the outer
scan's brace balancing — a comment containing a lone `{`, `"`, or
`[` breaks the enclosing file. Spell them out ("an open brace")
or keep them paired.
