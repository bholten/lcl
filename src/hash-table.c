#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "hash-table.h"
#include "lcl-values.h"

/* Bugfix #53: `hash_find` uses `(size_t)-1` as the not-found sentinel
 * (matching the `first_tomb` convention already in this file) instead
 * of returning `ssize_t`. `ssize_t` is POSIX, not C89 — it broke
 * Windows-MSVC C89 builds via `<sys/types.h>`. `HASH_NPOS` names the
 * sentinel so the intent is obvious at every callsite. */
#define HASH_NPOS ((size_t)-1)

/* Bugfix #57: Compile-time check that `lcl_u64` is exactly 64 bits.
 * FNV-1a's hash output depends on modular truncation at 64 bits — a
 * wider type would produce different (still-valid but
 * non-interoperable) hashes. A zero/negative-sized array typedef
 * fails to compile if the condition is false. */
typedef char lcl_u64_size_check[sizeof(lcl_u64) == 8 ? 1 : -1];

/* Bugfix: Canonical 64-bit FNV-1a parameters (RFC draft
 * Eastlake/Hansen). The previous offset basis was missing a digit (19
 * digits vs. the canonical 20), and the type was `unsigned long`
 * which C89 only guarantees at ≥32 bits. Both are now fixed.
 * `LCL_U64_C` applies the right integer suffix for the active
 * `lcl_u64` definition (UL on LP64, ULL on LLP64). */
#define LCL_FNV64_OFFSET_BASIS LCL_U64_C(14695981039346656037)
#define LCL_FNV64_PRIME LCL_U64_C(1099511628211)

lcl_u64 lcl_hash_fnv1a(const char *s) {
  lcl_u64 h = LCL_FNV64_OFFSET_BASIS;

  while (*s) {
    h ^= (unsigned char)*s++;
    h *= LCL_FNV64_PRIME;
  }

  return h ? h : LCL_U64_C(1);
}

static size_t mask(const hash_table *ht) {
  return ht->cap - 1;
}

static size_t hash_find(hash_table *ht, const char *key, lcl_u64 hk,
                        size_t *first_tomb) {
  size_t m = mask(ht);
  size_t i = hk & m;
  size_t checked = 0;
  *first_tomb = HASH_NPOS;

  for (;;) {
    hash_entry *e = &ht->slots[i];

    if (e->state == H_EMPTY) {
      return (*first_tomb != HASH_NPOS ? *first_tomb : i);
    }

    if (e->state == H_TOMB) {
      if (*first_tomb == HASH_NPOS) {
        *first_tomb = i;
      }
    } else if (e->hash == hk && strcmp(e->key, key) == 0) {
      return i;
    }

    i = (i + 1) & m;

    if (++checked >= ht->cap) {
      return (*first_tomb != HASH_NPOS ? *first_tomb : HASH_NPOS);
    }
  }
}

static int hash_rehash(hash_table *ht, size_t newcap) {
  hash_entry *old = ht->slots;
  size_t oldcap = ht->cap;
  size_t i;

  hash_entry *slots = (hash_entry *)calloc(newcap, sizeof(*slots));

  if (!slots) {
    return 0;
  }

  ht->slots = slots;
  ht->cap = newcap;
  ht->len = 0;
  ht->used = 0;

  for (i = 0; i < oldcap; i++) {
    if (old[i].state == H_FULL) {
      size_t dummy;
      size_t pos = hash_find(ht, old[i].key, old[i].hash, &dummy);
      hash_entry *e = &ht->slots[pos];

      e->state = H_FULL;
      e->hash = old[i].hash;
      e->key = old[i].key;
      e->value = old[i].value;
      ht->len++;
      ht->used++;
    } else if (old[i].state == H_TOMB) {
      free(old[i].key);
    }
  }

  free(old);

  return 1;
}

hash_table *hash_table_new(void) {
  hash_table *ht = (hash_table *)calloc(1, sizeof(*ht));

  if (!ht) {
    return NULL;
  }

  ht->cap = 32;
  ht->len = 0;
  ht->used = 0;
  ht->slots = (hash_entry *)calloc(ht->cap, sizeof(*ht->slots));

  if (!ht->slots) {
    free(ht);
    return NULL;
  }

  return ht;
}

