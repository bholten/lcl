#ifndef LCL_CRYPTO_H
#define LCL_CRYPTO_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register crypto commands with the interpreter.
 * Creates a "crypto" namespace with:
 *   Crypto::sha256 data           - Compute SHA-256 hash (hex string)
 *   Crypto::sha512 data           - Compute SHA-512 hash (hex string)
 *   Crypto::hmac key data algo    - Compute HMAC (algo: sha256|sha512)
 *   Crypto::sign_rsa_pss key data algo  - RSA-PSS signature (algo: sha256|sha512)
 *   Crypto::sign_ecdsa key data curve   - ECDSA signature (curve: p256|p384)
 *   Crypto::base64_encode data    - Base64 encode data
 *   Crypto::base64_decode data    - Base64 decode data
 */
void lcl_register_crypto(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif
