#ifndef LCL_PROCESS_H
#define LCL_PROCESS_H

#include <lcl.h>

/*
 * Register the process:: namespace with the interpreter.
 *
 * Provides:
 *   process::run      - synchronous execution with capture
 *   process::spawn    - asynchronous execution, returns handle
 *   process::send     - write to process stdin
 *   process::read     - read from process stdout/stderr
 *   process::wait     - wait for process to exit
 *   process::alive?   - check if process is running
 *   process::kill     - send signal to process
 *   process::close    - close handle and cleanup
 *   process::close-stdin - close stdin (half-close)
 */
void lcl_register_process(lcl_interp *interp);

#endif
