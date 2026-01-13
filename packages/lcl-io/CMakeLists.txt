# lcl-io: File I/O extensions for LCL
# Built as part of the LCL monorepo

set(LCL_IO_SOURCES
  src/lcl-io.c
)

add_library(lcl_io STATIC ${LCL_IO_SOURCES})

target_include_directories(lcl_io
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_io PRIVATE -Wall -Wextra)
target_link_libraries(lcl_io PRIVATE lcl::lcl)
