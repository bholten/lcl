#include <memory.h>
#include <string.h>

#include "lcl-values.h"

static lcl_result ns_def_take(lcl_value *ns, const char *name, lcl_value *value) {
  if (!ns || ns->type != LCL_NAMESPACE) return LCL_ERROR;

  if (!hash_table_put(ns->as.namespace.namespace, name, value)) {
    return LCL_ERROR;
  }
  
  return LCL_OK;
}

lcl_value *lcl_ns_new(const char *qname) {
  hash_table *h;
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) return NULL;

  v->type = LCL_NAMESPACE;
  v->refc = 1;
  h = hash_table_new();

  if (!h) {
    return NULL;
  }

  v->as.namespace.namespace = h;

  if (qname) {
    size_t n = strlen(qname);
    v->str_repr = (char *)malloc(n + 1);

    if (!v->str_repr) {
      hash_table_free(h);
      free(v);
      return NULL;
    }

    memcpy(v->str_repr, qname, n + 1);
    v->as.namespace.qname = malloc(n + 1);
    memcpy(v->as.namespace.qname, qname, n + 1);
    /* v->as.namespace.qname = (char*)qname; */
  }

  return v;  
}

lcl_result lcl_ns_def(lcl_value *ns, const char *name, lcl_value *value) {
  lcl_result r = ns_def_take(ns, name, value);
  lcl_ref_dec(value);
  return r;
}

lcl_result lcl_ns_get(lcl_value *ns, const char *name, lcl_value **out) {
  if (!ns || ns->type != LCL_NAMESPACE) return LCL_ERROR;

  if (!hash_table_get(ns->as.namespace.namespace, name, out)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

const char *lcl_ns_split(const char *q, char *lhs, size_t nlhs, const char **rhs) {
  size_t n;
  const char *p = strstr(q, "::");

  if (!p) return NULL;

  n = (size_t)(p - q);

  if (n >= nlhs) {
    n = nlhs - 1;
  }

  memcpy(lhs, q, n);
  lhs[n] = '\0';

  *rhs = p + 2;

  return *rhs;
}

