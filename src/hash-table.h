#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <limits.h>
#include <stdlib.h>

typedef struct lcl_value lcl_value;

/* Bugfix: 64-bit unsigned type for FNV-1a hashing without
 * `<stdint.h>` (C99). C89 doesn't guarantee any 64-bit integer:
 * `unsigned long` is only required to be ≥32 bits. On LP64 (Linux,
 * macOS) it's 64 bits — strict C89 works. On LLP64 (Windows MSVC x64)
 * it's 32, so we fall back to `unsigned long long` (C99, but a
 * near-universal compiler extension on every C89-mode compiler we
 * target: GCC, clang, MSVC ≥ VS2010). The compile-time size check in
 * hash-table.c enforces "exactly 64 bits" so a hypothetical wider
 * `unsigned long long` doesn't silently change FNV-1a's modular
 * truncation. */
#if ULONG_MAX > 0xFFFFFFFFUL
typedef unsigned long lcl_u64;
#define LCL_U64_C(x) x##UL
#else
typedef unsigned long long lcl_u64;
#define LCL_U64_C(x) x##ULL
#endif

enum { H_EMPTY = 0, H_FULL = 1, H_TOMB = 2 };

typedef struct {
  size_t i;
} hash_iter;

typedef struct {
  char *key;
  lcl_value *value;
  lcl_u64 hash;
  unsigned char state;
} hash_entry;

typedef struct {
  hash_entry *slots;
  size_t cap;
  size_t len;
  size_t used;
} hash_table;

hash_table *hash_table_new(void);
void hash_table_free(hash_table *ht);
void hash_table_clear(hash_table *ht);
int hash_table_put(hash_table *ht, const char *key, lcl_value *value);
int hash_table_get(hash_table *ht, const char *key, lcl_value **out);
lcl_value *hash_table_peek(hash_table *ht, const char *key);
int hash_table_delete(hash_table *ht, const char *key);
int hash_table_iterate(hash_table *ht, hash_iter *it, const char **key,
                       lcl_value **value);

/* Exposed for regression testing. The canonical 64-bit FNV-1a hash. */
lcl_u64 lcl_hash_fnv1a(const char *s);
#endif
