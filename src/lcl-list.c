#include "lcl-values.h"

lcl_value *lcl_list_new(void) {
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

  v->type = LCL_LIST;
  v->refc = 1;

  return v;
}

size_t lcl_list_len(const lcl_value *list) {
  if (list && list->type == LCL_LIST) {
    return list->as.list.len;
  }

  return 0;
}

lcl_result lcl_list_get(const lcl_value *list, size_t i, lcl_value **out) {
  if (!list || list->type != LCL_LIST || !out) return LCL_ERROR;
  if (i >= (size_t)list->as.list.len) return LCL_ERROR;

  *out = lcl_ref_inc(list->as.list.items[i]);

  return LCL_OK;
}

static lcl_result lcl_list_ensure_cap(lcl_value *list, size_t need) {
  size_t newcap;
  lcl_value **newitems;

  if (list->type != LCL_LIST) return LCL_ERROR;
  if ((size_t)list->as.list.cap >= need) return LCL_OK;

  newcap = list->as.list.cap ? list->as.list.cap * 2 : 4;

  while (newcap < need) {
    newcap *= 2;
  }

  newitems = (lcl_value **)realloc(list->as.list.items, newcap * sizeof(*newitems));

  if (!newitems) return LCL_ERROR;

  list->as.list.items = newitems;
  list->as.list.cap = newcap;
  
  return LCL_OK;
}

static lcl_value *lcl_list_clone_shallow(lcl_value *src) {
  lcl_value *dest;
  size_t n;

  if (!src || src->type != LCL_LIST) return NULL;

  dest = lcl_list_new();
  n = src->as.list.len;

  if (n) {
    size_t i = 0;

    dest->as.list.items = malloc(n * sizeof(*dest->as.list.items));
    dest->as.list.cap = n;
    dest->as.list.len = n;

    for (i = 0; i < n; i++) {
      dest->as.list.items[i] = lcl_ref_inc(src->as.list.items[i]);
    }
  }

  return dest;
}

lcl_result lcl_list_push(lcl_value **list_io, lcl_value *value) {
  lcl_value *list = *list_io;

  if (!list || list->type != LCL_LIST) return LCL_ERROR;

  if (list->refc > 1) {
    lcl_value *dup = lcl_list_clone_shallow(list);
    lcl_ref_dec(list);
    *list_io = list = dup;
  }

  if (lcl_list_ensure_cap(list, list->as.list.len + 1) != LCL_OK) {
    return LCL_ERROR;
  }

  list->as.list.items[list->as.list.len++] = lcl_ref_inc(value);
  free(list->str_repr);
  list->str_repr = NULL;

  return LCL_OK;
}

lcl_result lcl_list_set(lcl_value **list_io, size_t i, lcl_value *value) {
  lcl_value *list = *list_io;

  if (!list || list->type != LCL_LIST) return LCL_ERROR;
  if (i >= (size_t)list->as.list.len) return LCL_ERROR;

  /* Copy-on-write */
  if (list->refc > 1) {
    lcl_value *dup = lcl_list_clone_shallow(list);
    lcl_ref_dec(list);
    *list_io = list = dup;
  }

  lcl_ref_dec(list->as.list.items[i]);
  list->as.list.items[i] = lcl_ref_inc(value);
  free(list->str_repr);
  list->str_repr = NULL;

  return LCL_OK;
}
