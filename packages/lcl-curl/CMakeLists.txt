cmake_minimum_required(VERSION 3.14)
include(FetchContent)

# When built as a subproject, we don't declare a separate project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lcl_curl
    VERSION 0.1.0
    DESCRIPTION "Lcl CURL bindings"
    LANGUAGES C
  )

  # Standalone build: fetch LCL
  FetchContent_Declare(
    lcl
    GIT_REPOSITORY https://github.com/bholten/lcl.git
    GIT_TAG        master
    GIT_SHALLOW    ON
  )
  FetchContent_MakeAvailable(lcl)
endif()

# Fetch CURL
FetchContent_Declare(
  curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG        curl-8_11_1
  GIT_SHALLOW    ON
)

# Configure curl options before making available
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(curl)

set(LCL_CURL_SOURCES
  src/lcl-curl.c
)

# Create the lcl_curl library
add_library(lcl_curl STATIC ${LCL_CURL_SOURCES})

target_include_directories(lcl_curl
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_curl PRIVATE -Wall -Wextra)

# Link against parent LCL and CURL
target_link_libraries(lcl_curl
  PUBLIC lcl::lcl
  PRIVATE libcurl
)

# Standalone build: create test executable
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LCL_CURL_BUILD_TESTS "Build test executable" ON)

  if(LCL_CURL_BUILD_TESTS)
    add_executable(lcl-curl-test src/lcl-test.c)
    target_link_libraries(lcl-curl-test PRIVATE lcl_curl lcl::lcl libcurl)
  endif()

  # Installation rules for standalone build
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS lcl_curl
    EXPORT lclCurlTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  install(FILES include/lcl-curl.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(EXPORT lclCurlTargets
    FILE lclCurlTargets.cmake
    NAMESPACE lcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl_curl
  )
endif()
