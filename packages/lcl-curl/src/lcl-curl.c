#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include <lcl.h>
#include <string.h>

#define CURL_CONTEXT_TYPE "curl_context"
#define CURL_NS "curl"

struct curl_context {
  CURL *curl;
  lcl_interp *interp;
  struct curl_slist *headers;
  lcl_value *write_callback;
};

struct curl_context *curl_context_new(void) {
  CURL *c = curl_easy_init();
  struct curl_context *ctx = NULL;

  if (!c) return NULL;

  ctx = calloc(1, sizeof(*ctx));

  if (!ctx) {
    curl_easy_cleanup(c);
    return NULL;
  }

  ctx->curl = c;
  ctx->headers = NULL;
  ctx->interp = NULL;
  ctx->write_callback = NULL;

  return ctx;
}

void curl_context_free(struct curl_context *ctx) {
  if (!ctx) return;

  if (ctx->curl) {
    curl_easy_cleanup(ctx->curl);
  }

  free(ctx);
}

int c_curl_new(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  struct curl_context *ctx;
  lcl_value *c;
  (void)interp;
  (void)argv;

  if (argc > 0) return LCL_RC_ERR;

  ctx = curl_context_new();

  if (!ctx) {
    return LCL_RC_ERR;
  }

  ctx->interp = interp;

  c = lcl_opaque_new(ctx, CURL_CONTEXT_TYPE, (lcl_finalizer)curl_context_free);

  if (!c) {
    lcl_ref_dec(c);

    return LCL_RC_ERR;
  }

  *out = c;

  return LCL_RC_OK;
}

int c_curl_init(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  (void)argc;
  (void)argv;
  (void)interp;
  (void)out;

  if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

int c_curl_reset(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  struct curl_context *ctx;
  (void)interp;
  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  curl_easy_reset(ctx->curl);

  return LCL_RC_OK;
}

int c_curl_set_verb(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  struct curl_context *ctx;
  const char *verb;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  verb = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_CUSTOMREQUEST, verb);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_header(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  struct curl_context *ctx;
  const char *header;
  int i;

  int rc;

  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc; i++) {
    header = lcl_value_to_string(argv[i]);
    printf("header = %s\n", header);
    ctx->headers = curl_slist_append(ctx->headers, header);
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->headers);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_body(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *body;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  body = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_POSTFIELDS, body);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_url(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  struct curl_context *ctx;
  const char *url;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  url = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_URL, url);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

/*
 * Connection Settings
 */
int c_curl_set_option_accept_timeout_ms(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long timeout_ms;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &timeout_ms) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_ACCEPTTIMEOUT_MS, timeout_ms);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_connection_timeout_ms(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long timeout_ms;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &timeout_ms) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_interface(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *interface;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  interface = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_INTERFACE, interface);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_low_speed_limit(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long low_spl;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &low_spl) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_LOW_SPEED_LIMIT, low_spl);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_low_speed_time(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long low_spt;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &low_spt) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_LOW_SPEED_TIME, low_spt);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_tcp_keep_alive(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long tcp_ka;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &tcp_ka) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_TCP_KEEPALIVE, tcp_ka);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_tcp_keep_idle(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long tcp_ki;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &tcp_ki) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_TCP_KEEPIDLE, tcp_ki);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_tcp_keep_intvl(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long tcp_kintvl;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &tcp_kintvl) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_TCP_KEEPINTVL, tcp_kintvl);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

/*
 * CURL Context Setters
 */
int c_curl_set_option_accept_encoding(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
 struct curl_context *ctx;
 const char *accept_encoding;
 int rc;
 (void)interp;
 (void)out;

 if (argc < 2) {
   return LCL_RC_ERR;
 }

 if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
   return LCL_RC_ERR;
 }

 accept_encoding = lcl_value_to_string(argv[1]);
 rc = curl_easy_setopt(ctx->curl, CURLOPT_ACCEPT_ENCODING, accept_encoding);

 if (rc != CURLE_OK) return LCL_RC_ERR;

 return LCL_RC_OK;
}

int c_curl_set_option_http_version(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long http_version;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &http_version) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_HTTP_VERSION, http_version);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

/*
 * TLS/mTLS
 */
