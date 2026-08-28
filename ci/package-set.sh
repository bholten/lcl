#!/usr/bin/env bash
set -euo pipefail

mode="${1:-}"
set_name="${2:-}"

print_flags_core() {
    # `core` = core engine + pure-Lcl Test library. The Test library
    # has no system deps (it's embedded Lcl source); it's bundled with
    # `core` because every test suite we ship depends on `Test::suite`.
    # Without it, "core" would build but couldn't self-test.
    cat <<'EOF'
-DLCL_BUILD_TESTS=ON
-DLCL_BUILD_TEST_LIB=ON
EOF
}

print_flags_linux_full() {
    print_flags_core
    cat <<'EOF'
-DLCL_BUILD_IO=ON
-DLCL_BUILD_POSIX=ON
-DLCL_BUILD_PROCESS=ON
-DLCL_BUILD_REGEX=ON
-DLCL_BUILD_TIME=ON
-DLCL_BUILD_MATH=ON
-DLCL_BUILD_EXPECT=ON
-DLCL_BUILD_JSON=ON
-DLCL_BUILD_CURL=ON
-DLCL_BUILD_CRYPTO=ON
-DLCL_BUILD_SH_LIB=ON
-DLCL_BUILD_CURL_DSL_LIB=ON
-DLCL_BUILD_DOC_LIB=ON
-DLCL_BUILD_BENCH_LIB=ON
-DLCL_BUILD_EXAMPLES=ON
EOF
}

print_flags_macos_full() {
    # Currently identical to linux-full: every package's README claims
    # macOS support, and lcl-process already branches openpty linkage
    # on platform in CMake. Kept as a separate set so divergence (if
    # any feature ends up Linux-only) is a one-line change.
    print_flags_linux_full
}

print_apt_core() {
    # Build essentials are pre-installed on GitHub's ubuntu-latest.
    # Nothing extra needed for the core build.
    :
}

print_apt_linux_full() {
    # OpenSSL for lcl-crypto. lcl-curl and lcl-json pull libcurl and
    # cJSON sources via CMake FetchContent and do not need apt deps.
    # Embedded-library generation is pure CMake (see
    # cmake/EmbedFile.cmake) — no xxd required.
    cat <<'EOF'
libssl-dev
EOF
}

print_brew_core() {
    # Xcode CLT supplies a C toolchain on macos-latest runners.
    # Nothing extra needed for the core build.
    :
}

print_brew_macos_full() {
    # OpenSSL for lcl-crypto (Homebrew openssl is keg-only; the
    # workflow exports OPENSSL_ROOT_DIR so find_package can locate it).
    # libcurl and cJSON come in via FetchContent.
    cat <<'EOF'
openssl@3
EOF
}

case "$mode" in
    flags)
        case "$set_name" in
            core)        print_flags_core ;;
            linux-full)  print_flags_linux_full ;;
            macos-full)  print_flags_macos_full ;;
            *) echo "unknown package set: $set_name" >&2; exit 2 ;;
        esac
        ;;
    apt)
        case "$set_name" in
            core)        print_apt_core ;;
            linux-full)  print_apt_linux_full ;;
            *) echo "unknown package set: $set_name" >&2; exit 2 ;;
        esac
        ;;
    brew)
        case "$set_name" in
            core)        print_brew_core ;;
            macos-full)  print_brew_macos_full ;;
            *) echo "unknown package set: $set_name" >&2; exit 2 ;;
        esac
        ;;
    list)
        echo core
        echo linux-full
        echo macos-full
        ;;
    *)
        echo "usage: $0 {flags|apt|brew|list} [<set>]" >&2
        exit 2
        ;;
esac
