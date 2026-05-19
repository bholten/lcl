# Versioning Policy

LCL follows [Semantic Versioning 2.0.0](https://semver.org/) with one
explicit pre-1.0 carve-out (see below).

## Querying the version

At compile time:

```c
#include <lcl-version.h>

#if LCL_VERSION_NUMBER < 200          /* anything before 0.2.0 */
/* ... workaround ... */
#endif

const char *v = LCL_VERSION_STRING;   /* "0.1.0" */
```

`LCL_VERSION_NUMBER` is `MAJOR*10000 + MINOR*100 + PATCH`. MINOR and
PATCH are each kept in the 0..99 range.

At runtime:

```c
#include <lcl.h>

printf("linked against %s\n", lcl_version());
```

The compile-time string and the runtime function return the same
value at the time the library was *compiled*. They will disagree if
the program was compiled against one set of headers and linked
against a shared library of a different version — that mismatch is
the primary thing `lcl_version()` exists to catch.

From the CLI:

```
$ lcl --version
lcl 0.1.0
```

The CLI output is stable: `lcl <version>\n` with no trailing platform
or feature blurb. Scripts and CI can cut on whitespace.

## What gets versioned

LCL is one repository with several distinct surfaces. They version
together — a single `MAJOR.MINOR.PATCH` covers everything — but the
*meaning* of "breaking change" differs by surface.

### Core C API (`include/lcl.h`, `include/lcl-version.h`)

The contract for embedders. A breaking change here is anything that
makes correctly-written existing C code fail to compile, link, or
run correctly:

- Removing a function, type, or macro.
- Changing a function signature (parameter types, return type, count).
- Changing the semantics of an existing function (e.g. ownership
  rules, NULL behavior, error returns) — including tightening
  contracts in a way callers must adapt to.
- Changing a struct layout that embedders allocate or inspect.

ABI vs. API: pre-1.0, the shared library `SOVERSION` follows MAJOR
(currently `0`), so the entire 0.x line is treated as one ABI epoch.
We may still break ABI within 0.x — see the pre-1.0 carve-out.

### Lcl language semantics

The script-level contract. A breaking change is anything that makes
a previously-correct `.lcl` program produce a different result or
fail to run:

- Removing or renaming a stdlib command.
- Changing the semantics of an existing command.
- Changing the parse rules (new reserved syntax, new escape, etc.).
- Tightening an error case that used to succeed.

New stdlib commands, new built-in types, and new syntax that doesn't
conflict with existing programs are **not** breaking changes.

### Packages (`packages/`)

Each package (lcl-io, lcl-curl, lcl-crypto, …) ships under the same
version as core but has its own portability and dependency
expectations. See each package's `README.md` for what it requires
(POSIX, OpenSSL, libcurl, etc.). A package can be added, removed, or
restructured in any release that bumps MINOR — packages are
opt-in, behind CMake flags, and not part of the core surface.

## Pre-1.0 carve-out

Standard SemVer says pre-1.0 versions promise nothing. We're slightly
stricter than that:

- **PATCH** (`0.1.0` → `0.1.1`): bug fixes and internal changes only.
  No intentional breaking changes to any surface.
- **MINOR** (`0.1.0` → `0.2.0`): may include breaking changes to
  any surface. The release notes will call them out.
- **MAJOR** (`0.x` → `1.0`): this is the stability commitment, not
  a free pass for breakage. 1.0 says "we're done changing the
  core API and language semantics on a whim."

In practice that means: within an 0.x.y line, upgrading is safe;
between 0.x lines, read the release notes.

## Post-1.0 (the future)

Standard SemVer with no carve-outs:

- **PATCH**: bug fixes, no surface changes.
- **MINOR**: additive only. New commands, new C functions, new
  packages. Existing surfaces unchanged.
- **MAJOR**: any breaking change to any surface.

## Release process

(Sketch — to be filled in once 0.1.0 actually ships.)

1. Bump `PROJECT_VERSION` in `CMakeLists.txt`.
2. Reconfigure CMake (`cmake -S . -B build ...`) — the generated
   `lcl-version.h` picks up the new value.
3. Update release notes / `CHANGELOG.md`.
4. Tag the commit `vX.Y.Z`.
5. Publish.
