cmake_minimum_required(VERSION 3.14)
include(FetchContent)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lcl_process
    VERSION 0.1.0
    DESCRIPTION "Lcl process spawning and management"
    LANGUAGES C
  )

  FetchContent_Declare(
    lcl
    GIT_REPOSITORY https://github.com/bholten/lcl.git
    GIT_TAG        master
    GIT_SHALLOW    ON
  )
  FetchContent_MakeAvailable(lcl)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(LCL_PROCESS_SOURCES
  src/lcl-process.c
)

add_library(lcl_process STATIC ${LCL_PROCESS_SOURCES})

target_include_directories(lcl_process
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_process PRIVATE -Wall -Wextra)

target_link_libraries(lcl_process PRIVATE lcl::lcl)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LCL_PROCESS_BUILD_TESTS "Build test executable" ON)
  option(LCL_PROCESS_EMBED_TEST_FRAMEWORK "Embed Test framework in test executable" ON)

  if(LCL_PROCESS_BUILD_TESTS)
    add_executable(lcl-process-test src/lcl-test.c)
    target_link_libraries(lcl-process-test PRIVATE lcl_process lcl::lcl)

    if(LCL_PROCESS_EMBED_TEST_FRAMEWORK)
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
      add_dependencies(lcl-process-test generate_test_framework_data)

      target_include_directories(lcl-process-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
      target_compile_definitions(lcl-process-test PRIVATE LCL_HAVE_TEST)
    endif()
  endif()

  include(GNUInstallDirs)

  install(TARGETS lcl_process
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  install(FILES include/lcl-process.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
