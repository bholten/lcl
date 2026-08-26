# lcl-expect: Expect-style automation for LCL
# Built as part of the LCL monorepo

set(LCL_EXPECT_SOURCES
  src/lcl-expect.c
)

add_library(lcl_expect STATIC ${LCL_EXPECT_SOURCES})

target_include_directories(lcl_expect
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_expect PRIVATE ${LCL_COMPILE_OPTIONS})
target_link_libraries(lcl_expect PRIVATE lcl::lcl lcl_process)

lcl_add_package_test(NAME lcl-expect COMMAND test/test.lcl)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_expect
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-expect.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