void hash_table_free(hash_table *ht) {
  size_t i;

  if (!ht) {
    return;
  }

  for (i = 0; i < ht->cap; i++) {
    hash_entry *e = &ht->slots[i];

    if (e->state == H_FULL) {
      lcl_ref_dec(e->value);
      free(e->key);
    } else if (e->state == H_TOMB) {
      free(e->key);
    }
  }

  free(ht->slots);
  free(ht);
}

void hash_table_clear(hash_table *ht) {
  size_t i;

  if (!ht) {
    return;
  }

  for (i = 0; i < ht->cap; i++) {
    hash_entry *e = &ht->slots[i];

    if (e->state == H_FULL) {
      lcl_ref_dec(e->value);
      free(e->key);
    } else if (e->state == H_TOMB) {
      free(e->key);
    }

    e->key = NULL;
    e->value = NULL;
    e->hash = 0;
    e->state = H_EMPTY;
  }

  ht->len = 0;
  ht->used = 0;
}

int hash_table_put(hash_table *ht, const char *key, lcl_value *value) {
  lcl_u64 hk;
  size_t first_tomb;
  size_t idx;
  hash_entry *e;
  char *k;

  if ((ht->used + 1) * 10 >= ht->cap * 7) {
    if (!hash_rehash(ht, ht->cap ? ht->cap * 2 : 8)) {
      return 0;
    }
  }

  hk = lcl_hash_fnv1a(key);
  idx = hash_find(ht, key, hk, &first_tomb);
  /* After rehash, the load factor is <0.7, so `hash_find` must locate
   * either an empty slot or a tombstone (never the cap-exhausted
   * sentinel). */
  assert(idx != HASH_NPOS && idx < ht->cap);
  e = &ht->slots[idx];

  if (e->state == H_FULL) {
    lcl_ref_inc(value);
    lcl_ref_dec(e->value);
    e->value = value;
    return 1;
  }

  k = (char *)malloc(strlen(key) + 1);

  if (!k) {
    return 0;
  }

  /* Bugfix: A tombstone already counts toward `used`; reusing it must
   * not double-count. Only `H_EMPTY` slots contribute a new `used`
   * slot. */
  {
    int was_empty = (e->state == H_EMPTY);
    strcpy(k, key);
    e->state = H_FULL;
    e->hash = hk;
    e->key = k;
    e->value = lcl_ref_inc(value);

    ht->len++;
    if (was_empty) {
      ht->used++;
    }
  }

  return 1;
}

int hash_table_get(hash_table *ht, const char *key, lcl_value **out) {
  lcl_u64 hk = lcl_hash_fnv1a(key);
  size_t first_tomb;
  size_t idx = hash_find(ht, key, hk, &first_tomb);
  hash_entry *e = NULL;

  if (idx == HASH_NPOS) {
    return 0;
  }

  e = &ht->slots[idx];

  if (e->state != H_FULL) {
    return 0;
  }

  *out = lcl_ref_inc(e->value);

  return 1;
}

int hash_table_delete(hash_table *ht, const char *key) {
  lcl_u64 hk = lcl_hash_fnv1a(key);
  size_t first_tomb;
  size_t idx = hash_find(ht, key, hk, &first_tomb);
  hash_entry *e = NULL;

  if (idx == HASH_NPOS) {
    return 0;
  }

  e = &ht->slots[idx];

  if (e->state != H_FULL) {
    return 0;
  }

  lcl_ref_dec(e->value);
  free(e->key);
  e->key = NULL;
  e->value = NULL;
  e->state = H_TOMB;

  ht->len--;

  /* Compact if tombstones dominate: more tombstones than live entries */
  if (ht->len > 0 && (ht->used - ht->len) > ht->len) {
    hash_rehash(ht, ht->cap);
  }

  return 1;
}

int hash_table_iterate(hash_table *ht, hash_iter *it, const char **key,
                       lcl_value **value) {
  size_t i = it->i;

  while (i < ht->cap) {
    hash_entry *e = &ht->slots[i++];

    if (e->state == H_FULL) {
      it->i = i;
      *key = e->key;
      *value = lcl_ref_inc(e->value);
      return 1;
    }
  }

  return 0;
}
