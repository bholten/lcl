cmake_minimum_required(VERSION 3.14)

project(lcl
  VERSION 0.1.0
  DESCRIPTION "Lexical Command Language - A Tcl-like scripting language with lexical scoping"
  LANGUAGES C
)

set(LCL_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/lcl-version.h.in"
  "${LCL_VERSION_HEADER_DIR}/lcl-version.h"
  @ONLY
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EmbedFile)
include(PackageTest)

enable_testing()

option(LCL_BUILD_SHARED "Build shared library" ON)
option(LCL_BUILD_STATIC "Build static library" ON)
option(LCL_BUILD_CLI "Build command-line interpreter" ON)
option(LCL_BUILD_TESTS "Build test executable" OFF)
option(LCL_BUILD_EXAMPLES "Build embedding examples under examples/" OFF)
option(LCL_ENABLE_ASAN "Enable AddressSanitizer (debug builds)" OFF)
option(LCL_BUILD_FUZZERS "Build libFuzzer targets (requires Clang)" OFF)
option(LCL_ENABLE_CLANG_TIDY "Run clang-tidy alongside compilation" OFF)
option(LCL_DEBUG_REFC "Enable debug reference counting" OFF)
option(LCL_STATIC_BINARY "Build fully static CLI binary (requires static libs)" OFF)

# Optional packages
option(LCL_BUILD_CURL "Build CURL bindings" OFF)
option(LCL_BUILD_IO "Build Lcl IO extension library" OFF)
option(LCL_BUILD_POSIX "Build Lcl POSIX bindings" OFF)
option(LCL_BUILD_JSON "Build Lcl with JSON library" OFF)
option(LCL_BUILD_CRYPTO "Build Lcl with OpenSSL crypto bindings" OFF)
option(LCL_BUILD_PROCESS "Build Lcl with the Process extension library" OFF)
option(LCL_BUILD_REGEX "Build Lcl with the Regex extension library" OFF)
option(LCL_BUILD_TIME "Build Lcl with the Time extension library" OFF)
option(LCL_BUILD_MATH "Build Lcl with the Math extension library" OFF)
option(LCL_BUILD_EXPECT "Build Lcl with the Expect extension library" OFF)

# Lcl libraries
option(LCL_BUILD_TEST_LIB "Build Lcl with the Test library" OFF)
option(LCL_BUILD_SH_LIB "Build Lcl with the Sh library" OFF)
option(LCL_BUILD_CURL_DSL_LIB "Build Lcl with the cURL DSL library" OFF)
option(LCL_BUILD_DOC_LIB "Build Lcl with the Doc library" OFF)

# True when Lcl is the top-level CMake project (i.e. the user invoked
# cmake on this directory directly, not as a subproject). Package
# CMakeLists check this before adding install() rules.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(LCL_IS_TOP_LEVEL TRUE)
else()
  set(LCL_IS_TOP_LEVEL FALSE)
endif()

