cmake_minimum_required(VERSION 3.14)

project(lcl
  VERSION 0.1.0
  DESCRIPTION "Lexical Command Language - A Tcl-like scripting language with lexical scoping"
  LANGUAGES C
)

option(LCL_BUILD_SHARED "Build shared library" ON)
option(LCL_BUILD_STATIC "Build static library" ON)
option(LCL_BUILD_CLI "Build command-line interpreter" ON)
option(LCL_BUILD_TESTS "Build test executable" OFF)
option(LCL_ENABLE_ASAN "Enable AddressSanitizer (debug builds)" OFF)

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
  src/lcl-proc.c
  src/lcl-program.c
  src/lcl-ref.c
  src/lcl-scan.c
  src/lcl-stdlib.c
  src/lcl-str.c
  src/lcl-string.c
  src/lcl-word.c
  src/str-compat.c
)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LCL_COMPILE_OPTIONS -Wall -Wextra)

if(LCL_ENABLE_ASAN)
  list(APPEND LCL_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer)
  set(LCL_LINK_OPTIONS -fsanitize=address,undefined)
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
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lcl_shared PRIVATE ${LCL_COMPILE_OPTIONS})
  if(LCL_ENABLE_ASAN)
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
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_options(lcl_static PRIVATE ${LCL_COMPILE_OPTIONS})
  if(LCL_ENABLE_ASAN)
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

  if(LCL_ENABLE_ASAN)
    target_link_options(lcl-cli PRIVATE ${LCL_LINK_OPTIONS})
  endif()
endif()

if(LCL_BUILD_TESTS)
  add_executable(lcl-test src/lcl-test.c)
  target_compile_definitions(lcl-test PRIVATE LCL_TEST DEBUG_REFC)
  target_compile_options(lcl-test PRIVATE ${LCL_COMPILE_OPTIONS})

  if(LCL_BUILD_STATIC)
    target_link_libraries(lcl-test PRIVATE lcl_static)
  else()
    target_link_libraries(lcl-test PRIVATE lcl_shared)
  endif()

  if(LCL_ENABLE_ASAN)
    target_link_options(lcl-test PRIVATE ${LCL_LINK_OPTIONS})
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

  install(FILES include/lcl.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(EXPORT lclTargets
    FILE lclTargets.cmake
    NAMESPACE lcl::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lcl
  )

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
