# Lcl in the browser

The core compiles to WebAssembly with Emscripten and ships as a
JavaScript module. Two things come out of an `emcmake` build:

- **`lcl.js` + `lcl.wasm`** -- the ordinary CLI as a node program.
  `node build-emcc/lcl.js script.lcl` behaves like `./build/lcl
  script.lcl` (host filesystem, `argv`, `exit`), so the whole ctest
  suite runs under it unchanged.
- **`lcl.mjs` + `lcl-core.mjs` + `lcl-core.wasm`** -- the JavaScript
  host: an ES module for browsers and node, built from
  `wasm/lcl-wasm.c` and `wasm/lcl.mjs`.

```sh
emcmake cmake -S . -B build-emcc -DCMAKE_BUILD_TYPE=Release \
      -DLCL_BUILD_TESTS=ON -DLCL_BUILD_TEST_LIB=ON \
      -DLCL_BUILD_IO=ON -DLCL_BUILD_JSON=ON -DLCL_BUILD_REGEX=ON \
      -DLCL_BUILD_TIME=ON -DLCL_BUILD_MATH=ON -DLCL_BUILD_RANDOM=ON \
      -DLCL_BUILD_JS=ON -DLCL_BUILD_DOM_LIB=ON
cmake --build build-emcc --parallel
ctest --test-dir build-emcc
```

`Dag emcc` runs exactly that. The packages listed are the portable
set (ANSI C, or POSIX that musl provides); `Curl`, `Crypto`,
`Process`, `Posix` and `Expect` are platform-bound and do not build
here -- their browser counterparts (`fetch`, WebCrypto) are reached
through `Js::` (lcl-js, on by default under Emscripten: the
JavaScript host engine as a package, see its docs page), not as
ports. The pure-Lcl libraries
(`Test`, `Doc`, `Bench`, and `Dom` -- the declarative DOM library,
on by default under Emscripten) are embedded and need no filesystem.

## The JavaScript host

```js
import createLcl from './lcl.mjs';

const lcl = await createLcl({ print: line => output.append(line + '\n') });

lcl.define('greet', name => `hello, ${name}`);
lcl.eval('puts [greet world]');            // -> print("hello, world")

const add = lcl.eval('lambda {a b} { + $a $b }');
add(2, 3);                                 // -> 5

lcl.setBudget(1_000_000);                  // abort runaway scripts
try { lcl.eval('while {1} {}'); } catch (e) { /* LclError: ... aborted ... */ }
```

`createLcl(options)` resolves to an `Lcl` instance. `options.print`
and `options.printErr` receive each line `puts` (stdout) and the
interpreter's own warnings (stderr) produce; both default to the
console.

| Method | Effect |
|---|---|
| `eval(src, file = '<eval>')` | Evaluate `src`; the result as a JS value. Errors throw `LclError` with `message`, `file`, `line`. |
| `define(name, value)` | Bind at the root. A JS function becomes a host procedure callable from Lcl; anything else is marshalled by value. |
| `get(name)` | Read a root binding (`Ns::name` allowed). |
| `call(proc, ...args)` | Call an Lcl procedure value; procedures returned by `eval`/`get` are also directly callable. |
| `setStepHook(fn, interval)` | `fn(lcl)` runs every `interval` commands; returning truthy aborts the evaluation (sticky, uncatchable by the script). |
| `setBudget(n)` | A hard per-eval command budget; `setBudget(0)` removes it. |
| `setModuleSource(source)` | Where `require`/`load` get module text: a `{path: source}` object or a `path => source \| null` function (throw to explain a failure); `null` restores the filesystem. |
| `addRequireRoot(dir)` | A directory bare `require` names are looked up under. |
| `abort()` | Abort the evaluation in progress, from a host procedure or a step hook. |
| `free()` | Release the interpreter. |
| `version` | The Lcl version string. |

### Values across the boundary

Values cross by type, not by text.

