# lcl-process

Process spawning and management for Lcl.

Build with `-DLCL_BUILD_PROCESS=ON`.

POSIX only (Linux, macOS, BSD): uses `fork`/`exec`, pipes and
pseudo-terminals; not portable to Windows. `lcl-expect` builds on it.

Documentation: [docs/Process.lcl](docs/Process.lcl) — rendered on the docs site.
