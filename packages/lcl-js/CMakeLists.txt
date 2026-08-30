# lcl-js: the JavaScript host engine as `Js::` (Emscripten only)
# Built as part of the LCL monorepo

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "lcl-js binds the JavaScript host: configure with emcmake (LCL_BUILD_JS needs Emscripten)")
endif()

set(LCL_JS_SOURCES
  src/lcl-js.c
)

add_library(lcl_js STATIC ${LCL_JS_SOURCES})

target_include_directories(lcl_js
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_js PRIVATE ${LCL_COMPILE_OPTIONS})
target_link_libraries(lcl_js PRIVATE lcl::lcl)

# The JavaScript half rides on every executable that links this
# library; lcl_int is a wasm i64, so the bridge needs BigInt at the
# boundary.
set(LCL_JS_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/src/lcl-js-library.js")
target_link_options(lcl_js INTERFACE
  "SHELL:--js-library ${LCL_JS_LIBRARY}"
  -sWASM_BIGINT=1)
set_property(TARGET lcl_js APPEND PROPERTY INTERFACE_LINK_DEPENDS "$<BUILD_INTERFACE:${LCL_JS_LIBRARY}>")

lcl_add_package_test(NAME lcl-js COMMAND test/test.lcl)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_js
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-js.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
  install(FILES src/lcl-js-library.js
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/lcl
  )
endif()
