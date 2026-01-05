# lcl-crypto

Cryptographic functions for LCL using OpenSSL.

## Build

```bash
# From the lcl root directory
cmake -DLCL_BUILD_CRYPTO=ON -B build
cmake --build build

# Or standalone
cd packages/lcl-crypto
cmake -B build
cmake --build build
```

Requires OpenSSL development libraries.

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
