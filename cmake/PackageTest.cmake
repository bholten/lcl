# PackageTest.cmake — register a per-package script test with CTest.
#
# Each Lcl package ships a `test/*.lcl` file that exercises its
# script-level API via the embedded Test framework. This module
# wraps the `add_test()` boilerplate so packages add a single line.
#
# Usage from a package's CMakeLists.txt:
#
#   lcl_add_package_test(NAME lcl-io COMMAND test/test.lcl)
#
# `NAME` is what shows up in `ctest -V` output. `COMMAND` is the
# path to the .lcl file, relative to the package's source directory.
# The test runs through the `lcl-cli` executable and inherits
# whichever packages the CLI was compiled with — so the script tests
# transitively require LCL_BUILD_IO=ON (for `puts`) and
# LCL_BUILD_TEST_LIB=ON (for `Test::suite`). That implicit dependency
# is expected; see the linux-full CI matrix row.
#
# No-ops if either LCL_BUILD_TESTS or LCL_BUILD_CLI is off, so it's
# always safe to call.

function(lcl_add_package_test)
    set(_options "")
    set(_one_value NAME COMMAND)
    set(_multi_value "")
    cmake_parse_arguments(PT "${_options}" "${_one_value}"
                          "${_multi_value}" ${ARGN})

    if(NOT PT_NAME OR NOT PT_COMMAND)
        message(FATAL_ERROR
            "lcl_add_package_test: NAME and COMMAND are required")
    endif()

    if(NOT LCL_BUILD_TESTS)
        return()
    endif()

    if(NOT LCL_BUILD_CLI)
        message(WARNING
            "lcl_add_package_test(${PT_NAME}) skipped: "
            "LCL_BUILD_CLI is OFF")
        return()
    endif()

    add_test(
        NAME ${PT_NAME}
        COMMAND $<TARGET_FILE:lcl-cli>
                "${CMAKE_CURRENT_SOURCE_DIR}/${PT_COMMAND}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endfunction()