| Lcl | JavaScript | back to Lcl |
|---|---|---|
| string | `string` | string |
| int | `number`, or `BigInt` beyond ±2^53 | int (`BigInt` must fit 64 bits) |
| float | `number` | float when not integral |
| list | `Array` | list |
| dict | plain object | dict |
| cell | its contents | -- |
| proc / C proc | callable function (`f.release()`) | the same proc |
| namespace, opaque | `LclValue` handle (`toString()`, `release()`) | the same value |
| -- | `boolean` | int 1 / 0 |
| -- | `null`, `undefined` | `""` |
| -- | plain function | a lambda forwarding to the host |

Because JavaScript has one number type, an Lcl float that round-trips
through JS comes back as an int when it is integral (`2.0` becomes
`2`). Lcl ints are 64-bit; those outside Number's safe range arrive
as `BigInt` so no digits are lost, and a `BigInt` may be handed back
as long as it fits.

A JS function handed to Lcl -- by `define` or by value -- runs on the
same stack as the script; it may call back into Lcl freely
(`lcl.eval`, calling a procedure it was given), and an exception it
throws becomes an ordinary Lcl error the script can `catch`.

Every Lcl reference JavaScript holds (procedures, `LclValue` handles)
is released with `.release()`; a `FinalizationRegistry` releases what
the garbage collector reaches first. `free()` invalidates all of
them.

### How it is put together

Value marshalling is lcl-js's bridge (`Module.LclJs`, from
`packages/lcl-js/src/lcl-js-library.js`): the same code that backs
`Js::` converts host-procedure arguments and results, so a JavaScript
object handed to a host procedure arrives in Lcl as a `Js::ref` and a
proc returned to JavaScript is a callable function on both paths.

`wasm/lcl-wasm.c` is the browser counterpart of `src/lcl-main.c`: it
registers the core, the packages and the embedded libraries, and
exposes a flat `lclw_` surface -- out-parameters folded into return
values, `_take` variants so JS holds one reference at a time, and a
single C procedure `::Wasm::_host id args` that every host procedure
forwards to (`define('f', fn)` declares `proc f {*args} {
::Wasm::_host N $args }`). It is plain C89 with no Emscripten headers;
the export list lives on the link line in `CMakeLists.txt`, and
`wasm/lcl.mjs` reaches it through the module's exported functions.

## Limits

**Execution is synchronous.** A call into `eval` runs to completion
on the JavaScript stack; there is no yielding to the event loop from
inside a script and no blocking on a Promise. Events and Promise
results reach Lcl by calling back in. A step hook can watch a
wall-clock deadline or poll for interruption.

**Two stacks.** The wasm stack is set to 8 MiB at link time, matching
the native default (the Emscripten default of 64 KiB dies inside the
1024-deep recursion limit). The engine's own native stack is a second
limit, about 1 MiB on a browser main thread. The interpreter's
recursion limit fits comfortably, but the core walks nested values
recursively (building, checking, freeing and rendering a list nested
inside itself), so nesting deeper than roughly 6000 levels overflows
the browser's stack where native builds handle it (native builds
have the same limit at a larger number: 100000 levels segfaults). `ctest` under node
raises the stack (`--stack-size=8000`) so the conformance suite's
10000-deep case runs as it does natively.

**Integers are 64-bit here too.** An Lcl integer is `lcl_int`
(`include/lcl-int.h`), -2^63 .. 2^63-1 on every host regardless of
the C data model; under wasm32 it is a wasm `i64`, which is why the
JavaScript side sees `BigInt` for large values and the module is
linked with `WASM_BIGINT`. Container indexes remain host-sized
(`size_t`, 32 bits here), so an integer no container could index
fails as "out of range" rather than wrapping. The full conformance
suite passes under this build.

**Modules come from the host.** `require` and `load` read files
through the C library, which the CLI build sees through the host
filesystem (`NODERAWFS`) and the browser build does not see at all
(the embedded libraries need no files). A page serves modules itself
with `lcl.setModuleSource(...)` -- a `{path: source}` object or a
`path => source` function, called for every candidate path `require`
resolves and for every `load` -- which is `lcl_set_module_source_fn`
underneath; `lcl.addRequireRoot(dir)` registers search roots for bare
names.

**No `exit`.** `exit` is a CLI procedure; the browser host has none.
