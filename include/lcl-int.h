/*
 * lcl-int.h -- fixed-width integers for Lcl.
 *
 * Installed alongside lcl.h, which includes it; it stands alone so
 * the interpreter's internal headers (which mirror lcl.h's types
 * rather than include it) can share the one definition.
 */

#ifndef LCL_INT_H
#define LCL_INT_H

#include <limits.h>
#include <stddef.h>

/*
 * C89 guarantees `long` only 32 bits, and the hosts Lcl runs on
 * disagree: LP64 (Linux, macOS, the BSDs) makes it 64, ILP32/LLP64
 * (wasm32, Win64) 32. The language does not: an Lcl integer has the
 * range -2^63 .. 2^63-1 on every host. `lcl_i64`/`lcl_u64` are the
 * exact 64-bit storage types that implement that, chosen once here
 * for the whole tree; `lcl_int` is the language's integer, defined in
 * terms of them, and deliberately independent of the host's pointer,
 * `long` and `size_t` widths. On LP64 `lcl_int` *is* `long`, so
 * existing call sites compile unchanged; elsewhere the `long long`
 * extension is fenced (`__extension__` keeps -Wpedantic quiet).
 * ============================================================================
 */

#if defined(__GNUC__)
#define LCL_EXTENSION __extension__
#else
#define LCL_EXTENSION
#endif

#if LONG_MAX > 0x7FFFFFFFL
typedef long lcl_i64;
typedef unsigned long lcl_u64;
#define LCL_I64_C(x) x##L
#define LCL_U64_C(x) x##UL
#else
LCL_EXTENSION typedef long long lcl_i64;
LCL_EXTENSION typedef unsigned long long lcl_u64;
#define LCL_I64_C(x) (LCL_EXTENSION x##LL)
#define LCL_U64_C(x) (LCL_EXTENSION x##ULL)
#endif

#define LCL_I64_MAX LCL_I64_C(9223372036854775807)
#define LCL_I64_MIN (-LCL_I64_MAX - 1)
#define LCL_U64_MAX LCL_U64_C(18446744073709551615)

/* Exactly 64 bits, or the build stops here. */
typedef char lcl_i64_must_be_8_bytes[sizeof(lcl_i64) == 8 ? 1 : -1];
typedef char lcl_u64_must_be_8_bytes[sizeof(lcl_u64) == 8 ? 1 : -1];

/* The Lcl integer: the language's integer domain. */
typedef lcl_i64 lcl_int;
#define LCL_INT_MAX LCL_I64_MAX
#define LCL_INT_MIN LCL_I64_MIN

/* Bytes that hold any lcl_int in decimal, sign and NUL included. */
#define LCL_INT_STRLEN 21

#endif /* LCL_INT_H */
