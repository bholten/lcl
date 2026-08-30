#include <stdlib.h>
#include <string.h>

#include <limits.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <lcl.h>

#define CRYPTO_NS "Crypto"

static const char HEX_CHARS[] = "0123456789abcdef";

static char *bytes_to_hex(const unsigned char *data, size_t len) {
  char *hex;
  size_t i;

  hex = (char *)malloc(len * 2 + 1);

  if (!hex) {
    return NULL;
  }

  for (i = 0; i < len; i++) {
    hex[i * 2] = HEX_CHARS[(data[i] >> 4) & 0x0F];
    hex[i * 2 + 1] = HEX_CHARS[data[i] & 0x0F];
  }

  hex[len * 2] = '\0';

  return hex;
}

static const EVP_MD *get_md_by_name(const char *name) {
  if (strcmp(name, "sha256") == 0) {
    return EVP_sha256();
  } else if (strcmp(name, "sha512") == 0) {
    return EVP_sha512();
  } else if (strcmp(name, "sha384") == 0) {
    return EVP_sha384();
  }

  return NULL;
}

static lcl_return_code c_crypto_random_bytes(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  lcl_int n;
  unsigned char *buf;
  char *hex;

  if (argc != 1) {
    lcl_set_error(interp, "Crypto::random_bytes: expected 1 argument (n)");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[0], &n) != LCL_OK || n < 0) {
    lcl_set_error(interp,
                  "Crypto::random_bytes: n must be a non-negative integer");
    return LCL_RC_ERR;
  }

  if (n > 1048576) {
    lcl_set_error(interp, "Crypto::random_bytes: n must be <= 1048576");
    return LCL_RC_ERR;
  }

  buf = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);

  if (!buf) {
    lcl_set_error(interp, "Crypto::random_bytes: out of memory");
    return LCL_RC_ERR;
  }

  if (n > 0 && RAND_bytes(buf, (int)n) != 1) {
    free(buf);
    lcl_set_error(interp, "Crypto::random_bytes: RAND_bytes failed");
    return LCL_RC_ERR;
  }

  hex = bytes_to_hex(buf, (size_t)n);
  free(buf);

  if (!hex) {
    lcl_set_error(interp, "Crypto::random_bytes: out of memory");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(hex);
  free(hex);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code c_crypto_sha256(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  const char *data;
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_MD_CTX *ctx;
  char *hex;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ctx = EVP_MD_CTX_new();

  if (!ctx) {
    return LCL_RC_ERR;
  }

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
      EVP_DigestUpdate(ctx, data, strlen(data)) != 1 ||
      EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);

    return LCL_RC_ERR;
  }

  EVP_MD_CTX_free(ctx);

  hex = bytes_to_hex(hash, hash_len);

  if (!hex) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(hex);
  free(hex);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code c_crypto_sha512(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  const char *data;
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_MD_CTX *ctx;
  char *hex;

  if (argc != 1) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ctx = EVP_MD_CTX_new();

  if (!ctx) {
    return LCL_RC_ERR;
  }

  if (EVP_DigestInit_ex(ctx, EVP_sha512(), NULL) != 1 ||
      EVP_DigestUpdate(ctx, data, strlen(data)) != 1 ||
      EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);

    return LCL_RC_ERR;
  }

  EVP_MD_CTX_free(ctx);

  hex = bytes_to_hex(hash, hash_len);

  if (!hex) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(hex);
  free(hex);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code c_crypto_hmac(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  const char *key;
  const char *data;
  const char *algo;
  const EVP_MD *md;
  unsigned char hmac_result[EVP_MAX_MD_SIZE];
  unsigned int hmac_len = 0;
  char *hex;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &key) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[2], &algo) != LCL_OK) {
    return LCL_RC_ERR;
  }

  md = get_md_by_name(algo);

  if (!md) {
    return LCL_RC_ERR;
  }

  if (!HMAC(md, key, (int)strlen(key), (const unsigned char *)data,
            strlen(data), hmac_result, &hmac_len)) {
    return LCL_RC_ERR;
  }

  hex = bytes_to_hex(hmac_result, hmac_len);

  if (!hex) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(hex);
  free(hex);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static lcl_return_code c_crypto_sign_rsa_pss(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  const char *key_pem;
  const char *data;
  const char *algo;
  const EVP_MD *md;
  BIO *bio = NULL;
  EVP_PKEY *pkey = NULL;
  EVP_MD_CTX *md_ctx = NULL;
  EVP_PKEY_CTX *pkey_ctx = NULL;
  unsigned char *sig = NULL;
  size_t sig_len = 0;
  char *hex = NULL;
  lcl_return_code result = LCL_RC_ERR;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &key_pem) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[2], &algo) != LCL_OK) {
    return LCL_RC_ERR;
  }

  md = get_md_by_name(algo);

  if (!md) {
    goto cleanup;
  }

  bio = BIO_new_mem_buf(key_pem, -1);

  if (!bio) {
    goto cleanup;
  }

  pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);

  if (!pkey) {
    goto cleanup;
  }

  if (EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA) {
    goto cleanup;
  }

  md_ctx = EVP_MD_CTX_new();

  if (!md_ctx) {
    goto cleanup;
  }

  if (EVP_DigestSignInit(md_ctx, &pkey_ctx, md, NULL, pkey) != 1) {
    goto cleanup;
  }

  if (EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING) != 1) {
    goto cleanup;
  }

  if (EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, RSA_PSS_SALTLEN_DIGEST) != 1) {
    goto cleanup;
  }

  if (EVP_DigestSignUpdate(md_ctx, data, strlen(data)) != 1) {
    goto cleanup;
  }

  if (EVP_DigestSignFinal(md_ctx, NULL, &sig_len) != 1) {
    goto cleanup;
  }

  sig = (unsigned char *)malloc(sig_len);

  if (!sig) {
    goto cleanup;
  }

  if (EVP_DigestSignFinal(md_ctx, sig, &sig_len) != 1) {
    goto cleanup;
  }

  hex = bytes_to_hex(sig, sig_len);

  if (!hex) {
    goto cleanup;
  }

  *out = lcl_string_new(hex);

  if (*out) {
    result = LCL_RC_OK;
  }

