# lcl-curl: libcurl bindings for LCL
# Built as part of the LCL monorepo

include(FetchContent)

FetchContent_Declare(
  curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG        curl-8_11_1
  GIT_SHALLOW    ON
)

# Configure curl options
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(curl)

set(LCL_CURL_SOURCES
  src/lcl-curl.c
)

add_library(lcl_curl STATIC ${LCL_CURL_SOURCES})

target_include_directories(lcl_curl
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_curl PRIVATE -Wall -Wextra)

target_link_libraries(lcl_curl
  PUBLIC lcl::lcl
  PRIVATE libcurl
)
