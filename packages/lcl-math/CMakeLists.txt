# lcl-math: Math functions for LCL
# Built as part of the LCL monorepo

set(LCL_MATH_SOURCES
  src/lcl-math.c
)

add_library(lcl_math STATIC ${LCL_MATH_SOURCES})

target_include_directories(lcl_math
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_math PRIVATE ${LCL_COMPILE_OPTIONS})
target_link_libraries(lcl_math PRIVATE lcl::lcl m)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_math
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-math.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
