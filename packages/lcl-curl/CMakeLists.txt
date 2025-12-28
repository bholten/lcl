cmake_minimum_required(VERSION 3.14)
include(FetchContent)

project(lcl_curl
  VERSION 0.1.0
  DESCRIPTION "Lcl CURL bindings"
  LANGUAGES C
)

option(LCL_CURL_BUILD_SHARED "Build shared library" ON)
option(LCL_CURL_BUILD_STATIC "Build static library" OFF)
option(LCL_CURL_BUILD_TESTS "Build test executable" ON)
option(LCL_CURL_ENABLE_ASAN "Enable AddressSanitizer (debug builds)" OFF)

set(LCL_CURL_SOURCES
  src/lcl-curl.c
)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(LCL_CURL_COMPILE_OPTIONS -Wall -Wextra)

FetchContent_Declare(
  lcl
  GIT_REPOSITORY https://github.com/bholten/lcl.git
  GIT_TAG        9f9b9f3fe367e808c10c0d42ae50ae0094f104e3
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(lcl)

FetchContent_Declare(
  curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG        curl-8_17_0
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(curl)

if(LCL_CURL_ENABLE_ASAN)
  list(APPEND LCL_CURL_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer)
  set(LCL_CURL_LINK_OPTIONS -fsanitize=address,undefined)
endif()

if(LCL_CURL_BUILD_SHARED)
  add_library(lcl_curl_shared SHARED ${LCL_CURL_SOURCES})
  set(LCL_BUILD_SHARED ON)

  set_target_properties(lcl_curl_shared PROPERTIES
    OUTPUT_NAME lcl_curl
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
  )

  target_include_directories(lcl_curl_shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

  target_compile_options(lcl_curl_shared PRIVATE ${LCL_CURL_COMPILE_OPTIONS})
  target_link_libraries(lcl_curl_shared PRIVATE lcl::lcl)

  if(LCL_ENABLE_ASAN)
    target_link_options(lcl_curl_shared PRIVATE ${LCL_CURL_LINK_OPTIONS})
  endif()
endif()

if(LCL_CURL_BUILD_STATIC)
  set(LCL_BUILD_STATIC ON)
  add_library(lcl_curl_static STATIC ${LCL_CURL_SOURCES})

  set_target_properties(lcl_curl_static PROPERTIES
    OUTPUT_NAME lcl_curl
    POSITION_INDEPENDENT_CODE ON
  )

  target_include_directories(lcl_curl_static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  target_compile_options(lcl_curl_static PRIVATE ${LCL_CURL_COMPILE_OPTIONS})
  target_link_libraries(lcl_curl_static PRIVATE lcl::lcl)

  if(LCL_ENABLE_ASAN)
    target_link_options(lcl_curl_static PRIVATE ${LCL_CURL_LINK_OPTIONS})
  endif()
endif()

if(LCL_CURL_BUILD_STATIC)
  add_library(lcl_curl::lcl_curl ALIAS lcl_curl_static)
elseif(LCL_CURL_BUILD_SHARED)
  add_library(lcl_curl::lcl_curl ALIAS lcl_curl_shared)
endif()

if(LCL_CURL_BUILD_TESTS)
  add_executable(lcl-test src/lcl-test.c)
  target_compile_definitions(lcl-test PRIVATE LCL_TEST DEBUG_REFC)
  target_compile_options(lcl-test PRIVATE ${LCL_CURL_COMPILE_OPTIONS})

  if(LCL_CURL_BUILD_STATIC)
    target_link_libraries(lcl-test PRIVATE lcl_curl_static lcl_static)
  else()
    target_link_libraries(lcl-test PRIVATE lcl_curl_shared lcl_shared)
  endif()

  target_link_libraries(lcl-test PRIVATE lcl_curl_static libcurl)

  if(LCL_ENABLE_ASAN)
    target_link_options(lcl-test PRIVATE ${LCL_CURL_LINK_OPTIONS})
  endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  if(LCL_CURL_BUILD_SHARED)
    install(TARGETS lcl_curl_shared
      EXPORT lclCurlTargets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()

  # if(LCL_CURL_BUILD_STATIC)
  #   install(TARGETS lcl_curl_static
  #     EXPORT lclCurlTargets
  #     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  #     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  #   )
  # endif()

  install(FILES include/lcl_curl.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(EXPORT lclCurlTargets
    FILE lclCurlTargets.cmake
    NAMESPACE lcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl_curl
  )

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lclCurlConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/lclCurlConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl_curl
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/lclCurlConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lclCurlConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lclCurlConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl_curl
  )
endif()
