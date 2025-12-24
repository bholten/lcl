# Lcl - Lexical Command Language

## Build

```sh
# Standard build
cmake -B build && cmake --build build

# Debug with sanitizers
cmake -B build -DLCL_ENABLE_ASAN=ON && cmake --build build

# Install
cmake --install build --prefix /usr/local
```