int c_curl_set_option_ssl_verify_peer(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long verify_peer;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &verify_peer) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSL_VERIFYPEER, verify_peer);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ssl_verify_host(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long verify_host;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &verify_host) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSL_VERIFYHOST, verify_host);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ca_info(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct curl_context *ctx;
 const char *ca_info;
 int rc;
 (void)interp;
 (void)out;

 if (argc < 2) {
   return LCL_RC_ERR;
 }

 if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
   return LCL_RC_ERR;
 }

 ca_info = lcl_value_to_string(argv[1]);
 rc = curl_easy_setopt(ctx->curl, CURLOPT_CAINFO, ca_info);

 if (rc != CURLE_OK) return LCL_RC_ERR;

 return LCL_RC_OK;
}

int c_curl_set_option_ca_path(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
 struct curl_context *ctx;
 const char *ca_path;
 int rc;
 (void)interp;
 (void)out;

 if (argc < 2) {
   return LCL_RC_ERR;
 }

 if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
   return LCL_RC_ERR;
 }

 ca_path = lcl_value_to_string(argv[1]);
 rc = curl_easy_setopt(ctx->curl, CURLOPT_CAPATH, ca_path);

 if (rc != CURLE_OK) return LCL_RC_ERR;

 return LCL_RC_OK;
}

int c_curl_set_option_ssl_cert(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_cert;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_cert = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSLCERT, ssl_cert);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ssl_cert_type(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_cert_type;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
   return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_cert_type = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSLCERTTYPE, ssl_cert_type);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ssl_key(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_key;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
   return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_key = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSLKEY, ssl_key);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ssl_key_type(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_key_type;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
   return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_key_type = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSLKEYTYPE, ssl_key_type);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_key_password(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_key_password;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
   return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_key_password = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSLKEYPASSWD, ssl_key_password);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ssl_version(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_version;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_version = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSLVERSION, ssl_version);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_ssl_cipher_list(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *ssl_cipher_list;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  ssl_cipher_list = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_SSL_CIPHER_LIST, ssl_cipher_list);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

int c_curl_set_option_tls13_ciphers(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  const char *tls13_ciphers;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  tls13_ciphers = lcl_value_to_string(argv[1]);
  rc = curl_easy_setopt(ctx->curl, CURLOPT_TLS13_CIPHERS, tls13_ciphers);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}

