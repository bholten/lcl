#ifndef LCL_CURL_H
#define LCL_CURL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - allows use without pulling in all of lcl.h */
typedef struct lcl_interp lcl_interp;

/*
 * Register CURL bindings with the interpreter.
 *
 * This adds the "curl" namespace with the following commands:
 *   curl::new      - Create a new CURL context
 *   curl::init     - Initialize CURL globally
 *   curl::reset    - Reset a CURL context
 *   curl::set_url  - Set request URL
 *   curl::set_verb - Set HTTP method
 *   curl::set_header - Add HTTP header(s)
 *   curl::set_body - Set request body
 *   curl::perform  - Execute the request
 *   curl::set_write_callback - Set callback for response data
 *   ... and many more options
 */
void lcl_register_curl(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* LCL_CURL_H */
