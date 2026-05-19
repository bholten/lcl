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
EOF
}

print_apt_core() {
    # Build essentials are pre-installed on GitHub's ubuntu-latest.
    # Nothing extra needed for the core build.
    :
}

print_apt_linux_full() {
    # OpenSSL for lcl-crypto. lcl-curl and lcl-json pull libcurl and
    # cJSON sources via CMake FetchContent and do not need apt deps.
    # xxd is needed for the embedded-library generation step.
    cat <<'EOF'
libssl-dev
xxd
EOF
}

case "$mode" in
    flags)
        case "$set_name" in
            core)        print_flags_core ;;
            linux-full)  print_flags_linux_full ;;
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
    list)
        echo core
        echo linux-full
        ;;
    *)
        echo "usage: $0 {flags|apt|list} [<set>]" >&2
        exit 2
        ;;
esac
