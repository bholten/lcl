#include <stdio.h>

#ifdef DEBUG_REFC
#endif

#include "hash-table.h"
#include "lcl-compile.h"

lcl_frame *lcl_frame_new(lcl_frame *parent) {
  lcl_frame *f = malloc(sizeof(*f));

  if (!f) return NULL;

  f->refc = 1;
  f->locals = hash_table_new();
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
  if (!f || !f->locals) return;

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