set(LCL_SOURCES
  src/hash-table.c
  src/lcl-api.c
  src/lcl-cell.c
  src/lcl-command.c
  src/lcl-dict.c
  src/lcl-env.c
  src/lcl-eval.c
  src/lcl-frame.c
  src/lcl-interp.c
  src/lcl-list.c
  src/lcl-ns.c
  src/lcl-num.c
  src/lcl-opaque.c
  src/lcl-path.c
  src/lcl-proc.c
  src/lcl-program.c
  src/lcl-program-push.c
  src/lcl-ref.c
  src/lcl-scan.c
  src/lcl-stdlib.c
  src/lcl-string.c
  src/lcl-word.c
  src/str-compat.c
)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(LCL_COMPILE_OPTIONS
  -Wall -Wextra -Wdeclaration-after-statement
  -Wconversion -Wsign-conversion
)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if(LCL_ENABLE_CLANG_TIDY)
  find_program(LCL_CLANG_TIDY_EXE clang-tidy)
  if(NOT LCL_CLANG_TIDY_EXE)
    message(FATAL_ERROR "LCL_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
  endif()
  # Applies to every target defined after this point; checks come
  # from the repo-root .clang-tidy.
  set(CMAKE_C_CLANG_TIDY "${LCL_CLANG_TIDY_EXE}")
endif()

if(LCL_ENABLE_ASAN)
  # ASan overrides optimization to ensure accurate diagnostics
  list(APPEND LCL_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer -g3 -O0)
  set(LCL_LINK_OPTIONS -fsanitize=address,undefined)
endif()

if(LCL_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "LCL_BUILD_FUZZERS requires Clang (configure with CC=clang)")
  endif()
  if(NOT LCL_BUILD_STATIC)
    message(FATAL_ERROR "LCL_BUILD_FUZZERS requires LCL_BUILD_STATIC=ON")
  endif()
  if(LCL_ENABLE_ASAN)
    message(FATAL_ERROR "LCL_BUILD_FUZZERS already enables ASan/UBSan; leave LCL_ENABLE_ASAN off")
  endif()
  # Instrument the whole tree for coverage feedback (fuzzer-no-link);
  # each fuzz executable links the driver via -fsanitize=fuzzer below.
  # -O1 keeps execution fast while ASan stays accurate.
  list(APPEND LCL_COMPILE_OPTIONS
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1)
  set(LCL_LINK_OPTIONS -fsanitize=address,undefined)
endif()

# Static binary requires static library
if(LCL_STATIC_BINARY AND NOT LCL_BUILD_STATIC)
  message(FATAL_ERROR "LCL_STATIC_BINARY requires LCL_BUILD_STATIC=ON")
endif()

if(LCL_BUILD_SHARED)
  add_library(lcl_shared SHARED ${LCL_SOURCES})
  set_target_properties(lcl_shared PROPERTIES
    OUTPUT_NAME lcl
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
  )
  target_include_directories(lcl_shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${LCL_VERSION_HEADER_DIR}>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lcl_shared PRIVATE ${LCL_COMPILE_OPTIONS})
  if(LCL_ENABLE_ASAN OR LCL_BUILD_FUZZERS)
    target_link_options(lcl_shared PRIVATE ${LCL_LINK_OPTIONS})
  endif()
endif()

if(LCL_BUILD_STATIC)
  add_library(lcl_static STATIC ${LCL_SOURCES})
  set_target_properties(lcl_static PROPERTIES
    OUTPUT_NAME lcl
    POSITION_INDEPENDENT_CODE ON
  )
  target_include_directories(lcl_static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${LCL_VERSION_HEADER_DIR}>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lcl_static PRIVATE ${LCL_COMPILE_OPTIONS})
  if(LCL_ENABLE_ASAN OR LCL_BUILD_FUZZERS)
    target_link_options(lcl_static PRIVATE ${LCL_LINK_OPTIONS})
  endif()
endif()

if(LCL_BUILD_STATIC)
  add_library(lcl::lcl ALIAS lcl_static)
elseif(LCL_BUILD_SHARED)
  add_library(lcl::lcl ALIAS lcl_shared)
endif()

if(LCL_BUILD_CLI)
  add_executable(lcl-cli src/lcl-main.c)
  set_target_properties(lcl-cli PROPERTIES OUTPUT_NAME lcl)
  target_compile_options(lcl-cli PRIVATE ${LCL_COMPILE_OPTIONS})

  if(LCL_BUILD_STATIC)
    target_link_libraries(lcl-cli PRIVATE lcl_static)
  else()
    target_link_libraries(lcl-cli PRIVATE lcl_shared)
  endif()

  if(LCL_ENABLE_ASAN OR LCL_BUILD_FUZZERS)
    target_link_options(lcl-cli PRIVATE ${LCL_LINK_OPTIONS})
  endif()

  # Build fully static binary (all libraries linked statically)
  if(LCL_STATIC_BINARY)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
      target_link_options(lcl-cli PRIVATE -static)
    elseif(MSVC)
      set_property(TARGET lcl-cli PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
  endif()
endif()

# ==============================================================================
# Optional Packages
# ==============================================================================

if(LCL_BUILD_CURL)
  add_subdirectory(packages/lcl-curl)

  # Add compile definition for conditional compilation
  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_CURL)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_CURL)
  endif()

  # Link CLI against curl package
  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_CURL)
    target_link_libraries(lcl-cli PRIVATE lcl_curl)
  endif()
endif()

if(LCL_BUILD_IO)
  add_subdirectory(packages/lcl-io)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_IO)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_IO)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_IO)
    target_link_libraries(lcl-cli PRIVATE lcl_io)
  endif()
