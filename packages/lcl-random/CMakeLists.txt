# lcl-random: xoshiro128** random streams for LCL
# Built as part of the LCL monorepo

set(LCL_RANDOM_SOURCES
  src/lcl-random.c
)

add_library(lcl_random STATIC ${LCL_RANDOM_SOURCES})

target_include_directories(lcl_random
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_random PRIVATE -Wall -Wextra)
target_link_libraries(lcl_random PRIVATE lcl::lcl)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_random
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-random.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
