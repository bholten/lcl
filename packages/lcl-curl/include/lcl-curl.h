#ifndef LCL_CURL_H
#define LCL_CURL_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register CURL bindings with the interpreter.
 *
 * This adds the "curl" namespace with the following commands:
 *   Curl::new      - Create a new CURL context
 *   Curl::init     - Initialize CURL globally
 *   Curl::reset    - Reset a CURL context
 *   Curl::set_url  - Set request URL
 *   Curl::set_verb - Set HTTP method
 *   Curl::set_header - Add HTTP header(s)
 *   Curl::set_body - Set request body
 *   Curl::perform  - Execute the request
 *   Curl::set_write_callback - Set callback for response data
 *   ... and many more options
 */
void lcl_register_curl(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* LCL_CURL_H */