endif()

if(LCL_BUILD_POSIX)
  add_subdirectory(packages/lcl-posix)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_POSIX)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_POSIX)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_POSIX)
    target_link_libraries(lcl-cli PRIVATE lcl_posix)
  endif()
endif()

if(LCL_BUILD_JSON)
  add_subdirectory(packages/lcl-json)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_JSON)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_JSON)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_JSON)
    target_link_libraries(lcl-cli PRIVATE lcl_json)
  endif()
endif()

if(LCL_BUILD_CRYPTO)
  add_subdirectory(packages/lcl-crypto)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_CRYPTO)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_CRYPTO)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_CRYPTO)
    target_link_libraries(lcl-cli PRIVATE lcl_crypto)
  endif()
endif()

if(LCL_BUILD_PROCESS)
  add_subdirectory(packages/lcl-process)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_PROCESS)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_PROCESS)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_PROCESS)
    target_link_libraries(lcl-cli PRIVATE lcl_process)
  endif()
endif()

if(LCL_BUILD_REGEX)
  add_subdirectory(packages/lcl-regex)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_REGEX)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_REGEX)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_REGEX)
    target_link_libraries(lcl-cli PRIVATE lcl_regex)
  endif()
endif()

if(LCL_BUILD_TIME)
  add_subdirectory(packages/lcl-time)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_TIME)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_TIME)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_TIME)
    target_link_libraries(lcl-cli PRIVATE lcl_time)
  endif()
endif()

if(LCL_BUILD_MATH)
  add_subdirectory(packages/lcl-math)

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_MATH)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_MATH)
  endif()

  if(LCL_BUILD_CLI)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_MATH)
    target_link_libraries(lcl-cli PRIVATE lcl_math)
  endif()
endif()