cleanup:
  free(hex);
  free(sig);
  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_free(pkey);
  BIO_free(bio);

  return result;
}

static lcl_return_code c_crypto_sign_ecdsa(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  const char *key_pem;
  const char *data;
  const char *curve;
  const EVP_MD *md;
  BIO *bio = NULL;
  EVP_PKEY *pkey = NULL;
  EVP_MD_CTX *md_ctx = NULL;
  unsigned char *sig = NULL;
  size_t sig_len = 0;
  char *hex = NULL;
  lcl_return_code result = LCL_RC_ERR;

  if (argc != 3) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &key_pem) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[1], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[2], &curve) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (strcmp(curve, "p256") == 0) {
    md = EVP_sha256();
  } else if (strcmp(curve, "p384") == 0) {
    md = EVP_sha384();
  } else if (strcmp(curve, "p521") == 0) {
    md = EVP_sha512();
  } else {
    return LCL_RC_ERR;
  }

  bio = BIO_new_mem_buf(key_pem, -1);

  if (!bio) {
    goto cleanup;
  }

  pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);

  if (!pkey) {
    goto cleanup;
  }

  if (EVP_PKEY_base_id(pkey) != EVP_PKEY_EC) {
    goto cleanup;
  }

  md_ctx = EVP_MD_CTX_new();

  if (!md_ctx) {
    goto cleanup;
  }

  if (EVP_DigestSignInit(md_ctx, NULL, md, NULL, pkey) != 1) {
    goto cleanup;
  }

  if (EVP_DigestSignUpdate(md_ctx, data, strlen(data)) != 1) {
    goto cleanup;
  }

  if (EVP_DigestSignFinal(md_ctx, NULL, &sig_len) != 1) {
    goto cleanup;
  }

  sig = (unsigned char *)malloc(sig_len);

  if (!sig) {
    goto cleanup;
  }

  if (EVP_DigestSignFinal(md_ctx, sig, &sig_len) != 1) {
    goto cleanup;
  }

  hex = bytes_to_hex(sig, sig_len);

  if (!hex) {
    goto cleanup;
  }

  *out = lcl_string_new(hex);
  if (*out) {
    result = LCL_RC_OK;
  }

cleanup:
  free(hex);
  free(sig);
  EVP_MD_CTX_free(md_ctx);
  EVP_PKEY_free(pkey);
  BIO_free(bio);

  return result;
}

