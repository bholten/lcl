# lcl-crypto: OpenSSL crypto bindings for LCL
# Built as part of the LCL monorepo

find_package(OpenSSL REQUIRED)

set(LCL_CRYPTO_SOURCES
  src/lcl-crypto.c
)

add_library(lcl_crypto STATIC ${LCL_CRYPTO_SOURCES})

target_include_directories(lcl_crypto
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lcl_crypto PRIVATE -Wall -Wextra)

target_link_libraries(lcl_crypto
  PRIVATE lcl::lcl
  PUBLIC OpenSSL::SSL OpenSSL::Crypto
)

lcl_add_package_test(NAME lcl-crypto         COMMAND test/crypto_test.lcl)
lcl_add_package_test(NAME lcl-crypto-ecdsa   COMMAND test/ecdsa_test.lcl)
lcl_add_package_test(NAME lcl-crypto-signing COMMAND test/signing_test.lcl)

if(LCL_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(TARGETS lcl_crypto
    EXPORT lclTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(FILES include/lcl-crypto.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()
