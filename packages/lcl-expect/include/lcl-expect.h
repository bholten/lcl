#ifndef LCL_EXPECT_H
#define LCL_EXPECT_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the expect:: namespace with the interpreter.
 *
 * Provides pattern-based matching for interactive program automation:
 *
 * Pattern creation:
 *   expect::pattern str ?opts?  - Create literal pattern
 *   expect::regex str ?opts?    - Create regex pattern
 *   expect::timeout             - Create timeout sentinel pattern
 *   expect::eof                 - Create EOF sentinel pattern
 *
 * Low-level matching:
 *   expect::match-buffer buf patterns ?opts? - Match buffer against patterns
 *
 * High-level matching (special forms):
 *   expect::match session (pattern handler ...) - Pattern/handler matching
 *   expect::loop session (pattern handler ...)  - Loop with break/continue
 *
 * Convenience functions (in Lcl layer):
 *   expect::spawn cmd ?opts?     - Spawn with PTY by default
 *   expect::send-line session str - Send with newline
 *   expect::wait-for session pattern - Wait for single pattern
 */
void lcl_register_expect(lcl_interp *interp);

#ifdef __cplusplus
}
#endif
  
#endif
