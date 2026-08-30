# lcl-js

The JavaScript host engine for Lcl, exposed under its own name,
`Js::`: property access, calls, construction, `eval`, and Lcl
procedures as JavaScript callbacks, with JavaScript objects held by
reference. Emscripten only, by design — it binds whatever
`globalThis` is (a browser page, or node).

Build with `emcmake cmake ... -DLCL_BUILD_JS=ON` (the default under
Emscripten). The C half is strict C89; the JavaScript half is
`src/lcl-js-library.js`, linked with `--js-library`.

Documentation: [docs/Js.lcl](docs/Js.lcl) — rendered on the docs site.
