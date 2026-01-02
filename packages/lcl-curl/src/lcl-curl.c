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

  if (!c) {
    return NULL;
  }

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
  if (!ctx) {
    return;
  }

  if (ctx->curl) {
    curl_easy_cleanup(ctx->curl);
  }

  if (ctx->headers) {
    curl_slist_free_all(ctx->headers);
  }

  if (ctx->write_callback) {
    lcl_ref_dec(ctx->write_callback);
  }

  free(ctx);
}

int c_curl_new(lcl_interp *interp, int argc, lcl_value **argv,
               lcl_value **out) {
  struct curl_context *ctx;
  lcl_value *c;
  (void)interp;
  (void)argv;

  if (argc > 0) {
    return LCL_RC_ERR;
  }

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

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  curl_easy_reset(ctx->curl);

  return LCL_RC_OK;
}

/*
 * Helper macros for common option patterns
 */
#define CURL_STRING_OPTION(fn_name, curl_opt)                                  \
  int fn_name(lcl_interp *interp, int argc, lcl_value **argv,                  \
              lcl_value **out) {                                               \
    struct curl_context *ctx;                                                  \
    const char *val;                                                           \
    (void)interp;                                                              \
    (void)out;                                                                 \
    if (argc < 2)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK)   \
      return LCL_RC_ERR;                                                       \
    val = lcl_value_to_string(argv[1]);                                        \
    return (curl_easy_setopt(ctx->curl, curl_opt, val) == CURLE_OK)            \
               ? LCL_RC_OK                                                     \
               : LCL_RC_ERR;                                                   \
  }

#define CURL_LONG_OPTION(fn_name, curl_opt)                                    \
  int fn_name(lcl_interp *interp, int argc, lcl_value **argv,                  \
              lcl_value **out) {                                               \
    struct curl_context *ctx;                                                  \
    long val;                                                                  \
    (void)interp;                                                              \
    (void)out;                                                                 \
    if (argc < 2)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK)   \
      return LCL_RC_ERR;                                                       \
    if (lcl_value_to_int(argv[1], &val) != LCL_OK)                             \
      return LCL_RC_ERR;                                                       \
    return (curl_easy_setopt(ctx->curl, curl_opt, val) == CURLE_OK)            \
               ? LCL_RC_OK                                                     \
               : LCL_RC_ERR;                                                   \
  }

/*
 * Main request attributes
 */
