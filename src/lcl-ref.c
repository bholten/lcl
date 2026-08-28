#include <stdlib.h>

#include "lcl-compile.h"
#include "lcl-values.h"

#ifdef DEBUG_REFC
#include <stdio.h>
#endif

static unsigned long stats_values_allocated;
static unsigned long stats_values_freed;
static unsigned long stats_list_clones;
static unsigned long stats_dict_clones;

lcl_value *lcl_value_alloc(void) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (v) {
    stats_values_allocated++;
  }

  return v;
}

void lcl_stats_note_clone(int is_dict) {
  if (is_dict) {
    stats_dict_clones++;
  } else {
    stats_list_clones++;
  }
}

void lcl_stats_read(unsigned long *allocated, unsigned long *freed,
                    unsigned long *list_clones, unsigned long *dict_clones) {
  *allocated = stats_values_allocated;
  *freed = stats_values_freed;
  *list_clones = stats_list_clones;
  *dict_clones = stats_dict_clones;
}

lcl_value *lcl_ref_inc(lcl_value *value) {
  if (value) {
    value->refc++;
#ifdef DEBUG_REFC
    fprintf(stderr, "INC %s rc = %d\n", value->str_repr, value->refc);
#endif
  }

  return value;
}

void lcl_ref_dec(lcl_value *value) {
  if (!value) {
    return;
  }

  if (--value->refc) {
    return;
  }

#ifdef DEBUG_REFC
  fprintf(stderr, "DEC %s rc = %d\n", value->str_repr, value->refc);
#endif

  stats_values_freed++;
  free(value->str_repr);

  switch (value->type) {
  case LCL_LIST: {
    size_t i;

    for (i = 0; i < value->as.list.len; i++) {
      lcl_ref_dec(value->as.list.items[i]);
    }

    free(value->as.list.items);
  } break;

  case LCL_DICT: {
    hash_table_free(value->as.dict.dictionary);
  } break;

  case LCL_PROC: {
    lcl_proc *p = value->as.procedure.proc;
    int i;

    for (i = 0; i < p->nupvals; i++) {
      free(p->upvals[i].name);
      lcl_ref_dec(p->upvals[i].value);
      lcl_ns_anchor_unref(p->upvals[i].anchor);
    }

    free(p->upvals);

    for (i = 0; i < p->nhome; i++) {
      lcl_ns_anchor_unref(p->home[i]);
    }

    free(p->home);
    free(p->self_name);
    free(p->file);
    free(p->body_src);
    lcl_param_spec_free(&p->pspec);
    lcl_program_free(p->body);
    free(p);
  } break;

  case LCL_NAMESPACE: {
    lcl_ns_anchor *a = (lcl_ns_anchor *)value->as.namespace.anchor;

    if (a) {
      a->target = NULL; /* surviving home references fail cleanly */
      lcl_ns_anchor_unref(a);
    }

    hash_table_free(value->as.namespace.namespace);
    free(value->as.namespace.qname);
  } break;

  case LCL_CPROC: {
    free(value->as.c_proc.fn);
  } break;

  case LCL_CELL: {
    lcl_ref_dec(value->as.cell.inner);
  } break;

  case LCL_OPAQUE: {
    if (value->as.opaque.finalizer && value->as.opaque.ptr) {
      value->as.opaque.finalizer(value->as.opaque.ptr);
    }
    free((void *)value->as.opaque.type_tag);
  } break;

  default: break;
  }

  free(value);
}
