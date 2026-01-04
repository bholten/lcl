#ifndef LCL_CRYPTO_H
#define LCL_CRYPTO_H

#include <lcl.h>

/*
 * Register crypto commands with the interpreter.
 * Creates a "crypto" namespace with:
 *   crypto::sha256 data           - Compute SHA-256 hash (hex string)
 *   crypto::sha512 data           - Compute SHA-512 hash (hex string)
 *   crypto::hmac key data algo    - Compute HMAC (algo: sha256|sha512)
 *   crypto::sign_rsa_pss key data algo  - RSA-PSS signature (algo: sha256|sha512)
 *   crypto::sign_ecdsa key data curve   - ECDSA signature (curve: p256|p384)
 *   crypto::base64_encode data    - Base64 encode data
 *   crypto::base64_decode data    - Base64 decode data
 */
void lcl_register_crypto(lcl_interp *interp);

#endif
