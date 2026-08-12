# lcl-posix: POSIX filesystem/process-environment bindings for LCL
# Built as part of the LCL monorepo

set(LCL_POSIX_SOURCES
  src/lcl-posix.c
)

add_library(lcl_posix STATIC ${LCL_POSIX_SOURCES})

target_include_directories(lcl_posix
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_posix PRIVATE -Wall -Wextra)
target_link_libraries(lcl_posix PRIVATE lcl::lcl)

lcl_add_package_test(NAME lcl-posix COMMAND test/test.lcl)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_posix
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-posix.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