CURL_STRING_OPTION(c_curl_set_verb, CURLOPT_CUSTOMREQUEST)
CURL_STRING_OPTION(c_curl_set_url, CURLOPT_URL)

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

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  for (i = 1; i < argc; i++) {
    header = lcl_value_to_string(argv[i]);
    printf("header = %s\n", header);
    ctx->headers = curl_slist_append(ctx->headers, header);
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->headers);

  if (rc != CURLE_OK) {
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

int c_curl_set_body(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  struct curl_context *ctx;
  const char *body;
  int rc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  body = lcl_value_to_string(argv[1]);

  /*
   * Use CURLOPT_COPYPOSTFIELDS instead of CURLOPT_POSTFIELDS.
   * POSTFIELDS does NOT copy the data - it just stores the pointer.
   * Since 'body' points to memory owned by the LCL value (argv[1]),
   * that memory may be freed before curl_easy_perform() is called,
   * resulting in garbage data being sent.
   * COPYPOSTFIELDS makes CURL copy the data internally.
   */
  rc = curl_easy_setopt(ctx->curl, CURLOPT_COPYPOSTFIELDS, body);

  if (rc != CURLE_OK) {
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/*
 * Connection Settings
 */
CURL_LONG_OPTION(c_curl_set_option_accept_timeout_ms, CURLOPT_ACCEPTTIMEOUT_MS)
CURL_LONG_OPTION(c_curl_set_option_connection_timeout_ms,
                 CURLOPT_CONNECTTIMEOUT_MS)
CURL_STRING_OPTION(c_curl_set_option_interface, CURLOPT_INTERFACE)
CURL_LONG_OPTION(c_curl_set_option_low_speed_limit, CURLOPT_LOW_SPEED_LIMIT)
CURL_LONG_OPTION(c_curl_set_option_low_speed_time, CURLOPT_LOW_SPEED_TIME)
CURL_LONG_OPTION(c_curl_set_option_tcp_keep_alive, CURLOPT_TCP_KEEPALIVE)
CURL_LONG_OPTION(c_curl_set_option_tcp_keep_idle, CURLOPT_TCP_KEEPIDLE)
CURL_LONG_OPTION(c_curl_set_option_tcp_keep_intvl, CURLOPT_TCP_KEEPINTVL)

/*
 * HTTP Settings
 */
CURL_STRING_OPTION(c_curl_set_option_accept_encoding, CURLOPT_ACCEPT_ENCODING)
CURL_LONG_OPTION(c_curl_set_option_http_version, CURLOPT_HTTP_VERSION)

/*
 * TLS/mTLS
 */
CURL_LONG_OPTION(c_curl_set_option_ssl_verify_peer, CURLOPT_SSL_VERIFYPEER)
CURL_LONG_OPTION(c_curl_set_option_ssl_verify_host, CURLOPT_SSL_VERIFYHOST)
CURL_STRING_OPTION(c_curl_set_option_ca_info, CURLOPT_CAINFO)
CURL_STRING_OPTION(c_curl_set_option_ca_path, CURLOPT_CAPATH)
CURL_STRING_OPTION(c_curl_set_option_ssl_cert, CURLOPT_SSLCERT)
CURL_STRING_OPTION(c_curl_set_option_ssl_cert_type, CURLOPT_SSLCERTTYPE)
CURL_STRING_OPTION(c_curl_set_option_ssl_key, CURLOPT_SSLKEY)
CURL_STRING_OPTION(c_curl_set_option_ssl_key_type, CURLOPT_SSLKEYTYPE)
CURL_STRING_OPTION(c_curl_set_option_key_password, CURLOPT_KEYPASSWD)
CURL_LONG_OPTION(c_curl_set_option_ssl_version, CURLOPT_SSLVERSION)
CURL_STRING_OPTION(c_curl_set_option_ssl_cipher_list, CURLOPT_SSL_CIPHER_LIST)
CURL_STRING_OPTION(c_curl_set_option_tls13_ciphers, CURLOPT_TLS13_CIPHERS)

/*
 * Proxy options
 */
CURL_STRING_OPTION(c_curl_set_option_proxy, CURLOPT_PROXY)
CURL_LONG_OPTION(c_curl_set_option_proxy_port, CURLOPT_PROXYPORT)
CURL_LONG_OPTION(c_curl_set_option_proxy_type, CURLOPT_PROXYTYPE)
CURL_STRING_OPTION(c_curl_set_option_proxy_username, CURLOPT_PROXYUSERNAME)
CURL_STRING_OPTION(c_curl_set_option_proxy_password, CURLOPT_PROXYPASSWORD)
CURL_STRING_OPTION(c_curl_set_option_no_proxy, CURLOPT_NOPROXY)
CURL_LONG_OPTION(c_curl_set_option_proxy_ssl_verify_peer,
                 CURLOPT_PROXY_SSL_VERIFYPEER)
CURL_LONG_OPTION(c_curl_set_option_proxy_ssl_verify_host,
                 CURLOPT_PROXY_SSL_VERIFYHOST)
CURL_STRING_OPTION(c_curl_set_option_proxy_ca_info, CURLOPT_PROXY_CAINFO)
CURL_STRING_OPTION(c_curl_set_option_proxy_ca_path, CURLOPT_PROXY_CAPATH)
CURL_LONG_OPTION(c_curl_set_option_proxy_ssl_version, CURLOPT_PROXY_SSLVERSION)

/*
 * Redirect options
 */
CURL_LONG_OPTION(c_curl_set_option_follow_location, CURLOPT_FOLLOWLOCATION)
CURL_LONG_OPTION(c_curl_set_option_max_redirects, CURLOPT_MAXREDIRS)
CURL_LONG_OPTION(c_curl_set_option_post_redirect, CURLOPT_POSTREDIR)

/*
 * Authentication options
 */
CURL_LONG_OPTION(c_curl_set_option_httpauth, CURLOPT_HTTPAUTH)
CURL_STRING_OPTION(c_curl_set_option_username, CURLOPT_USERNAME)
CURL_STRING_OPTION(c_curl_set_option_password, CURLOPT_PASSWORD)
CURL_STRING_OPTION(c_curl_set_option_xoauth2_bearer, CURLOPT_XOAUTH2_BEARER)

/*
 * Observability options
 */
CURL_LONG_OPTION(c_curl_set_option_verbose, CURLOPT_VERBOSE)
CURL_LONG_OPTION(c_curl_set_option_header, CURLOPT_HEADER)

/*
 * Power user options
 */
CURL_STRING_OPTION(c_curl_set_option_doh_url, CURLOPT_DOH_URL)
CURL_STRING_OPTION(c_curl_set_option_dns_servers, CURLOPT_DNS_SERVERS)
CURL_LONG_OPTION(c_curl_set_option_fresh_connect, CURLOPT_FRESH_CONNECT)
CURL_LONG_OPTION(c_curl_set_option_forbid_reuse, CURLOPT_FORBID_REUSE)
CURL_LONG_OPTION(c_curl_set_option_expect_100_timeout_ms,
                 CURLOPT_EXPECT_100_TIMEOUT_MS)
CURL_LONG_OPTION(c_curl_set_option_max_recv_speed_large,
                 CURLOPT_MAX_RECV_SPEED_LARGE)
CURL_LONG_OPTION(c_curl_set_option_max_send_speed_large,
                 CURLOPT_MAX_SEND_SPEED_LARGE)
CURL_STRING_OPTION(c_curl_set_option_cookie_file, CURLOPT_COOKIEFILE)
CURL_STRING_OPTION(c_curl_set_option_cookie_jar, CURLOPT_COOKIEJAR)
CURL_STRING_OPTION(c_curl_set_option_user_agent, CURLOPT_USERAGENT)
CURL_LONG_OPTION(c_curl_set_option_timeout_ms, CURLOPT_TIMEOUT_MS)

/*
 * Info from last request
 */
#define CURL_INFO_LONG(fn_name, curl_info)                                     \
  int fn_name(lcl_interp *interp, int argc, lcl_value **argv,                  \
              lcl_value **out) {                                               \
    struct curl_context *ctx;                                                  \
    long val;                                                                  \
    (void)interp;                                                              \
    if (argc < 1)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK)   \
      return LCL_RC_ERR;                                                       \
    if (curl_easy_getinfo(ctx->curl, curl_info, &val) != CURLE_OK)             \
      return LCL_RC_ERR;                                                       \
    *out = lcl_int_new(val);                                                   \
    return LCL_RC_OK;                                                          \
  }

#define CURL_INFO_STRING(fn_name, curl_info)                                   \
  int fn_name(lcl_interp *interp, int argc, lcl_value **argv,                  \
              lcl_value **out) {                                               \
    struct curl_context *ctx;                                                  \
    char *val = NULL;                                                          \
    (void)interp;                                                              \
    if (argc < 1)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK)   \
      return LCL_RC_ERR;                                                       \
    if (curl_easy_getinfo(ctx->curl, curl_info, &val) != CURLE_OK)             \
      return LCL_RC_ERR;                                                       \
    *out = lcl_string_new(val ? val : "");                                     \
    return LCL_RC_OK;                                                          \
  }

#define CURL_INFO_DOUBLE(fn_name, curl_info)                                   \
  int fn_name(lcl_interp *interp, int argc, lcl_value **argv,                  \
              lcl_value **out) {                                               \
    struct curl_context *ctx;                                                  \
    double val;                                                                \
    (void)interp;                                                              \
    if (argc < 1)                                                              \
      return LCL_RC_ERR;                                                       \
    if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK)   \
      return LCL_RC_ERR;                                                       \
    if (curl_easy_getinfo(ctx->curl, curl_info, &val) != CURLE_OK)             \
      return LCL_RC_ERR;                                                       \
    *out = lcl_float_new((float)val);                                          \
    return LCL_RC_OK;                                                          \
  }

