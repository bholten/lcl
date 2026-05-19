# examples/CMakeLists.txt — opt-in builds of the embedding samples.
#
# Built when LCL_BUILD_EXAMPLES=ON at the top level. Each example
# links against the lcl::lcl alias (resolves to lcl_static if built,
# else lcl_shared) and is registered as a ctest entry when
# LCL_BUILD_TESTS is also on — so the examples are not just
# illustrative but actually exercised by CI.

add_executable(embed_example embed_example.c)
target_link_libraries(embed_example PRIVATE lcl::lcl)
target_compile_options(embed_example PRIVATE ${LCL_COMPILE_OPTIONS})

if(LCL_ENABLE_ASAN)
    target_link_options(embed_example PRIVATE ${LCL_LINK_OPTIONS})
endif()

if(LCL_BUILD_TESTS)
    add_test(NAME embed_example COMMAND embed_example)
    # Pin to a specific line of the example's stdout so a regression
    # in the eval path (e.g. arity check, int coercion, script-frame
    # binding) is caught here, not just at compile time.
    # square 9 == 81 with the example's mul implementation.
    set_tests_properties(embed_example PROPERTIES
        PASS_REGULAR_EXPRESSION "script returned: 81")
endif()
