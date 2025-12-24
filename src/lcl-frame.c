#include <stdio.h>

#ifdef DEBUG_REFC
#endif

#include "hash-table.h"
#include "lcl-compile.h"
#include "lcl-values.h"

lcl_frame *lcl_frame_new(lcl_frame *parent) {
  lcl_frame *f = malloc(sizeof(*f));
  hash_table *locals;

  if (!f) return NULL;

  locals = hash_table_new();
  if (!locals) {
    free(f);
    return NULL;
  }

  f->refc = 1;
  f->locals = locals;
  f->parent = lcl_frame_ref_inc(parent);
  f->owns_locals = 1;

  return f;
}

lcl_frame *lcl_frame_new_ns(lcl_frame *parent, hash_table *ns_locals) {
  lcl_frame *f = malloc(sizeof(*f));

  if (!f) return NULL;

  f->refc = 1;
  f->locals = ns_locals;  /* Borrowed from namespace */
  f->parent = lcl_frame_ref_inc(parent);
  f->owns_locals = 0;

  return f;
}

void lcl_frame_free(lcl_frame *f) {
  if (!f) return;

  if (f->parent) {
    lcl_frame_ref_dec(f->parent);
  }

  if (f->locals && f->owns_locals) {
    hash_table_free(f->locals);
  }

  free(f);
}

lcl_frame *lcl_frame_ref_inc(lcl_frame *f) {
  if (f) {
    f->refc++;    
#ifdef DEBUG_REFC
    fprintf(stderr, "INC FRAME REF rc = %d\n", f->refc);
#endif
  }
  
  return f;
}

void lcl_frame_ref_dec(lcl_frame *f) {
  if (!f) return;

  if (--f->refc) return;

  lcl_frame_free(f);
}

void lcl_frame_clear(lcl_frame *f) {
  hash_iter it = {0};
  const char *key;
  lcl_value *val;

  if (!f || !f->locals) return;

  /* Break reference cycles through cells before freeing.
   * This handles mutual recursion cases where:
   * cell A -> lambda A -> upvalues -> cell B -> lambda B -> upvalues -> cell A
   * By clearing cell contents first, we break the cycle.
   */
  while (hash_table_iterate(f->locals, &it, &key, &val)) {
    if (val->type == LCL_CELL && val->as.cell.inner) {
      lcl_ref_dec(val->as.cell.inner);
      val->as.cell.inner = NULL;
    }
    lcl_ref_dec(val); /* Balance the incref from hash_table_iterate */
  }

  hash_table_free(f->locals);
  f->locals = NULL;
}

int lcl_frame_get_binding(lcl_frame *f, const char *name, lcl_value **out) {
  while (f) {
    if (hash_table_get(f->locals, name, out)) {
      return 1;
    }

    f = f->parent;
  }

  return 0;
}
