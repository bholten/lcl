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
  lcl_value *header_callback;

  /* SSE support */
  lcl_value *sse_callback;
  char *sse_buffer;
  size_t sse_buffer_len;
  size_t sse_buffer_cap;
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
  ctx->header_callback = NULL;
  ctx->sse_callback = NULL;
  ctx->sse_buffer = NULL;
  ctx->sse_buffer_len = 0;
  ctx->sse_buffer_cap = 0;

  return ctx;
}

static void curl_context_free(struct curl_context *ctx) {
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

  if (ctx->header_callback) {
    lcl_ref_dec(ctx->header_callback);
  }

  if (ctx->sse_callback) {
    lcl_ref_dec(ctx->sse_callback);
  }

  if (ctx->sse_buffer) {
    free(ctx->sse_buffer);
  }

  free(ctx);
}

static void curl_context_sse_buffer_append(struct curl_context *ctx,
                                           const char *contents, size_t size) {
  size_t new_len = ctx->sse_buffer_len + size;

  if (new_len + 1 > ctx->sse_buffer_cap) {
    size_t new_cap = ctx->sse_buffer_cap == 0 ? 1024 : ctx->sse_buffer_cap * 2;
    char *new_buf;

    while (new_cap < new_len + 1) {
      new_cap *= 2;
    }

    new_buf = realloc(ctx->sse_buffer, new_cap);

    if (!new_buf) {
      return;
    }

    ctx->sse_buffer = new_buf;
    ctx->sse_buffer_cap = new_cap;
  }

  memcpy(ctx->sse_buffer + ctx->sse_buffer_len, contents, size);
  ctx->sse_buffer_len = new_len;
  ctx->sse_buffer[ctx->sse_buffer_len] = '\0';
}

/*
 * Parse an SSE event into a dict with keys: event, data, id, retry
 * SSE format:
 *   event: <type>     - event type (default "message")
 *   data: <content>   - event data (multiple lines joined with \n)
 *   id: <id>          - event ID
 *   retry: <ms>       - reconnection time
 *   : <comment>       - ignored
 */
static lcl_value *parse_sse_event(const char *event_text) {
  lcl_value *dict = lcl_dict_new();
  lcl_value *event_type = NULL;
  lcl_value *event_id = NULL;
  lcl_value *retry_val = NULL;
  char *data_buf = NULL;
  size_t data_len = 0;
  size_t data_cap = 0;
  const char *line_start;
  const char *line_end;
  const char *p;

  if (!dict) {
    return NULL;
  }

  line_start = event_text;

  while (*line_start) {
    line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') {
      line_end++;
    }

    if (*line_start == ':') {
      goto next_line;
    }

    p = line_start;

    while (p < line_end && *p != ':') {
      p++;
    }

    if (p < line_end) {
      size_t field_len = (size_t)(p - line_start);
      const char *value_start = p + 1;
      size_t value_len;

      if (value_start < line_end && *value_start == ' ') {
        value_start++;
      }

      value_len = (size_t)(line_end - value_start);

      if (field_len == 5 && strncmp(line_start, "event", 5) == 0) {
        /* event: <type> */
        char *type_str = malloc(value_len + 1);
        if (type_str) {
          memcpy(type_str, value_start, value_len);
          type_str[value_len] = '\0';
          if (event_type) {
            lcl_ref_dec(event_type);
          }
          event_type = lcl_string_new(type_str);
          free(type_str);
        }

      } else if (field_len == 4 && strncmp(line_start, "data", 4) == 0) {
        /* data: <content> - append to data buffer with \n separator */
        size_t needed = data_len + value_len + 2; /* +1 for \n, +1 for \0 */

        if (needed > data_cap) {
          size_t new_cap = data_cap == 0 ? 256 : data_cap * 2;
          char *new_buf;

          while (new_cap < needed) {
            new_cap *= 2;
          }

          new_buf = realloc(data_buf, new_cap);
          if (!new_buf) {
            goto cleanup;
          }
          data_buf = new_buf;
          data_cap = new_cap;
        }

        if (data_len > 0) {
          data_buf[data_len++] = '\n';
        }
        memcpy(data_buf + data_len, value_start, value_len);
        data_len += value_len;
        data_buf[data_len] = '\0';

      } else if (field_len == 2 && strncmp(line_start, "id", 2) == 0) {
        /* id: <id> */
        char *id_str = malloc(value_len + 1);
        if (id_str) {
          memcpy(id_str, value_start, value_len);
          id_str[value_len] = '\0';
          if (event_id) {
            lcl_ref_dec(event_id);
          }
          event_id = lcl_string_new(id_str);
          free(id_str);
        }

      } else if (field_len == 5 && strncmp(line_start, "retry", 5) == 0) {
        /* retry: <ms> */
        char *retry_str = malloc(value_len + 1);
        if (retry_str) {
          long retry_ms;
          memcpy(retry_str, value_start, value_len);
          retry_str[value_len] = '\0';
          retry_ms = strtol(retry_str, NULL, 10);
          free(retry_str);
          if (retry_val) {
            lcl_ref_dec(retry_val);
          }
          retry_val = lcl_int_new(retry_ms);
        }
      }
    }

  next_line:
    while (*line_end == '\r' || *line_end == '\n') {
      line_end++;
    }
    line_start = line_end;
  }

  if (event_type) {
    lcl_dict_put(&dict, "event", event_type);
    lcl_ref_dec(event_type);
  } else {
    /* Note: default event type is "message" */
    lcl_value *default_type = lcl_string_new("message");
    if (default_type) {
      lcl_dict_put(&dict, "event", default_type);
      lcl_ref_dec(default_type);
    }
  }

  if (data_buf && data_len > 0) {
    lcl_value *data_val = lcl_string_new(data_buf);
    if (data_val) {
      lcl_dict_put(&dict, "data", data_val);
      lcl_ref_dec(data_val);
    }
  }

  if (event_id) {
    lcl_dict_put(&dict, "id", event_id);
    lcl_ref_dec(event_id);
  }

  if (retry_val) {
    lcl_dict_put(&dict, "retry", retry_val);
    lcl_ref_dec(retry_val);
  }

  free(data_buf);
  return dict;

