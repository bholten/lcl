# lcl-crypto

Cryptographic functions for LCL using OpenSSL.

## Requirements

- LCL core engine
- OpenSSL 1.1+ (libssl-dev on Debian/Ubuntu, openssl on macOS via Homebrew)
- **Portability:** Portable wherever OpenSSL builds — Linux, macOS, BSD, and Windows.

## Build

```bash
cmake -S . -B build -DLCL_BUILD_CRYPTO=ON
cmake --build build
```

## Usage

```tcl
# Hashing
let hash [crypto::sha256 "Hello, World!"]
puts "SHA256: $hash"

# HMAC
let mac [crypto::hmac "secret-key" "message" sha256]
puts "HMAC: $mac"

# Base64
let encoded [crypto::base64_encode "Hello"]
let decoded [crypto::base64_decode $encoded]
```

## API Reference

### Hashing

| Function | Description |
|----------|-------------|
| `crypto::sha256 $data` | SHA-256 hash (returns hex string) |
| `crypto::sha512 $data` | SHA-512 hash (returns hex string) |

### Random bytes

| Function | Description |
|----------|-------------|
| `crypto::random_bytes $n` | `n` cryptographically secure random bytes from OpenSSL `RAND_bytes` (returns hex string, `2n` characters; `n` ≤ 1 MiB) |

This is the generator for keys, tokens, nonces, and salts. For
simulation, games, and anything that needs a *seeded, reproducible*
stream, use `xoshiro::` from `lcl-random` instead — the two are
different engines with different guarantees, deliberately not hidden
behind a common interface.

### HMAC

| Function | Description |
|----------|-------------|
| `crypto::hmac $key $data $algo` | HMAC signature (returns hex string) |

Supported algorithms: `sha256`, `sha384`, `sha512`

### Digital Signatures

| Function | Description |
|----------|-------------|
| `crypto::sign_rsa_pss $key_pem $data $algo` | RSA-PSS signature |
| `crypto::sign_ecdsa $key_pem $data $curve` | ECDSA signature |

**RSA-PSS** uses PKCS#1 PSS padding with salt length equal to digest length.
- Algorithms: `sha256`, `sha384`, `sha512`

**ECDSA** automatically selects the hash based on curve:
- `p256` - uses SHA-256
- `p384` - uses SHA-384
- `p521` - uses SHA-512

```tcl
# RSA-PSS signing
let rsa_key {-----BEGIN RSA PRIVATE KEY-----
...
-----END RSA PRIVATE KEY-----}
let sig [crypto::sign_rsa_pss $rsa_key "message" sha256]

# ECDSA signing
let ec_key {-----BEGIN EC PRIVATE KEY-----
...
-----END EC PRIVATE KEY-----}
let sig [crypto::sign_ecdsa $ec_key "message" p256]
```

### Base64

| Function | Description |
|----------|-------------|
| `crypto::base64_encode $data` | Encode string to base64 |
| `crypto::base64_decode $data` | Decode base64 to string |

## Tests

Tests live in `packages/lcl-crypto/test/`. Run via ctest:

```bash
cmake -S . -B build \
  -DLCL_BUILD_CRYPTO=ON \
  -DLCL_BUILD_TESTS=ON \
  -DLCL_BUILD_IO=ON \
  -DLCL_BUILD_TEST_LIB=ON \
  -DLCL_BUILD_CLI=ON
cmake --build build
ctest --test-dir build -R lcl-crypto
```

The `LCL_BUILD_IO` and `LCL_BUILD_TEST_LIB` flags are required because the test suite uses `puts` (lcl-io) and the `Test::suite` framework (Test lib).

The crypto package has three test files: `lcl-crypto` (core), `lcl-crypto-ecdsa` (ECDSA signing), and `lcl-crypto-signing` (RSA-PSS signing). The `-R lcl-crypto` filter matches all three.