// Proxies
int c_curl_set_option_proxy(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_port(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_type(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_username(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_password(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_no_proxy(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_ssl_verify_peer(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_ssl_verify_host(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_ca_info(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_ca_path(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_proxy_ssl_version(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);

// Redirects
int c_curl_set_option_follow_location(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_max_redirects(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_post_redirect(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);

// Auth
int c_curl_set_option_curl_context_auth(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_username(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_password(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_xoauth2_bearer(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);

// Observability
int c_curl_set_option_verbose(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_debug_function(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_header(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_header_function(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);

// Power users
int c_curl_set_option_resolve(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_doh_url(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_dns_servers(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_fresh_connect(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_forbid_reuse(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_expect_100_timeout_ms(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_max_recv_speed_large(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_max_send_speed_large(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_cookie_file(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_set_option_cookie_jar(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);

// Observability
int c_curl_set_option_verbose(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long verbose;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &verbose) != LCL_OK) {
    return LCL_RC_ERR;
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_VERBOSE, verbose);

  if (rc != CURLE_OK) return LCL_RC_ERR;

  return LCL_RC_OK;
}


/*
 * Info from last request
 */
int c_curl_get_info_response_code(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  long status;
  (void)interp;
  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  /* TODO: guard for failure? can this fail? */
  curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &status);

  *out = lcl_int_new(status);

  return LCL_RC_OK;
}

int c_curl_get_info_content_type(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);

int c_curl_get_info_effective_url(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);
int c_curl_get_info_total_time(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out);


int c_curl_perform(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  struct curl_context *ctx;
  CURLcode result;
  (void)interp;
  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = curl_easy_perform(ctx->curl);

  if (result == CURLE_OK) {
    curl_slist_free_all(ctx->headers);

    return LCL_RC_OK;
  }

  curl_slist_free_all(ctx->headers);

  return LCL_RC_ERR;
}

/*
 * Callbacks
 */
size_t curl_write_wrapper(char *contents, size_t size, size_t nmemb, void *userdata) {
  size_t realsize = size * nmemb;
  struct curl_context *ctx = (struct curl_context *)userdata;
  lcl_value *result = NULL;
  lcl_value *arg = lcl_string_new(contents);

  lcl_call_proc(ctx->interp, ctx->write_callback, 1, &arg, &result);

  lcl_ref_dec(result);
  lcl_ref_dec(arg);

  return realsize;
}

int c_curl_set_write_callback(lcl_interp *interp, int argc, lcl_value **argv, lcl_value **out) {
  struct curl_context *ctx;
  lcl_value *callback_proc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void**)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  callback_proc = argv[1];

  if (!lcl_is_callable(callback_proc)) {
    return LCL_RC_ERR;
  }

  ctx->write_callback = lcl_ref_inc(callback_proc);

  curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, curl_write_wrapper);
  curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, (void*)ctx);

  return LCL_RC_OK;
}

void lcl_register_curl(lcl_interp *interp) {
  lcl_value *curl_ns = lcl_ns_new(CURL_NS);
  lcl_define_take(interp, CURL_NS, curl_ns);

  lcl_ns_def(curl_ns, "new",   lcl_c_proc_new("curl::new", c_curl_new));
  lcl_ns_def(curl_ns, "init",  lcl_c_proc_new("curl::init", c_curl_init));
  lcl_ns_def(curl_ns, "reset", lcl_c_proc_new("curl::reset", c_curl_reset));

  /* Main request attributes */
  lcl_ns_def(curl_ns, "set_verb",   lcl_c_proc_new("curl::set_verb", c_curl_set_verb));
  lcl_ns_def(curl_ns, "set_header", lcl_c_proc_new("curl::set_header", c_curl_set_header));
  lcl_ns_def(curl_ns, "set_body",   lcl_c_proc_new("curl::set_body", c_curl_set_body));
  lcl_ns_def(curl_ns, "set_url",    lcl_c_proc_new("curl::set_url", c_curl_set_url));

  /* Connection settings */
  lcl_ns_def(curl_ns, "set_accept_timeout_ms",     lcl_c_proc_new("curl::set_accept_timeout_ms", c_curl_set_option_accept_timeout_ms));
  lcl_ns_def(curl_ns, "set_connection_timeout_ms", lcl_c_proc_new("curl::set_connection_timeout_ms", c_curl_set_option_connection_timeout_ms));
  lcl_ns_def(curl_ns, "set_interface",             lcl_c_proc_new("curl::set_interface", c_curl_set_option_interface));
  lcl_ns_def(curl_ns, "set_low_speed_limit",       lcl_c_proc_new("curl::set_low_speed_limit", c_curl_set_option_low_speed_limit));
  lcl_ns_def(curl_ns, "set_low_speed_time",        lcl_c_proc_new("curl::set_low_speed_time", c_curl_set_option_low_speed_time));
  lcl_ns_def(curl_ns, "set_tcp_keep_alive",        lcl_c_proc_new("curl::set_tcp_keep_alive", c_curl_set_option_tcp_keep_alive));
  lcl_ns_def(curl_ns, "set_tcp_keep_idle",         lcl_c_proc_new("curl::set_tcp_keep_idle", c_curl_set_option_tcp_keep_idle));
  lcl_ns_def(curl_ns, "set_tcp_keep_intvl",        lcl_c_proc_new("curl::set_tcp_keep_intvl", c_curl_set_option_tcp_keep_intvl));

  /*
   * Context setters
   */
  lcl_ns_def(curl_ns, "set_accept_encoding", lcl_c_proc_new("curl::set_accept_encoding", c_curl_set_option_accept_encoding));
  lcl_ns_def(curl_ns, "set_http_version",    lcl_c_proc_new("curl::set_http_version", c_curl_set_option_http_version));

  /*
   * TLS/mTLS
   */


  /*
   * Observability
   */
  lcl_ns_def(curl_ns, "set_verbose", lcl_c_proc_new("curl::set_verbose", c_curl_set_option_verbose));

  /*
   * Callbacks
   */
  lcl_ns_def(curl_ns, "set_write_callback", lcl_c_proc_new("curl::set_write_callback", c_curl_set_write_callback));

  /* ship it */
  lcl_ns_def(curl_ns, "perform", lcl_c_proc_new("curl::perform", c_curl_perform));
}
