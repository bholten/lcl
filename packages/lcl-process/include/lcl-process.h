#ifndef LCL_PROCESS_H
#define LCL_PROCESS_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the Process:: namespace with the interpreter.
 *
 * Provides:
 *   Process::run        - synchronous execution with capture
 *   Process::spawn      - asynchronous execution, returns handle (with PTY support)
 *   Process::send       - write to process stdin
 *   Process::read       - read from process stdout/stderr
 *   Process::read-until - read until pattern matched
 *   Process::wait       - wait for process to exit
 *   Process::alive?     - check if process is running
 *   Process::kill       - send signal to process
 *   Process::close      - close handle and cleanup
 *   Process::close-stdin - close stdin (half-close)
 *   Process::pty?       - check if handle is using PTY mode
 *   Process::set-winsize - set terminal window size (PTY only)
 *   Process::get-winsize - get terminal window size (PTY only)
 *
 * spawn options:
 *   pty  - bool, use PTY mode for terminal emulation (enables interactive programs)
 *   rows - initial PTY rows (default: 24)
 *   cols - initial PTY cols (default: 80)
 *   env  - dict of environment vars
 *   cwd  - working directory
 *   merge - bool, merge stderr into stdout
 */
void lcl_register_process(lcl_interp *interp);

#ifdef __cplusplus
}
#endif
  
#endif
