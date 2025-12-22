#include "lcl-compile.h"
#include "lcl-values.h"

lcl_value *lcl_cell_new(lcl_value *init) {
  lcl_value *c = (lcl_value *)calloc(1, sizeof(*c));

  if (!c) {
    return NULL;
  }

  c->type = LCL_CELL;
  c->refc = 1;
  c->as.cell.inner = lcl_ref_inc(init);

  return c;
}

lcl_result lcl_cell_get(lcl_value *cell, lcl_value **out) {
  if (!cell || cell->type != LCL_CELL || !out) {
    return LCL_ERROR;
  }

  *out = lcl_ref_inc(cell->as.cell.inner);

  return LCL_OK;
}

lcl_result lcl_cell_set(lcl_value *cell, lcl_value *v) {
  if (!cell || cell->type != LCL_CELL || !v) {
    return LCL_ERROR;
  }

  {  
    lcl_value *old = cell->as.cell.inner;
    cell->as.cell.inner = lcl_ref_inc(v);
    lcl_ref_dec(old);

    if (cell->str_repr) {
      free(cell->str_repr);
      cell->str_repr = NULL;
    }
  }
  
  return LCL_OK;
}