CURL_INFO_LONG(c_curl_get_info_response_code, CURLINFO_RESPONSE_CODE)
CURL_INFO_STRING(c_curl_get_info_content_type, CURLINFO_CONTENT_TYPE)
CURL_INFO_STRING(c_curl_get_info_effective_url, CURLINFO_EFFECTIVE_URL)
CURL_INFO_DOUBLE(c_curl_get_info_total_time, CURLINFO_TOTAL_TIME)
CURL_INFO_LONG(c_curl_get_info_header_size, CURLINFO_HEADER_SIZE)
CURL_INFO_LONG(c_curl_get_info_request_size, CURLINFO_REQUEST_SIZE)
CURL_INFO_LONG(c_curl_get_info_num_connects, CURLINFO_NUM_CONNECTS)
CURL_INFO_STRING(c_curl_get_info_primary_ip, CURLINFO_PRIMARY_IP)
CURL_INFO_LONG(c_curl_get_info_primary_port, CURLINFO_PRIMARY_PORT)

int c_curl_perform(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  struct curl_context *ctx;
  CURLcode result;
  (void)interp;
  (void)out;

  if (argc < 1) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  result = curl_easy_perform(ctx->curl);

  /* Free headers after perform (they were already sent) */
  if (ctx->headers) {
    curl_slist_free_all(ctx->headers);
    ctx->headers = NULL;
  }

  return (result == CURLE_OK) ? LCL_RC_OK : LCL_RC_ERR;
}

