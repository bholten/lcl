# lcl-random

Seeded pseudo-random streams for Lcl: xoshiro128**, exposed under the
engine's own name, `Xoshiro::`. Not cryptographically secure — use
`Crypto::random_bytes` (lcl-crypto) for anything security-sensitive.

Build with `-DLCL_BUILD_RANDOM=ON`. ISO C only; builds everywhere the
core builds.

Documentation: [docs/Xoshiro.lcl](docs/Xoshiro.lcl) — rendered on the docs site.
