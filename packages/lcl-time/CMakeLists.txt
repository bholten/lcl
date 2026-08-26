# lcl-time: Time functions for LCL
# Built as part of the LCL monorepo

set(LCL_TIME_SOURCES
  src/lcl-time.c
)

add_library(lcl_time STATIC ${LCL_TIME_SOURCES})

target_include_directories(lcl_time
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_time PRIVATE ${LCL_COMPILE_OPTIONS})
target_link_libraries(lcl_time PRIVATE lcl::lcl)

lcl_add_package_test(NAME lcl-time COMMAND test/test.lcl)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_time
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-time.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