/*
 * Callbacks
 */
size_t curl_write_wrapper(char *contents, size_t size, size_t nmemb,
                          void *userdata) {
  size_t realsize = size * nmemb;
  struct curl_context *ctx = (struct curl_context *)userdata;
  lcl_value *result = NULL;
  lcl_value *arg;
  char *buf;

  /*
   * CURL's write callback data is NOT null-terminated.
   * We must copy it to a null-terminated buffer before creating an LCL string.
   */
  buf = malloc(realsize + 1);
  if (!buf) {
    return 0;
  }
  memcpy(buf, contents, realsize);
  buf[realsize] = '\0';

  arg = lcl_string_new(buf);
  free(buf);

  lcl_call_proc(ctx->interp, ctx->write_callback, 1, &arg, &result);

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_ref_dec(arg);

  return realsize;
}

int c_curl_set_write_callback(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct curl_context *ctx;
  lcl_value *callback_proc;
  (void)interp;
  (void)out;

  if (argc < 2) {
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], CURL_CONTEXT_TYPE, (void **)&ctx) != LCL_OK) {
    return LCL_RC_ERR;
  }

  callback_proc = argv[1];

  if (!lcl_is_callable(callback_proc)) {
    return LCL_RC_ERR;
  }

  ctx->write_callback = lcl_ref_inc(callback_proc);

  curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, curl_write_wrapper);
  curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, (void *)ctx);

  return LCL_RC_OK;
}

