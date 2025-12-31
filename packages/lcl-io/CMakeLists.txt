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

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

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

# Link against LCL (PRIVATE since consumers link lcl separately)
target_link_libraries(lcl_io PRIVATE lcl::lcl)

# Standalone build: create test executable
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LCL_IO_BUILD_TESTS "Build test executable" ON)
  option(LCL_IO_EMBED_TEST_FRAMEWORK "Embed Test framework in test executable" ON)

  if(LCL_IO_BUILD_TESTS)
    add_executable(lcl-io-test src/lcl-test.c)
    target_link_libraries(lcl-io-test PRIVATE lcl_io lcl::lcl)

    if(LCL_IO_EMBED_TEST_FRAMEWORK)
      # Generate test-framework-data.h from Test.lcl using xxd
      find_program(XXD_EXECUTABLE xxd REQUIRED)

      set(TEST_FRAMEWORK_SRC ${lcl_SOURCE_DIR}/lib/Test.lcl)
      set(TEST_FRAMEWORK_DATA ${CMAKE_CURRENT_BINARY_DIR}/test-framework-data.h)

      add_custom_command(
        OUTPUT ${TEST_FRAMEWORK_DATA}
        COMMAND ${XXD_EXECUTABLE} -i lib/Test.lcl > ${TEST_FRAMEWORK_DATA}
        WORKING_DIRECTORY ${lcl_SOURCE_DIR}
        DEPENDS ${TEST_FRAMEWORK_SRC}
        COMMENT "Generating test-framework-data.h from Test.lcl"
      )

      add_custom_target(generate_test_framework_data DEPENDS ${TEST_FRAMEWORK_DATA})
      add_dependencies(lcl-io-test generate_test_framework_data)

      target_include_directories(lcl-io-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
      target_compile_definitions(lcl-io-test PRIVATE LCL_HAVE_TEST)
    endif()
  endif()

  # Installation rules for standalone build
  include(GNUInstallDirs)

  install(TARGETS lcl_io
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  install(FILES include/lcl-io.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
