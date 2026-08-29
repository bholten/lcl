#ifndef LCL_EXPECT_H
#define LCL_EXPECT_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the Expect:: namespace with the interpreter.
 *
 * Provides pattern-based matching for interactive program automation:
 *
 * Pattern creation:
 *   Expect::pattern str ?opts?  - Create literal pattern
 *   Expect::regex str ?opts?    - Create regex pattern
 *   Expect::timeout             - Create timeout sentinel pattern
 *   Expect::eof                 - Create EOF sentinel pattern
 *
 * Low-level matching:
 *   Expect::match-buffer buf patterns ?opts? - Match buffer against patterns
 *
 * High-level matching (special forms):
 *   Expect::match session (pattern handler ...) - Pattern/handler matching
 *   Expect::loop session (pattern handler ...)  - Loop with break/continue
 *
 * Convenience functions (in Lcl layer):
 *   Expect::spawn cmd ?opts?     - Spawn with PTY by default
 *   Expect::send-line session str - Send with newline
 *   Expect::wait-for session pattern - Wait for single pattern
 */
void lcl_register_expect(lcl_interp *interp);

#ifdef __cplusplus
}
#endif
  
#endif