static lcl_return_code c_crypto_base64_encode(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  const char *data;
  size_t data_len;
  unsigned char *buf;
  int enc_len;

  if (argc != 1) {
    lcl_set_error(interp,
                  "Crypto::base64_encode requires exactly one argument");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  data_len = strlen(data);

  if (data_len > (size_t)INT_MAX / 2) {
    lcl_set_error(interp, "Crypto::base64_encode: input too large");
    return LCL_RC_ERR;
  }

  buf = (unsigned char *)malloc(4 * ((data_len + 2) / 3) + 1);

  if (!buf) {
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  enc_len = EVP_EncodeBlock(buf, (const unsigned char *)data, (int)data_len);

  if (enc_len < 0) {
    free(buf);
    lcl_set_error(interp, "Crypto::base64_encode failed");
    return LCL_RC_ERR;
  }

  buf[enc_len] = '\0';
  *out = lcl_string_new((const char *)buf);
  free(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* Strict standard base64: the alphabet, a length that is a multiple
 * of four, and `=` only as the last one or two characters. Returns
 * the number of padding characters, or -1 if `s` is not base64. */
static int base64_check(const char *s, size_t n) {
  size_t i;
  int pad = 0;

  if (n % 4 != 0) {
    return -1;
  }

  for (i = 0; i < n; i++) {
    char c = s[i];

    if (c == '=') {
      if (i + 2 < n) {
        return -1;
      }

      pad++;
    } else if (pad > 0) {
      return -1;
    } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '+' || c == '/')) {
      return -1;
    }
  }

  return pad;
}

static lcl_return_code c_crypto_base64_decode(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  const char *data;
  size_t data_len;
  unsigned char *decoded;
  int pad;
  int dec_len;

  if (argc != 1) {
    lcl_set_error(interp,
                  "Crypto::base64_decode requires exactly one argument");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_cstring(interp, argv[0], &data) != LCL_OK) {
    return LCL_RC_ERR;
  }

  data_len = strlen(data);

  if (data_len > (size_t)INT_MAX) {
    lcl_set_error(interp, "Crypto::base64_decode: input too large");
    return LCL_RC_ERR;
  }

  pad = base64_check(data, data_len);

  if (pad < 0) {
    lcl_set_error(interp, "Crypto::base64_decode: invalid base64 input");
    return LCL_RC_ERR;
  }

  decoded = (unsigned char *)malloc(data_len / 4 * 3 + 1);

  if (!decoded) {
    lcl_set_error(interp, "out of memory");
    return LCL_RC_ERR;
  }

  dec_len = data_len == 0
                ? 0
                : EVP_DecodeBlock(decoded, (const unsigned char *)data,
                                  (int)data_len);

  if (dec_len < 0) {
    free(decoded);
    lcl_set_error(interp, "Crypto::base64_decode: invalid base64 input");
    return LCL_RC_ERR;
  }

  /* EVP_DecodeBlock counts padding as decoded bytes */
  dec_len -= pad;
  decoded[dec_len] = '\0';
  *out = lcl_string_new((const char *)decoded);
  free(decoded);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

void lcl_register_crypto(lcl_interp *interp) {
  lcl_value *crypto_ns = lcl_ns_new(CRYPTO_NS);
  lcl_define_take(interp, CRYPTO_NS, crypto_ns);

  lcl_ns_def_take(
      crypto_ns, "random_bytes",
      lcl_c_proc_new("Crypto::random_bytes", c_crypto_random_bytes));
  lcl_ns_def_take(crypto_ns, "sha256",
                  lcl_c_proc_new("Crypto::sha256", c_crypto_sha256));
  lcl_ns_def_take(crypto_ns, "sha512",
                  lcl_c_proc_new("Crypto::sha512", c_crypto_sha512));
  lcl_ns_def_take(crypto_ns, "hmac",
                  lcl_c_proc_new("Crypto::hmac", c_crypto_hmac));
  lcl_ns_def_take(
      crypto_ns, "sign_rsa_pss",
      lcl_c_proc_new("Crypto::sign_rsa_pss", c_crypto_sign_rsa_pss));
  lcl_ns_def_take(crypto_ns, "sign_ecdsa",
                  lcl_c_proc_new("Crypto::sign_ecdsa", c_crypto_sign_ecdsa));
  lcl_ns_def_take(
      crypto_ns, "base64_encode",
      lcl_c_proc_new("Crypto::base64_encode", c_crypto_base64_encode));
  lcl_ns_def_take(
      crypto_ns, "base64_decode",
      lcl_c_proc_new("Crypto::base64_decode", c_crypto_base64_decode));
}
