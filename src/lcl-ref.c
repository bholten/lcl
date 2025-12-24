#include "lcl-values.h"

#ifdef DEBUG_REFC
#include <stdio.h>
#endif

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
  if (!value) return;
  if (--value->refc) return;

#ifdef DEBUG_REFC
  fprintf(stderr, "DEC %s rc = %d\n", value->str_repr, value->refc);
#endif

  free(value->str_repr);

  switch(value->type) {
  case LCL_LIST: {
    int i;

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
    /* Free upvalues */
    for (i = 0; i < p->nupvals; i++) {
      free(p->upvals[i].name);
      lcl_ref_dec(p->upvals[i].value);
    }
    free(p->upvals);
    lcl_ref_dec(p->params);
    lcl_ref_dec(p->captured_ns);
    lcl_program_free(p->body);
    free(p);
  } break;

  case LCL_NAMESPACE: {
    hash_table_free(value->as.namespace.namespace);
    free(value->as.namespace.qname);
  } break;

  case LCL_CPROC: {
    free(value->as.c_proc.fn);
  } break;

  case LCL_CELL: {
    lcl_ref_dec(value->as.cell.inner);
  } break;

  default:
    break;
  }

  free(value);
}
