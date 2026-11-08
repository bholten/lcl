# lcl-expect

Automation of interactive programs: spawn a child on a pseudo-terminal, send it input, and wait for patterns in its output with handlers that are ordinary Lcl code.

Build with `-DLCL_BUILD_EXPECT=ON` (requires `-DLCL_BUILD_PROCESS=ON`). POSIX only (pseudo-terminals via lcl-process); not portable to Windows.

Documentation: [docs/Expect.lcl](docs/Expect.lcl) — rendered on the docs site. A complete, tested scenario lives in [examples/router.lcl](examples/router.lcl).
