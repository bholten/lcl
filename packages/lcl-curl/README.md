# lcl-curl

HTTP client bindings for Lcl using libcurl.

Build with `-DLCL_BUILD_CURL=ON` (needs libcurl 7.x or 8.x). Portable wherever libcurl builds.
In-tree only for 0.1.0: not part of the `cmake --install` artifact (libcurl is vendored via `FetchContent`).

Documentation: [docs/Curl.lcl](docs/Curl.lcl) — rendered on the docs site.
