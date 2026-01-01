cmake_minimum_required(VERSION 3.14)
include(FetchContent)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lcl_json
    VERSION 0.1.0
    DESCRIPTION "Lcl cJson bindings"
    LANGUAGES C
  )

  # For development, use local LCL. Set LCL_LOCAL_PATH to override.
  # For release, use FetchContent from GitHub.
  if(DEFINED LCL_LOCAL_PATH)
    set(lcl_SOURCE_DIR ${LCL_LOCAL_PATH})
    add_subdirectory(${LCL_LOCAL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/lcl)
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt)
    # We're in packages/lcl-json, use parent LCL
    set(lcl_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    add_subdirectory(${lcl_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/lcl)
  else()
    FetchContent_Declare(
      lcl
      GIT_REPOSITORY https://github.com/bholten/lcl.git
      GIT_TAG        master
      GIT_SHALLOW    ON
    )
    FetchContent_MakeAvailable(lcl)
  endif()
endif()

FetchContent_Declare(
  cJSON
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON
  GIT_TAG        v1.7.19
  GIT_SHALLOW    ON
)

set(ENABLE_CJSON_TEST OFF CACHE BOOL "cJSON: Build with unit testing" FORCE)

FetchContent_MakeAvailable(cJSON)

set(LCL_JSON_SOURCES
  src/lcl-json.c
)

# Create the lcl_json library
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

# Standalone build: create test executable
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LCL_JSON_BUILD_TESTS "Build test executable" ON)
  option(LCL_JSON_EMBED_TEST_FRAMEWORK "Embed Test framework in test executable" ON)

  if(LCL_JSON_BUILD_TESTS)
    add_executable(lcl-json-test src/lcl-test.c)
    target_link_libraries(lcl-json-test PRIVATE lcl_json lcl::lcl)

    if(LCL_JSON_EMBED_TEST_FRAMEWORK)
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
      add_dependencies(lcl-json-test generate_test_framework_data)

      target_include_directories(lcl-json-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
      target_compile_definitions(lcl-json-test PRIVATE LCL_HAVE_TEST)
    endif()
  endif()

  # Installation rules for standalone build (disabled for now due to export issues)
  # include(GNUInstallDirs)
  # include(CMakePackageConfigHelpers)
  #
  # install(TARGETS lcl_json
  #   EXPORT lclJsonTargets
  #   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  #   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  # )
  #
  # install(FILES include/lcl-json.h
  #   DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  # )
  #
  # install(EXPORT lclJsonTargets
  #   FILE lclJsonTargets.cmake
  #   NAMESPACE lcl::
  #   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl_json
  # )
endif()
