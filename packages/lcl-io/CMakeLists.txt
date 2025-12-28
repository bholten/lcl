cmake_minimum_required(VERSION 3.14)
include(FetchContent)

# When built as a subproject, we don't declare a separate project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lcl_io
    VERSION 0.1.0
    DESCRIPTION "Lcl IO extensions"
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

# Configure curl options before making available
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)

set(LCL_IO_SOURCES
  src/lcl-io.c
)

# Create the lcl_io library
add_library(lcl_io STATIC ${LCL_IO_SOURCES})

target_include_directories(lcl_io
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_io PRIVATE -Wall -Wextra)

# Link against parent LCL and CURL
target_link_libraries(lcl_io
  PUBLIC lcl::lcl
  PRIVATE libcurl
)

# Standalone build: create test executable
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LCL_IO_BUILD_TESTS "Build test executable" ON)

  if(LCL_IO_BUILD_TESTS)
    add_executable(lcl-curl-test src/lcl-test.c)
    target_link_libraries(lcl-curl-test PRIVATE lcl_io lcl::lcl libcurl)
  endif()

  # Installation rules for standalone build
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS lcl_io
    EXPORT lclIoTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  install(FILES include/lcl-curl.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(EXPORT lclIoTargets
    FILE lclIoTargets.cmake
    NAMESPACE lcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl_io
  )
endif()
