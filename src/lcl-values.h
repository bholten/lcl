#ifndef LCL_VALUES_H
#define LCL_VALUES_H

#include "lcl-compile.h"

typedef enum lcl_type {
  LCL_STRING,
  LCL_INT,
  LCL_FLOAT,
  LCL_LIST,
  LCL_DICT,
  LCL_CELL,
  LCL_PROC,
  LCL_CPROC,
  LCL_NAMESPACE,
  LCL_OPAQUE
} lcl_type;

typedef void (*lcl_finalizer)(void *ptr);

struct lcl_value {
  lcl_type type;
  int refc;
  char *str_repr;
  union {
    long i;
    double f;
    struct {
      lcl_value **items;
      size_t len;
      size_t cap;
    } list;
    struct {
      hash_table *dictionary;
    } dict;
    struct {
      lcl_value *inner;
    } cell;
    struct {
      lcl_proc *proc;
    } procedure;
    struct {
      hash_table *namespace;
      char *qname;
      void *anchor;
    } namespace;
    struct {
      lcl_c_func *fn;
    } c_proc;
    struct {
      void *ptr;
      const char *type_tag;
      lcl_finalizer finalizer;
    } opaque;
  } as;
};

typedef struct {
  size_t i;
} lcl_dict_it;

lcl_value *lcl_ref_inc(lcl_value *value);
void lcl_ref_dec(lcl_value *value);

lcl_value *lcl_string_new(const char *str);
const char *lcl_value_to_string(lcl_value *value);
lcl_result lcl_value_get_string(lcl_value *value, const char **out);
lcl_result lcl_value_to_cstring(lcl_interp *interp, lcl_value *value,
                                const char **out);

const char *lcl_type_name(lcl_type t);
/* malloc'd type-aware representation; NULL on OOM. */
char *lcl_value_repr(lcl_value *v);

lcl_value *lcl_int_new(const long n);
lcl_value *lcl_float_new(const double f);
lcl_result lcl_value_to_int(lcl_value *value, long *out);
lcl_result lcl_value_to_float(lcl_value *value, double *out);
lcl_result lcl_double_to_long(double f, long *out);
void lcl_normalize_decimal_to_c(char *buf);
size_t lcl_parse_double_c(const char *s, double *out);

typedef enum { LCL_NUM_NONE, LCL_NUM_INT, LCL_NUM_FLOAT } lcl_num_class;

lcl_num_class lcl_num_literal_classify(const char *s, size_t n);
lcl_num_class lcl_num_text_classify(const char *s, size_t n);

lcl_value *lcl_list_new(void);
lcl_result lcl_list_get(const lcl_value *list, size_t i, lcl_value **out);
lcl_value *lcl_list_peek(const lcl_value *list, size_t i);
lcl_value *lcl_value_alloc(void);
void lcl_stats_note_clone(int is_dict);
void lcl_stats_read(unsigned long *allocated, unsigned long *freed,
                    unsigned long *list_clones, unsigned long *dict_clones);

lcl_result lcl_list_push(lcl_value **list_io, lcl_value *value);
lcl_result lcl_list_pop(lcl_value **list_io, lcl_value **out);
lcl_result lcl_list_del(lcl_value **list_io, size_t i);
lcl_result lcl_list_set(lcl_value **list_io, size_t i, lcl_value *value);
size_t lcl_list_len(const lcl_value *list);

lcl_value *lcl_dict_new(void);
size_t lcl_dict_len(const lcl_value *dict);
lcl_result lcl_dict_get(const lcl_value *dict, const char *key,
                        lcl_value **out);
lcl_value *lcl_dict_peek(const lcl_value *dict, const char *key);
lcl_result lcl_dict_put(lcl_value **dict_io, const char *key, lcl_value *value);
lcl_result lcl_dict_del(lcl_value **dict_io, const char *key);
lcl_result lcl_dict_iter(const lcl_value **dict_io, lcl_dict_it *it,
                         const char **key, lcl_value **value);

lcl_value *lcl_cell_new(lcl_value *init);
lcl_result lcl_cell_get(lcl_value *cell, lcl_value **out);
lcl_value *lcl_cell_peek(const lcl_value *cell);
lcl_result lcl_cell_set(lcl_value *cell, lcl_value *v);
int lcl_cell_would_cycle(lcl_value *cell, lcl_value *value);
int lcl_value_would_cycle(lcl_value *container, lcl_value *value);
int lcl_value_cycle_explain(lcl_value *container, lcl_value *value, char *buf,
                            size_t len);

lcl_value *lcl_ns_new(const char *qname);
lcl_result lcl_ns_def(lcl_value *ns, const char *name, lcl_value *value);
lcl_result lcl_ns_def_take(lcl_value *ns, const char *name, lcl_value *value);
lcl_result lcl_ns_get(lcl_value *ns, const char *name, lcl_value **out);
lcl_value *lcl_ns_peek(const lcl_value *ns, const char *name);
const char *lcl_ns_split(const char *q, char *lhs, size_t nlhs,
                         const char **rhs);
lcl_value *lcl_ns_from_dict(lcl_value *dict, const char *qname);

const char *lcl_value_to_string(lcl_value *value);
lcl_value *lcl_value_new_string(const char *str);

lcl_value *lcl_proc_new(const char *self_name, lcl_upvalue *upvals, int nupvals,
                        lcl_param_spec *pspec, lcl_program *body,
                        const char *body_src, const char *file, int line);
lcl_return_code lcl_parse_params(lcl_interp *interp, const char *param_str,
                                 lcl_param_spec *pspec);

lcl_value *lcl_c_proc_new(const char *name, lcl_c_proc_fn fn);
lcl_value *lcl_c_spec_new(const char *name, lcl_c_spec_fn fn);

lcl_value *lcl_opaque_new(void *ptr, const char *type_tag,
                          lcl_finalizer finalizer);
lcl_result lcl_opaque_get(lcl_value *v, const char *expected_type, void **out);
const char *lcl_opaque_type(lcl_value *v);

#endif
