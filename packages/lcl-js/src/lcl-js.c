/*
 * lcl-js: the JavaScript host engine, as `Js::`.
 *
 * This file is the C89 half of the binding: it registers the
 * namespace and forwards every operation to lcl_js_op, which is
 * implemented in JavaScript (src/lcl-js-library.js, linked with
 * --js-library). JavaScript objects reach Lcl as opaque values tagged
 * "Js::ref" that hold a handle into the library's table; the opaque's
 * finalizer releases the handle, so a JS object lives as long as some
 * Lcl value refers to it. The library reaches back into Lcl through
 * the lcl_js_* functions below and the public value API.
 *
 * No Emscripten headers: the JavaScript side is declared extern and
 * resolved at link time, which keeps this file under the same
 * -std=c90 -Wpedantic flags as the rest of the tree.
 */

#include <stdlib.h>

#include <lcl-js.h>

extern int lcl_js_op(lcl_interp *interp, int op, int argc, lcl_value **argv,
                     lcl_value **out);
extern void lcl_js_ref_release(int id);
extern void lcl_js_interp_open(lcl_interp *interp);
extern void lcl_js_interp_closed(lcl_interp *interp);

enum {
  LCL_JS_GLOBAL,
  LCL_JS_GET,
  LCL_JS_SET,
  LCL_JS_DEL,
  LCL_JS_CALL,
  LCL_JS_INVOKE,
  LCL_JS_NEW,
  LCL_JS_EVAL,
  LCL_JS_FN,
  LCL_JS_TYPEOF,
  LCL_JS_TO_LIST,
  LCL_JS_TO_DICT,
  LCL_JS_RELEASE,
  LCL_JS_OBJECT,
  LCL_JS_ARRAY
};

#define LCL_JS_REF_TAG "Js::ref"

static void ref_finalizer(void *p) {
  lcl_js_ref_release((int)(size_t)p);
}

lcl_value *lcl_js_ref_new(int id) {
  return lcl_opaque_new((void *)(size_t)id, LCL_JS_REF_TAG, ref_finalizer);
}

int lcl_js_ref_id(lcl_value *value) {
  void *p;

  if (lcl_opaque_get(value, LCL_JS_REF_TAG, &p) != LCL_OK) {
    return -1;
  }

  return (int)(size_t)p;
}

lcl_value *lcl_js_call_proc(lcl_interp *interp, lcl_value *proc,
                            lcl_value *args) {
  lcl_value *result = NULL;
  lcl_value **argv = NULL;
  size_t n = lcl_list_len(args);
  size_t i;
  lcl_return_code rc;

  if (!lcl_is_callable(proc)) {
    lcl_set_error(interp, "Js: value is not callable");
    return NULL;
  }

  if (n > 0) {
    argv = (lcl_value **)calloc(n, sizeof *argv);

    if (!argv) {
      lcl_set_error(interp, "Js: out of memory");
      return NULL;
    }

    for (i = 0; i < n; i++) {
      argv[i] = lcl_list_peek(args, i);
    }
  }

  rc = lcl_call_proc(interp, proc, (int)n, argv, &result);
  free(argv);

  if (rc != LCL_RC_OK) {
    if (result) {
      lcl_ref_dec(result);
    }

    return NULL;
  }

  return result ? result : lcl_string_new("");
}

lcl_int lcl_js_int_of(lcl_value *value) {
  lcl_int i;

  return lcl_value_type_of(value) == LCL_INT &&
                 lcl_value_to_int(value, &i) == LCL_OK
             ? i
             : 0;
}

double lcl_js_float_of(lcl_value *value) {
  double f;

  return lcl_value_type_of(value) == LCL_FLOAT &&
                 lcl_value_to_float(value, &f) == LCL_OK
             ? f
             : 0.0;
}

/* Append `value` (consumed) to `list` (consumed); the list (+1) or NULL. */
lcl_value *lcl_js_list_push_take(lcl_value *list, lcl_value *value) {
  lcl_result r = lcl_list_push(&list, value);

  lcl_ref_dec(value);

  if (r != LCL_OK) {
    lcl_ref_dec(list);
    return NULL;
  }

  return list;
}

