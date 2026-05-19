# lcl-json: cJSON bindings for LCL
# Built as part of the LCL monorepo

include(FetchContent)

FetchContent_Declare(
  cJSON
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON
  GIT_TAG        v1.7.19
  GIT_SHALLOW    ON
)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "cJSON: Build with unit testing" FORCE)

# cJSON 1.7.19's CMakeLists declares `cmake_minimum_required(VERSION
# 2.8.5)`, which CMake 4.x rejects on macOS
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(cJSON)

set(LCL_JSON_SOURCES
  src/lcl-json.c
)

add_library(lcl_json STATIC ${LCL_JSON_SOURCES})

target_include_directories(lcl_json
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${cjson_SOURCE_DIR}
)

target_compile_options(lcl_json PRIVATE -Wall -Wextra)

target_link_libraries(lcl_json
  PRIVATE lcl::lcl cjson
)

lcl_add_package_test(NAME lcl-json COMMAND test/test.lcl)
