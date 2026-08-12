cmake_minimum_required(VERSION 3.16)
project(lcl-install-test C)

find_package(lcl REQUIRED)

# Core library via the lcl::lcl alias.
add_executable(verify-core verify-core.c)
target_link_libraries(verify-core PRIVATE lcl::lcl)

# A simple package (no external deps).
add_executable(verify-io verify-io.c)
target_link_libraries(verify-io PRIVATE lcl::lcl_io lcl::lcl)

# A platform-bound package (POSIX headers, no external libs).
add_executable(verify-posix verify-posix.c)
target_link_libraries(verify-posix PRIVATE lcl::lcl_posix lcl::lcl)

# A package with a transitive find_dependency() (OpenSSL).
add_executable(verify-crypto verify-crypto.c)
target_link_libraries(verify-crypto PRIVATE lcl::lcl_crypto lcl::lcl)
