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
  LCL_NAMESPACE
} lcl_type;


typedef int (*lcl_cproc)(lcl_frame *env,
                         int argc,
                         lcl_value **argv,
                         lcl_value **out);


struct lcl_value {
  lcl_type type;
  int refc;
  char *str_repr;
  union {
    long i;
    double f;
    struct {
      lcl_value **items;
      int len;
      int cap;
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
    } namespace;
    struct {
      lcl_c_func *fn;
    } c_proc;
  } as;
};

typedef struct {
  size_t i;
} lcl_dict_it;

lcl_value *lcl_ref_inc(lcl_value *value);
void lcl_ref_dec(lcl_value *value);

lcl_value *lcl_string_new(const char *str);
const char *lcl_value_to_string(lcl_value *value);

lcl_value *lcl_int_new(const long n);
lcl_value *lcl_float_new(const float f);
lcl_result lcl_value_to_int(lcl_value *value, long *out);
lcl_result lcl_value_to_float(lcl_value *value, float *out);

lcl_value *lcl_list_new(void);
lcl_result lcl_list_get(const lcl_value *list, size_t i, lcl_value **out);
lcl_result lcl_list_push(lcl_value **list_io, lcl_value *value);
size_t lcl_list_len(const lcl_value *list);

lcl_value *lcl_dict_new(void);
size_t lcl_dict_len(const lcl_value *dict);
lcl_result lcl_dict_get(const lcl_value *dict, const char *key,
                        lcl_value **out);
lcl_result lcl_dict_put(lcl_value **dict_io, const char *key,
                        lcl_value *value);
lcl_result lcl_dict_del(lcl_value **dict_io, const char *key);
lcl_result lcl_dict_iter(const lcl_value **dict_io, lcl_dict_it *it, const char **key,
                         lcl_value **value);

lcl_value *lcl_cell_new(lcl_value *init);
lcl_result lcl_cell_get(lcl_value *cell, lcl_value **out);
lcl_result lcl_cell_set(lcl_value *cell, lcl_value *v);

lcl_value *lcl_ns_new(const char *qname);
lcl_result lcl_ns_def(lcl_value *ns, const char *name, lcl_value *value);
lcl_result lcl_ns_get(lcl_value *ns, const char *name, lcl_value **out);
const char *lcl_ns_split(const char *q, char *lhs, size_t nlhs, const char **rhs);

const char *lcl_value_to_string(lcl_value *value);
lcl_value *lcl_value_new_string(const char *str);

/* lcl_value *lcl_proc_new(lcl_proc *p); */
lcl_value *lcl_proc_new(lcl_frame *closure, lcl_value *params, lcl_program *body);

lcl_value *lcl_c_proc_new(const char *name, lcl_c_proc_fn fn);
lcl_value *lcl_c_spec_new(const char *name, lcl_c_spec_fn fn);

#endif
