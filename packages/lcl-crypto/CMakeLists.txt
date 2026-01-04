cmake_minimum_required(VERSION 3.14)
include(FetchContent)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(lcl_crypto
    VERSION 0.1.0
    DESCRIPTION "Lcl OpenSSL crypto bindings"
    LANGUAGES C
  )

  # For development, use local LCL. Set LCL_LOCAL_PATH to override.
  # For release, use FetchContent from GitHub.
  if(DEFINED LCL_LOCAL_PATH)
    set(lcl_SOURCE_DIR ${LCL_LOCAL_PATH})
    add_subdirectory(${LCL_LOCAL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/lcl)
  elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../CMakeLists.txt)
    # We're in packages/lcl-crypto, use parent LCL
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

find_package(OpenSSL REQUIRED)

set(LCL_CRYPTO_SOURCES
  src/lcl-crypto.c
)

# Create the lcl_crypto library
add_library(lcl_crypto STATIC ${LCL_CRYPTO_SOURCES})

target_include_directories(lcl_crypto
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_crypto PRIVATE -Wall -Wextra)

target_link_libraries(lcl_crypto
  PRIVATE lcl::lcl
  PUBLIC OpenSSL::SSL OpenSSL::Crypto
)

# Standalone build: create test executable
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LCL_CRYPTO_BUILD_TESTS "Build test executable" ON)
  option(LCL_CRYPTO_EMBED_TEST_FRAMEWORK "Embed Test framework in test executable" ON)

  if(LCL_CRYPTO_BUILD_TESTS)
    add_executable(lcl-crypto-test src/lcl-test.c)
    target_link_libraries(lcl-crypto-test PRIVATE lcl_crypto lcl::lcl)

    if(LCL_CRYPTO_EMBED_TEST_FRAMEWORK)
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

      add_custom_target(generate_crypto_test_framework_data DEPENDS ${TEST_FRAMEWORK_DATA})
      add_dependencies(lcl-crypto-test generate_crypto_test_framework_data)

      target_include_directories(lcl-crypto-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
      target_compile_definitions(lcl-crypto-test PRIVATE LCL_HAVE_TEST)
    endif()
  endif()
endif()
