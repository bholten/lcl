#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdlib.h>

/* Forward declaration - full definition in lcl-values.h */
typedef struct lcl_value lcl_value;

enum { H_EMPTY = 0, H_FULL = 1, H_TOMB = 2 };

typedef struct {
  size_t i;
} hash_iter;

typedef struct {
  char *key;
  lcl_value *value;
  unsigned long hash;
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
int hash_table_put(hash_table *ht, const char *key, lcl_value *value);
int hash_table_get(hash_table *ht, const char *key,
                   lcl_value **out);
int hash_table_delete(hash_table *ht, const char *key);
int hash_table_iterate(hash_table *ht, hash_iter *it,
                       const char **key, lcl_value **value);
#endif
