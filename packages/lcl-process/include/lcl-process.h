#ifndef LCL_PROCESS_H
#define LCL_PROCESS_H

#include <lcl.h>

/*
 * Register the process:: namespace with the interpreter.
 *
 * Provides:
 *   process::run        - synchronous execution with capture
 *   process::spawn      - asynchronous execution, returns handle (with PTY support)
 *   process::send       - write to process stdin
 *   process::read       - read from process stdout/stderr
 *   process::read-until - read until pattern matched
 *   process::wait       - wait for process to exit
 *   process::alive?     - check if process is running
 *   process::kill       - send signal to process
 *   process::close      - close handle and cleanup
 *   process::close-stdin - close stdin (half-close)
 *   process::pty?       - check if handle is using PTY mode
 *   process::set-winsize - set terminal window size (PTY only)
 *   process::get-winsize - get terminal window size (PTY only)
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

#endif
