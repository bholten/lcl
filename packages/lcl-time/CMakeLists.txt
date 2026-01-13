# lcl-time: Time functions for LCL
# Built as part of the LCL monorepo

set(LCL_TIME_SOURCES
  src/lcl-time.c
)

add_library(lcl_time STATIC ${LCL_TIME_SOURCES})

target_include_directories(lcl_time
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_time PRIVATE -Wall -Wextra)
target_link_libraries(lcl_time PRIVATE lcl::lcl)
