#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>

#include "hash-table.h"
#include "lcl-values.h"

static unsigned long fnv1a(const char *s) {
  unsigned long h = 1469598103934665603UL;

  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 1099511628211UL;
  }

  return h ? h : 1UL;
}

static size_t mask(const hash_table *ht) {
  return ht->cap - 1;
}

static ssize_t hash_find(hash_table *ht, const char *key,
                         unsigned long hk, size_t *first_tomb) {
  size_t m = mask(ht);
  size_t i = hk & m;
  *first_tomb = (size_t)-1;

  for (;;) {
    hash_entry *e = &ht->slots[i];

    if (e->state == H_EMPTY) {
      return (*first_tomb != (size_t)-1 ? (ssize_t)(*first_tomb) : (ssize_t)i);
    }

    if (e->state == H_TOMB) {
      if (*first_tomb == (size_t)-1) {
        *first_tomb = i;
      }
    } else if (e->hash == hk && strcmp(e->key, key) == 0) {
      return (ssize_t)i;
    }

    i = (i + 1) & m;
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
      size_t pos = (size_t)hash_find(ht, old[i].key, old[i].hash, &dummy);
      hash_entry *e = &ht->slots[(size_t)pos];

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

  if (!ht) return NULL;

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

  if (!ht) return;

  for (i = 0; i < ht->cap; i++) {
    hash_entry *e = &ht->slots[i];

    if (e->state == H_FULL) {
      lcl_ref_dec(e->value);
      free(e->key);
    }
    else if (e->state == H_TOMB) {
      free(e->key);
    }
  }

  free(ht->slots);
  free(ht);
}

int hash_table_put(hash_table *ht, const char *key, lcl_value *value) {
  unsigned long hk;
  size_t first_tomb;
  ssize_t idx;
  hash_entry *e;
  char *k;

  if ((ht->used + 1) * 10 >= ht->cap * 7) {
    if (!hash_rehash(ht, ht->cap ? ht->cap * 2 : 8)) {
      return 0;
    }
  }

  hk = fnv1a(key);
  idx = hash_find(ht, key, hk, &first_tomb);
  assert(idx >= 0 && (size_t)idx < ht->cap);
  e = &ht->slots[(size_t)idx];

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

  strcpy(k, key);
  e->state = H_FULL;
  e->hash = hk;
  e->key = k;
  e->value = lcl_ref_inc(value);

  ht->len++;
  ht->used++;

  return 1;
}

int hash_table_get(hash_table *ht, const char *key,
                       lcl_value **out) {
  unsigned long hk = fnv1a(key);
  size_t first_tomb;
  ssize_t idx = hash_find(ht, key, hk, &first_tomb);
  hash_entry *e = NULL;

  if (idx < 0) {
    return 0;
  }

  e = &ht->slots[(size_t)idx];

  if (e->state != H_FULL) {
    return 0;
  }

  *out = lcl_ref_inc(e->value);

  return 1;
}

int hash_table_delete(hash_table *ht, const char *key) {
  unsigned long hk = fnv1a(key);
  size_t first_tomb;
  ssize_t idx = hash_find(ht, key, hk, &first_tomb);
  hash_entry *e = NULL;

  if (idx < 0) {
    return 0;
  }

  e = &ht->slots[(size_t)idx];

  if (e->state != H_FULL) {
    return 0;
  }

  lcl_ref_dec(e->value);
  free(e->key);
  e->key = NULL;
  e->value = NULL;
  e->state = H_TOMB;

  ht->len--;
  /** TODO compact? **/
  return 1;
}

int hash_table_iterate(hash_table *ht, hash_iter *it,
                       const char **key, lcl_value **value) {
  size_t i = it->i;

  while (i < ht->cap) {
    hash_entry *e = &ht->slots[i++];

    if (e->state == H_FULL) {
      it->i =  i;
      *key = e->key;
      *value = lcl_ref_inc(e->value);
      return 1;
    }
  }

  return 0;
}