/* Store `value` (consumed) under `key` in `dict` (consumed); the dict (+1). */
lcl_value *lcl_js_dict_put_take(lcl_value *dict, const char *key,
                                lcl_value *value) {
  lcl_result r = lcl_dict_put(&dict, key, value);

  lcl_ref_dec(value);

  if (r != LCL_OK) {
    lcl_ref_dec(dict);
    return NULL;
  }

  return dict;
}

/* The keys of `dict` as a fresh list (+1), or NULL. */
lcl_value *lcl_js_dict_keys(lcl_value *dict) {
  lcl_value *keys = NULL;

  if (lcl_dict_keys(dict, &keys) != LCL_OK) {
    return NULL;
  }

  return keys;
}

static void session_finalizer(void *p) {
  lcl_js_interp_closed((lcl_interp *)p);
}

#define JS_OP(fn, op)                                                          \
  static lcl_return_code fn(lcl_interp *interp, int argc, lcl_value **argv,    \
                            lcl_value **out) {                                 \
    return lcl_js_op(interp, op, argc, argv, out) == 0 ? LCL_RC_OK             \
                                                       : LCL_RC_ERR;           \
  }

JS_OP(c_global, LCL_JS_GLOBAL)
JS_OP(c_get, LCL_JS_GET)
JS_OP(c_set, LCL_JS_SET)
JS_OP(c_del, LCL_JS_DEL)
JS_OP(c_call, LCL_JS_CALL)
JS_OP(c_invoke, LCL_JS_INVOKE)
JS_OP(c_new, LCL_JS_NEW)
JS_OP(c_eval, LCL_JS_EVAL)
JS_OP(c_fn, LCL_JS_FN)
JS_OP(c_typeof, LCL_JS_TYPEOF)
JS_OP(c_to_list, LCL_JS_TO_LIST)
JS_OP(c_to_dict, LCL_JS_TO_DICT)
JS_OP(c_release, LCL_JS_RELEASE)
JS_OP(c_object, LCL_JS_OBJECT)
JS_OP(c_array, LCL_JS_ARRAY)

void lcl_register_js(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new("Js");

  if (!ns) {
    return;
  }

  lcl_ns_def_take(ns, "global", lcl_c_proc_new("Js::global", c_global));
  lcl_ns_def_take(ns, "get", lcl_c_proc_new("Js::get", c_get));
  lcl_ns_def_take(ns, "set", lcl_c_proc_new("Js::set", c_set));
  lcl_ns_def_take(ns, "del", lcl_c_proc_new("Js::del", c_del));
  lcl_ns_def_take(ns, "call", lcl_c_proc_new("Js::call", c_call));
  lcl_ns_def_take(ns, "invoke", lcl_c_proc_new("Js::invoke", c_invoke));
  lcl_ns_def_take(ns, "new", lcl_c_proc_new("Js::new", c_new));
  lcl_ns_def_take(ns, "eval", lcl_c_proc_new("Js::eval", c_eval));
  lcl_ns_def_take(ns, "fn", lcl_c_proc_new("Js::fn", c_fn));
  lcl_ns_def_take(ns, "typeof", lcl_c_proc_new("Js::typeof", c_typeof));
  lcl_ns_def_take(ns, "to_list", lcl_c_proc_new("Js::to_list", c_to_list));
  lcl_ns_def_take(ns, "to_dict", lcl_c_proc_new("Js::to_dict", c_to_dict));
  lcl_ns_def_take(ns, "release", lcl_c_proc_new("Js::release", c_release));
  lcl_ns_def_take(ns, "object", lcl_c_proc_new("Js::object", c_object));
  lcl_ns_def_take(ns, "array", lcl_c_proc_new("Js::array", c_array));
  lcl_ns_def_take(ns, "_session",
                  lcl_opaque_new(interp, "Js::session", session_finalizer));

  lcl_js_interp_open(interp);
  lcl_define_take(interp, "Js", ns);
}