cleanup:
  free(data_buf);
  if (event_type) {
    lcl_ref_dec(event_type);
  }
  if (event_id) {
    lcl_ref_dec(event_id);
  }
  if (retry_val) {
    lcl_ref_dec(retry_val);
  }
  lcl_ref_dec(dict);
  return NULL;
}

static void curl_context_dispatch_sse_event(struct curl_context *ctx,
                                            const char *event_text) {
  lcl_value *result = NULL;
  lcl_value *parsed_event;

  if (!ctx->sse_callback) {
    return;
  }

  parsed_event = parse_sse_event(event_text);
  if (!parsed_event) {
    return;
  }

  lcl_call_proc(ctx->interp, ctx->sse_callback, 1, &parsed_event, &result);

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_ref_dec(parsed_event);
}

static void curl_context_sse_buffer_shift(struct curl_context *ctx,
                                          size_t consumed) {
  if (consumed == 0) {
    return;
  }

  if (consumed >= ctx->sse_buffer_len) {
    /* All data consumed */
    ctx->sse_buffer_len = 0;
    if (ctx->sse_buffer) {
      ctx->sse_buffer[0] = '\0';
    }
  } else {
    /* Shift remaining data to start of buffer */
    size_t remaining = ctx->sse_buffer_len - consumed;
    memmove(ctx->sse_buffer, ctx->sse_buffer + consumed, remaining);
    ctx->sse_buffer_len = remaining;
    ctx->sse_buffer[ctx->sse_buffer_len] = '\0';
  }
}

static int c_curl_new(lcl_interp *interp, int argc, lcl_value **argv,
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

static int c_curl_init(lcl_interp *interp, int argc, lcl_value **argv,
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

static int c_curl_reset(lcl_interp *interp, int argc, lcl_value **argv,
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

#define CURL_STRING_OPTION(fn_name, curl_opt)                                  \
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
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
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
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

static int c_curl_set_header(lcl_interp *interp, int argc, lcl_value **argv,
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
    ctx->headers = curl_slist_append(ctx->headers, header);
  }

  rc = curl_easy_setopt(ctx->curl, CURLOPT_HTTPHEADER, ctx->headers);

  if (rc != CURLE_OK) {
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

static int c_curl_set_body(lcl_interp *interp, int argc, lcl_value **argv,
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
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
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
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
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
  static int fn_name(lcl_interp *interp, int argc, lcl_value **argv,           \
                     lcl_value **out) {                                        \
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

static int c_curl_perform(lcl_interp *interp, int argc, lcl_value **argv,
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
static size_t curl_write_wrapper(char *contents, size_t size, size_t nmemb,
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

static int c_curl_set_write_callback(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
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

static size_t curl_sse_write_wrapper(char *contents, size_t size, size_t nmemb,
                                     void *userdata) {
  size_t realsize = size * nmemb;
  struct curl_context *ctx = (struct curl_context *)userdata;
  char *event_start = NULL;
  char *event_end = NULL;

  curl_context_sse_buffer_append(ctx, contents, realsize);
  event_start = ctx->sse_buffer;

  while ((event_end = strstr(event_start, "\n\n")) != NULL) {
    size_t event_len = (size_t)(event_end - event_start);
    char *event = malloc(event_len + 1);

    if (event) {
      memcpy(event, event_start, event_len);
      event[event_len] = '\0';

      curl_context_dispatch_sse_event(ctx, event);
      free(event);
    }

    event_start = event_end + 2;
  }

  /* Shift consumed data out of buffer */
  curl_context_sse_buffer_shift(ctx, (size_t)(event_start - ctx->sse_buffer));

  return realsize;
}

static size_t curl_header_wrapper(char *contents, size_t size, size_t nmemb,
                                  void *userdata) {
  size_t realsize = size * nmemb;
  struct curl_context *ctx = (struct curl_context *)userdata;
  lcl_value *result = NULL;
  lcl_value *arg;
  char *buf;

  if (!ctx->header_callback) {
    return realsize;
  }

  /* Headers include trailing \r\n - create null-terminated copy */
  buf = malloc(realsize + 1);
  if (!buf) {
    return 0;
  }
  memcpy(buf, contents, realsize);
  buf[realsize] = '\0';

  arg = lcl_string_new(buf);
  free(buf);

  if (!arg) {
    return 0;
  }

  lcl_call_proc(ctx->interp, ctx->header_callback, 1, &arg, &result);

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_ref_dec(arg);

  return realsize;
}

static int c_curl_set_header_callback(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
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

  ctx->header_callback = lcl_ref_inc(callback_proc);

  curl_easy_setopt(ctx->curl, CURLOPT_HEADERFUNCTION, curl_header_wrapper);
  curl_easy_setopt(ctx->curl, CURLOPT_HEADERDATA, (void *)ctx);

  return LCL_RC_OK;
}

static int c_curl_set_sse_callback(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
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

  ctx->sse_callback = lcl_ref_inc(callback_proc);

  curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, curl_sse_write_wrapper);
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
  lcl_ns_def(
      curl_ns, "set_header_callback",
      lcl_c_proc_new("curl::set_header_callback", c_curl_set_header_callback));
  lcl_ns_def(curl_ns, "set_sse_callback",
             lcl_c_proc_new("curl::set_sse_callback", c_curl_set_sse_callback));

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
