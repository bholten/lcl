# lcl-process: Process spawning and management for LCL
# Built as part of the LCL monorepo

set(LCL_PROCESS_SOURCES
  src/lcl-process.c
)

add_library(lcl_process STATIC ${LCL_PROCESS_SOURCES})

target_include_directories(lcl_process
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_process PRIVATE -Wall -Wextra)
target_link_libraries(lcl_process PRIVATE lcl::lcl)

# Link util library for PTY support (openpty)
# - macOS/BSD: uses util.h, needs -lutil
# - Linux (glibc): uses pty.h, needs -lutil
# - Linux (musl): openpty is in libc, no extra lib needed
include(CheckFunctionExists)
check_function_exists(openpty HAVE_OPENPTY_IN_LIBC)
if(NOT HAVE_OPENPTY_IN_LIBC)
  target_link_libraries(lcl_process PRIVATE util)
endif()

lcl_add_package_test(NAME lcl-process COMMAND test/process_test.lcl)
