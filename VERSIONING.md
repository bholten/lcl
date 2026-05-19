# Versioning Policy

LCL follows [Semantic Versioning 2.0.0](https://semver.org/) with one
explicit pre-1.0 carve-out (see below).

## What gets versioned

LCL is one repository with several distinct surfaces. They version
together — a single `MAJOR.MINOR.PATCH` covers everything — but the
*meaning* of "breaking change" differs by surface.

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
