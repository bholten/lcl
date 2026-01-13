# lcl-regex: POSIX regex bindings for LCL
# Built as part of the LCL monorepo

set(LCL_REGEX_SOURCES
  src/lcl-regex.c
)

add_library(lcl_regex STATIC ${LCL_REGEX_SOURCES})

target_include_directories(lcl_regex
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_regex PRIVATE -Wall -Wextra)
target_link_libraries(lcl_regex PRIVATE lcl::lcl)