if(LCL_BUILD_EXPECT)
  # lcl-expect requires lcl-process
  if(NOT LCL_BUILD_PROCESS)
    message(FATAL_ERROR "LCL_BUILD_EXPECT requires LCL_BUILD_PROCESS=ON")
  endif()

  add_subdirectory(packages/lcl-expect)

  lcl_embed_file(
    INPUT  packages/lcl-expect/src/expect.lcl
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/expect-data.h
    SYMBOL packages_lcl_expect_src_expect_lcl
    TARGET generate_expect_data
  )

  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC LCL_HAVE_EXPECT)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC LCL_HAVE_EXPECT)
  endif()

  if(LCL_BUILD_CLI)
    add_dependencies(lcl-cli generate_expect_data)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_EXPECT)
    target_include_directories(lcl-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(lcl-cli PRIVATE lcl_expect)
  endif()
endif()

if(LCL_BUILD_TEST_LIB)
  lcl_embed_file(
    INPUT  lib/test/src/Test.lcl
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test-framework-data.h
    SYMBOL lib_test_src_Test_lcl
    TARGET generate_test_framework_data
  )

  if(LCL_BUILD_CLI)
    add_dependencies(lcl-cli generate_test_framework_data)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_TEST)
    target_include_directories(lcl-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()

if(LCL_BUILD_SH_LIB)
  lcl_embed_file(
    INPUT  lib/sh/src/sh.lcl
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sh-lib-data.h
    SYMBOL lib_sh_src_sh_lcl
    TARGET generate_sh_lib_data
  )

  if(LCL_BUILD_CLI)
    add_dependencies(lcl-cli generate_sh_lib_data)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_SH_LIB)
    target_include_directories(lcl-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()


if(LCL_BUILD_CURL_DSL_LIB)
  lcl_embed_file(
    INPUT  lib/curl-dsl/src/curl-dsl.lcl
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/curl-dsl-lib-data.h
    SYMBOL lib_curl_dsl_src_curl_dsl_lcl
    TARGET generate_curl_dsl_lib_data
  )

  if(LCL_BUILD_CLI)
    add_dependencies(lcl-cli generate_curl_dsl_lib_data)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_CURL_DSL_LIB)
    target_include_directories(lcl-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()

if(LCL_BUILD_DOC_LIB)
  lcl_embed_file(
    INPUT  lib/doc/src/Doc.lcl
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/doc-lib-data.h
    SYMBOL lib_doc_src_Doc_lcl
    TARGET generate_doc_lib_data
  )

  if(LCL_BUILD_CLI)
    add_dependencies(lcl-cli generate_doc_lib_data)
    target_compile_definitions(lcl-cli PRIVATE LCL_HAVE_DOC_LIB)
    target_include_directories(lcl-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  endif()
endif()

if(LCL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(LCL_BUILD_FUZZERS)
  foreach(FUZZ_NAME compile eval)
    add_executable(fuzz-${FUZZ_NAME} test/fuzz/fuzz-${FUZZ_NAME}.c)
    target_compile_options(fuzz-${FUZZ_NAME} PRIVATE ${LCL_COMPILE_OPTIONS})
    target_include_directories(fuzz-${FUZZ_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(fuzz-${FUZZ_NAME} PRIVATE lcl_static)
    target_link_options(fuzz-${FUZZ_NAME} PRIVATE
      -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
  endforeach()
endif()

if(LCL_BUILD_TESTS)
  # The C-level harness uses `-Wl,--wrap` to inject failures into
  # calloc/strndup/strdup and intercept a couple of internal calls for
  # OOM coverage. Apple's linker rejects it.
  if(NOT APPLE)
    add_executable(lcl-test test/lcl-test.c)
    target_compile_definitions(lcl-test PRIVATE LCL_TEST DEBUG_REFC)
    target_compile_options(lcl-test PRIVATE ${LCL_COMPILE_OPTIONS})
    target_include_directories(lcl-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    if(LCL_BUILD_STATIC)
      target_link_libraries(lcl-test PRIVATE lcl_static)
    else()
      target_link_libraries(lcl-test PRIVATE lcl_shared)
    endif()

    if(LCL_ENABLE_ASAN OR LCL_BUILD_FUZZERS)
      target_link_options(lcl-test PRIVATE ${LCL_LINK_OPTIONS})
    endif()

    target_link_options(lcl-test PRIVATE
      -Wl,--wrap=calloc
      -Wl,--wrap=strndup
      -Wl,--wrap=strdup
      -Wl,--wrap=lcl_program_push_command
      -Wl,--wrap=hash_table_put)

    add_test(NAME lcl-test-c COMMAND lcl-test)
  endif()

  if(LCL_BUILD_CLI AND LCL_BUILD_TEST_LIB)
    add_test(
      NAME conformance-smoke
      COMMAND $<TARGET_FILE:lcl-cli>
              "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance_smoke.lcl"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(
      NAME anaphoric-macros
      COMMAND $<TARGET_FILE:lcl-cli>
              "${CMAKE_CURRENT_SOURCE_DIR}/test/anaphoric_macros.lcl"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(LCL_BUILD_DOC_LIB)
      add_test(
        NAME doc-lib
        COMMAND $<TARGET_FILE:lcl-cli>
                "${CMAKE_CURRENT_SOURCE_DIR}/lib/doc/test/doc_test.lcl"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      )
    endif()
  endif()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  if(LCL_BUILD_SHARED)
    install(TARGETS lcl_shared
      EXPORT lclTargets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()

  if(LCL_BUILD_STATIC)
    install(TARGETS lcl_static
      EXPORT lclTargets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()

  if(LCL_BUILD_CLI)
    install(TARGETS lcl-cli
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()

  install(FILES
    include/lcl.h
    ${LCL_VERSION_HEADER_DIR}/lcl-version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(EXPORT lclTargets
    FILE lclTargets.cmake
    NAMESPACE lcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl
  )

  # Packages that have public/transitive deps record them here so
  # lclConfig.cmake can find_dependency() them before loading the
  # targets file.
  set(LCL_INSTALL_DEPS "")
  if(LCL_BUILD_CRYPTO)
    list(APPEND LCL_INSTALL_DEPS "OpenSSL")
  endif()

  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/lclConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/lclConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/lclConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lclConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lclConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl
  )
endif()

# Debugging
if(LCL_DEBUG_REFC)
  if(LCL_BUILD_SHARED)
    target_compile_definitions(lcl_shared PUBLIC DEBUG_REFC)
  endif()
  if(LCL_BUILD_STATIC)
    target_compile_definitions(lcl_static PUBLIC DEBUG_REFC)
  endif()
endif()