void lcl_register_curl(lcl_interp *interp) {
  lcl_value *curl_ns = lcl_ns_new(CURL_NS);
  lcl_define_take(interp, CURL_NS, curl_ns);

  /* Lifecycle */
  lcl_ns_def(curl_ns, "new", lcl_c_proc_new("curl::new", c_curl_new));
  lcl_ns_def(curl_ns, "init", lcl_c_proc_new("curl::init", c_curl_init));
  lcl_ns_def(curl_ns, "reset", lcl_c_proc_new("curl::reset", c_curl_reset));
  lcl_ns_def(curl_ns, "perform",
             lcl_c_proc_new("curl::perform", c_curl_perform));

  /* Main request attributes */
  lcl_ns_def(curl_ns, "set_url",
             lcl_c_proc_new("curl::set_url", c_curl_set_url));
  lcl_ns_def(curl_ns, "set_verb",
             lcl_c_proc_new("curl::set_verb", c_curl_set_verb));
  lcl_ns_def(curl_ns, "set_header",
             lcl_c_proc_new("curl::set_header", c_curl_set_header));
  lcl_ns_def(curl_ns, "set_body",
             lcl_c_proc_new("curl::set_body", c_curl_set_body));
  lcl_ns_def(
      curl_ns, "set_user_agent",
      lcl_c_proc_new("curl::set_user_agent", c_curl_set_option_user_agent));

  /* Timeouts and connection settings */
  lcl_ns_def(
      curl_ns, "set_timeout_ms",
      lcl_c_proc_new("curl::set_timeout_ms", c_curl_set_option_timeout_ms));
  lcl_ns_def(curl_ns, "set_accept_timeout_ms",
             lcl_c_proc_new("curl::set_accept_timeout_ms",
                            c_curl_set_option_accept_timeout_ms));
  lcl_ns_def(curl_ns, "set_connection_timeout_ms",
             lcl_c_proc_new("curl::set_connection_timeout_ms",
                            c_curl_set_option_connection_timeout_ms));
  lcl_ns_def(curl_ns, "set_expect_100_timeout_ms",
             lcl_c_proc_new("curl::set_expect_100_timeout_ms",
                            c_curl_set_option_expect_100_timeout_ms));
  lcl_ns_def(
      curl_ns, "set_interface",
      lcl_c_proc_new("curl::set_interface", c_curl_set_option_interface));
  lcl_ns_def(curl_ns, "set_low_speed_limit",
             lcl_c_proc_new("curl::set_low_speed_limit",
                            c_curl_set_option_low_speed_limit));
  lcl_ns_def(curl_ns, "set_low_speed_time",
             lcl_c_proc_new("curl::set_low_speed_time",
                            c_curl_set_option_low_speed_time));
  lcl_ns_def(curl_ns, "set_tcp_keep_alive",
             lcl_c_proc_new("curl::set_tcp_keep_alive",
                            c_curl_set_option_tcp_keep_alive));
  lcl_ns_def(curl_ns, "set_tcp_keep_idle",
             lcl_c_proc_new("curl::set_tcp_keep_idle",
                            c_curl_set_option_tcp_keep_idle));
  lcl_ns_def(curl_ns, "set_tcp_keep_intvl",
             lcl_c_proc_new("curl::set_tcp_keep_intvl",
                            c_curl_set_option_tcp_keep_intvl));

  /* HTTP settings */
  lcl_ns_def(curl_ns, "set_accept_encoding",
             lcl_c_proc_new("curl::set_accept_encoding",
                            c_curl_set_option_accept_encoding));
  lcl_ns_def(
      curl_ns, "set_http_version",
      lcl_c_proc_new("curl::set_http_version", c_curl_set_option_http_version));

  /* Redirects */
  lcl_ns_def(curl_ns, "set_follow_location",
             lcl_c_proc_new("curl::set_follow_location",
                            c_curl_set_option_follow_location));
  lcl_ns_def(curl_ns, "set_max_redirects",
             lcl_c_proc_new("curl::set_max_redirects",
                            c_curl_set_option_max_redirects));
  lcl_ns_def(curl_ns, "set_post_redirect",
             lcl_c_proc_new("curl::set_post_redirect",
                            c_curl_set_option_post_redirect));

  /* Authentication */
  lcl_ns_def(curl_ns, "set_httpauth",
             lcl_c_proc_new("curl::set_httpauth", c_curl_set_option_httpauth));
  lcl_ns_def(curl_ns, "set_username",
             lcl_c_proc_new("curl::set_username", c_curl_set_option_username));
  lcl_ns_def(curl_ns, "set_password",
             lcl_c_proc_new("curl::set_password", c_curl_set_option_password));
  lcl_ns_def(curl_ns, "set_xoauth2_bearer",
             lcl_c_proc_new("curl::set_xoauth2_bearer",
                            c_curl_set_option_xoauth2_bearer));

  /* TLS/SSL */
  lcl_ns_def(curl_ns, "set_ssl_verify_peer",
             lcl_c_proc_new("curl::set_ssl_verify_peer",
                            c_curl_set_option_ssl_verify_peer));
  lcl_ns_def(curl_ns, "set_ssl_verify_host",
             lcl_c_proc_new("curl::set_ssl_verify_host",
                            c_curl_set_option_ssl_verify_host));
  lcl_ns_def(curl_ns, "set_ca_info",
             lcl_c_proc_new("curl::set_ca_info", c_curl_set_option_ca_info));
  lcl_ns_def(curl_ns, "set_ca_path",
             lcl_c_proc_new("curl::set_ca_path", c_curl_set_option_ca_path));
  lcl_ns_def(curl_ns, "set_ssl_cert",
             lcl_c_proc_new("curl::set_ssl_cert", c_curl_set_option_ssl_cert));
  lcl_ns_def(curl_ns, "set_ssl_cert_type",
             lcl_c_proc_new("curl::set_ssl_cert_type",
                            c_curl_set_option_ssl_cert_type));
  lcl_ns_def(curl_ns, "set_ssl_key",
             lcl_c_proc_new("curl::set_ssl_key", c_curl_set_option_ssl_key));
  lcl_ns_def(
      curl_ns, "set_ssl_key_type",
      lcl_c_proc_new("curl::set_ssl_key_type", c_curl_set_option_ssl_key_type));
  lcl_ns_def(
      curl_ns, "set_key_password",
      lcl_c_proc_new("curl::set_key_password", c_curl_set_option_key_password));
  lcl_ns_def(
      curl_ns, "set_ssl_version",
      lcl_c_proc_new("curl::set_ssl_version", c_curl_set_option_ssl_version));
  lcl_ns_def(curl_ns, "set_ssl_cipher_list",
             lcl_c_proc_new("curl::set_ssl_cipher_list",
                            c_curl_set_option_ssl_cipher_list));
  lcl_ns_def(curl_ns, "set_tls13_ciphers",
             lcl_c_proc_new("curl::set_tls13_ciphers",
                            c_curl_set_option_tls13_ciphers));

  /* Proxy */
  lcl_ns_def(curl_ns, "set_proxy",
             lcl_c_proc_new("curl::set_proxy", c_curl_set_option_proxy));
  lcl_ns_def(
      curl_ns, "set_proxy_port",
      lcl_c_proc_new("curl::set_proxy_port", c_curl_set_option_proxy_port));
  lcl_ns_def(
      curl_ns, "set_proxy_type",
      lcl_c_proc_new("curl::set_proxy_type", c_curl_set_option_proxy_type));
  lcl_ns_def(curl_ns, "set_proxy_username",
             lcl_c_proc_new("curl::set_proxy_username",
                            c_curl_set_option_proxy_username));
  lcl_ns_def(curl_ns, "set_proxy_password",
             lcl_c_proc_new("curl::set_proxy_password",
                            c_curl_set_option_proxy_password));
  lcl_ns_def(curl_ns, "set_no_proxy",
             lcl_c_proc_new("curl::set_no_proxy", c_curl_set_option_no_proxy));
  lcl_ns_def(curl_ns, "set_proxy_ssl_verify_peer",
             lcl_c_proc_new("curl::set_proxy_ssl_verify_peer",
                            c_curl_set_option_proxy_ssl_verify_peer));
  lcl_ns_def(curl_ns, "set_proxy_ssl_verify_host",
             lcl_c_proc_new("curl::set_proxy_ssl_verify_host",
                            c_curl_set_option_proxy_ssl_verify_host));
  lcl_ns_def(curl_ns, "set_proxy_ca_info",
             lcl_c_proc_new("curl::set_proxy_ca_info",
                            c_curl_set_option_proxy_ca_info));
  lcl_ns_def(curl_ns, "set_proxy_ca_path",
             lcl_c_proc_new("curl::set_proxy_ca_path",
                            c_curl_set_option_proxy_ca_path));
  lcl_ns_def(curl_ns, "set_proxy_ssl_version",
             lcl_c_proc_new("curl::set_proxy_ssl_version",
                            c_curl_set_option_proxy_ssl_version));

  /* DNS and connection reuse */
  lcl_ns_def(curl_ns, "set_doh_url",
             lcl_c_proc_new("curl::set_doh_url", c_curl_set_option_doh_url));
  lcl_ns_def(
      curl_ns, "set_dns_servers",
      lcl_c_proc_new("curl::set_dns_servers", c_curl_set_option_dns_servers));
  lcl_ns_def(curl_ns, "set_fresh_connect",
             lcl_c_proc_new("curl::set_fresh_connect",
                            c_curl_set_option_fresh_connect));
  lcl_ns_def(
      curl_ns, "set_forbid_reuse",
      lcl_c_proc_new("curl::set_forbid_reuse", c_curl_set_option_forbid_reuse));

  /* Speed limits */
  lcl_ns_def(curl_ns, "set_max_recv_speed",
             lcl_c_proc_new("curl::set_max_recv_speed",
                            c_curl_set_option_max_recv_speed_large));
  lcl_ns_def(curl_ns, "set_max_send_speed",
             lcl_c_proc_new("curl::set_max_send_speed",
                            c_curl_set_option_max_send_speed_large));

  /* Cookies */
  lcl_ns_def(
      curl_ns, "set_cookie_file",
      lcl_c_proc_new("curl::set_cookie_file", c_curl_set_option_cookie_file));
  lcl_ns_def(
      curl_ns, "set_cookie_jar",
      lcl_c_proc_new("curl::set_cookie_jar", c_curl_set_option_cookie_jar));

  /* Observability */
  lcl_ns_def(curl_ns, "set_verbose",
             lcl_c_proc_new("curl::set_verbose", c_curl_set_option_verbose));
  lcl_ns_def(
      curl_ns, "set_include_header",
      lcl_c_proc_new("curl::set_include_header", c_curl_set_option_header));

  /* Callbacks */
  lcl_ns_def(
      curl_ns, "set_write_callback",
      lcl_c_proc_new("curl::set_write_callback", c_curl_set_write_callback));

  /* Response info getters */
  lcl_ns_def(
      curl_ns, "get_response_code",
      lcl_c_proc_new("curl::get_response_code", c_curl_get_info_response_code));
  lcl_ns_def(
      curl_ns, "get_content_type",
      lcl_c_proc_new("curl::get_content_type", c_curl_get_info_content_type));
  lcl_ns_def(
      curl_ns, "get_effective_url",
      lcl_c_proc_new("curl::get_effective_url", c_curl_get_info_effective_url));
  lcl_ns_def(
      curl_ns, "get_total_time",
      lcl_c_proc_new("curl::get_total_time", c_curl_get_info_total_time));
  lcl_ns_def(
      curl_ns, "get_header_size",
      lcl_c_proc_new("curl::get_header_size", c_curl_get_info_header_size));
  lcl_ns_def(
      curl_ns, "get_request_size",
      lcl_c_proc_new("curl::get_request_size", c_curl_get_info_request_size));
  lcl_ns_def(
      curl_ns, "get_num_connects",
      lcl_c_proc_new("curl::get_num_connects", c_curl_get_info_num_connects));
  lcl_ns_def(
      curl_ns, "get_primary_ip",
      lcl_c_proc_new("curl::get_primary_ip", c_curl_get_info_primary_ip));
  lcl_ns_def(
      curl_ns, "get_primary_port",
      lcl_c_proc_new("curl::get_primary_port", c_curl_get_info_primary_port));
}
