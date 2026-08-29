# lcl-time

Time, clock, calendar and profiling functions for Lcl.

Build with `-DLCL_BUILD_TIME=ON`.

The calendar API is ISO C and builds everywhere; the monotonic clock,
sub-second sleep and profiler need POSIX `clock_gettime`/`nanosleep`.

Documentation: [docs/Time.lcl](docs/Time.lcl) — rendered on the docs site.
