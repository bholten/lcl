# lcl-raylib: Raylib bindings for LCL
# Built as part of the LCL monorepo

include(FetchContent)

set(raylib_BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples" FORCE)
set(raylib_BUILD_GAMES OFF CACHE BOOL "Build raylib games" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(raylib)

set(LCL_RAYLIB_SOURCES
  src/lcl-raylib.c
)

add_library(lcl_raylib STATIC ${LCL_RAYLIB_SOURCES})

target_include_directories(lcl_raylib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${raylib_SOURCE_DIR}
)

target_compile_options(lcl_raylib PRIVATE -Wall -Wextra)

target_link_libraries(lcl_raylib
  PRIVATE lcl::lcl raylib
)

# lcl_add_package_test(NAME lcl-raylib COMMAND test/test.lcl)
