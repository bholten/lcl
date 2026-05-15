#include "hash-table.h"
#include "lcl-compile.h"
#include "lcl-values.h"

typedef struct {
  lcl_value **cells;
  int count;
  int cap;
} cell_set;

static void cell_set_init(cell_set *s) {
  s->cells = NULL;
  s->count = 0;
  s->cap = 0;
}

static void cell_set_free(cell_set *s) {
  free(s->cells);
}

static int cell_set_contains(cell_set *s, lcl_value *cell) {
  int i;
  for (i = 0; i < s->count; i++) {
    if (s->cells[i] == cell) {
      return 1;
    }
  }
  return 0;
}

static int cell_set_add(cell_set *s, lcl_value *cell) {
  if (cell_set_contains(s, cell)) {
    return 1;
  }

  if (s->count >= s->cap) {
    int newcap = s->cap ? s->cap * 2 : 8;
    lcl_value **newcells =
        realloc(s->cells, (size_t)newcap * sizeof(lcl_value *));

    if (!newcells) {
      return 0;
    }

    s->cells = newcells;
    s->cap = newcap;
  }

  s->cells[s->count++] = cell;
  return 1;
}

static int cycle_check_value(lcl_value *target, lcl_value *val,
                             cell_set *visited);

static int cycle_check_proc(lcl_value *target, lcl_proc *proc,
                            cell_set *visited) {
  int i;

  if (!proc || !proc->upvals) {
    return 0;
  }

  for (i = 0; i < proc->nupvals; i++) {
    lcl_upvalue *uv = &proc->upvals[i];

    if (uv->is_cell && uv->value) {
      lcl_value *cell = uv->value;

      if (cell == target) {
        return 1;
      }

      if (cell_set_contains(visited, cell)) {
        continue;
      }

      if (!cell_set_add(visited, cell)) {
        continue;
      }

      if (cell->type == LCL_CELL && cell->as.cell.inner) {
        if (cycle_check_value(target, cell->as.cell.inner, visited)) {
          return 1;
        }
      }
    }
  }

  return 0;
}

static int cycle_check_value(lcl_value *target, lcl_value *val,
                             cell_set *visited) {
  if (!val) {
    return 0;
  }

  switch (val->type) {
  case LCL_PROC:
    return cycle_check_proc(target, val->as.procedure.proc, visited);

  case LCL_LIST: {
    /* Bugfix: A proc capturing `target` may be hidden inside any list
       element. */
    size_t i;

    for (i = 0; i < val->as.list.len; i++) {
      if (cycle_check_value(target, val->as.list.items[i], visited)) {
        return 1;
      }
    }

    return 0;
  }

  case LCL_DICT: {
    hash_iter it;
    const char *k;
    lcl_value *v;

    it.i = 0;

    while (hash_table_iterate(val->as.dict.dictionary, &it, &k, &v)) {
      int found = cycle_check_value(target, v, visited);
      lcl_ref_dec(v); /* hash_table_iterate increments refcount */

      if (found) {
        return 1;
      }
    }

    return 0;
  }

  case LCL_CELL:
    if (val == target) {
      return 1;
    }

    if (cell_set_contains(visited, val)) {
      return 0;
    }

    if (!cell_set_add(visited, val)) {
      return 0;
    }

    return cycle_check_value(target, val->as.cell.inner, visited);

  default:
    return 0;
  }
}

int lcl_cell_would_cycle(lcl_value *cell, lcl_value *value) {
  cell_set visited;
  int result;

  if (!cell || cell->type != LCL_CELL || !value) {
    return 0;
  }

  /* Bugfix: The value can be any container — a proc that closes over
   * `cell` may be stored inside a list, dict, or nested cell. Walk
   * the value graph looking for one. */
  cell_set_init(&visited);
  result = cycle_check_value(cell, value, &visited);
  cell_set_free(&visited);

  return result;
}

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
