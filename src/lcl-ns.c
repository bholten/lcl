#include <memory.h>
#include <stdlib.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-values.h"

lcl_ns_anchor *lcl_ns_anchor_get(lcl_value *ns) {
  lcl_ns_anchor *a;

  if (!ns || ns->type != LCL_NAMESPACE) {
    return NULL;
  }

  a = (lcl_ns_anchor *)ns->as.namespace.anchor;

  if (a) {
    return a;
  }

  a = (lcl_ns_anchor *)calloc(1, sizeof(*a));

  if (!a) {
    return NULL;
  }

  a->refc = 1;
  a->target = ns;
  ns->as.namespace.anchor = a;

  return a;
}

lcl_ns_anchor *lcl_ns_anchor_ref(lcl_ns_anchor *a) {
  if (a) {
    a->refc++;
  }

  return a;
}

void lcl_ns_anchor_unref(lcl_ns_anchor *a) {
  if (a && --a->refc == 0) {
    free(a);
  }
}

lcl_result lcl_ns_def(lcl_value *ns, const char *name, lcl_value *value) {
  if (!ns || ns->type != LCL_NAMESPACE) {
    return LCL_ERROR;
  }

  if (!hash_table_put(ns->as.namespace.namespace, name, value)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

lcl_value *lcl_ns_new(const char *qname) {
  hash_table *h;
  lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

  if (!v) {
    return NULL;
  }

  v->type = LCL_NAMESPACE;
  v->refc = 1;
  h = hash_table_new();

  if (!h) {
    free(v);
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

    if (!v->as.namespace.qname) {
      free(v->str_repr);
      hash_table_free(h);
      free(v);
      return NULL;
    }

    memcpy(v->as.namespace.qname, qname, n + 1);
  }

  return v;
}

lcl_result lcl_ns_def_take(lcl_value *ns, const char *name, lcl_value *value) {
  lcl_result r = lcl_ns_def(ns, name, value);
  lcl_ref_dec(value);
  return r;
}

lcl_result lcl_ns_get(lcl_value *ns, const char *name, lcl_value **out) {
  if (!ns || ns->type != LCL_NAMESPACE) {
    return LCL_ERROR;
  }

  if (!hash_table_get(ns->as.namespace.namespace, name, out)) {
    return LCL_ERROR;
  }

  return LCL_OK;
}

lcl_value *lcl_ns_peek(const lcl_value *ns, const char *name) {
  if (!ns || ns->type != LCL_NAMESPACE || !name) {
    return NULL;
  }

  return hash_table_peek(ns->as.namespace.namespace, name);
}

const char *lcl_ns_split(const char *q, char *lhs, size_t nlhs,
                         const char **rhs) {
  size_t n;
  const char *p = strstr(q, "::");

  if (!p) {
    return NULL;
  }

  n = (size_t)(p - q);

  if (n >= nlhs) {
    return NULL; /* name too long for buffer */
  }

  memcpy(lhs, q, n);
  lhs[n] = '\0';

  *rhs = p + 2;

  return *rhs;
}

lcl_value *lcl_ns_from_dict(lcl_value *dict, const char *qname) {
  lcl_value *ns;
  hash_iter it = {0};
  const char *key;
  lcl_value *value;

  if (!dict || dict->type != LCL_DICT) {
    if (dict) {
      lcl_ref_dec(dict);
    }
    return NULL;
  }

  ns = lcl_ns_new(qname);
  if (!ns) {
    lcl_ref_dec(dict);
    return NULL;
  }

  while (hash_table_iterate(dict->as.dict.dictionary, &it, &key, &value)) {
    if (!hash_table_put(ns->as.namespace.namespace, key, value)) {
      lcl_ref_dec(value);
      lcl_ref_dec(ns);
      lcl_ref_dec(dict);
      return NULL;
    }

    /* bugfix: hash_table_put did its own ref_inc, balance the
       iterate */
    lcl_ref_dec(value);
  }

  lcl_ref_dec(dict);
  return ns;
}
