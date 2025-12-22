#include <memory.h>
#include <string.h>

#include "lcl-compile.h"
#include "lcl-values.h"
#include "str-compat.h"

lcl_value *lcl_proc_new(lcl_frame *closure, lcl_value *params, lcl_program *body) {
  lcl_proc *p = (lcl_proc *)calloc(1, sizeof(*p));

  if (!p) return NULL;

  p->closure = lcl_frame_ref_inc(closure);
  p->params = lcl_ref_inc(params);
  p->body = body;

  {
    lcl_value *v = (lcl_value *)calloc(1, sizeof(*v));

    if (!v) {
      lcl_frame_ref_dec(p->closure);
      lcl_ref_dec(p->params);
      lcl_program_free(p->body);
      free(p);
      return NULL;
    }

    v->type = LCL_PROC;
    v->refc = 1;
    v->as.procedure.proc = p;

    return v;
  }  
}

lcl_value *lcl_c_proc_new(const char *name, lcl_c_proc_fn fn) {
  lcl_value *proc = (lcl_value *)calloc(1, sizeof(*proc));

  if (!proc) {
    return NULL;
  }

  {  
    lcl_c_func *func = (lcl_c_func *)calloc(1, sizeof(*func));

    if (!func) {
      free(proc);
      return NULL;
    }
    
    func->kind = LCL_CK_PROC;
    func->name = "";
    func->fn.proc = fn;
  
    proc->type = LCL_CPROC;
    proc->refc = 1;
    proc->str_repr = strndup(name, strlen(name));
    proc->as.c_proc.fn = func;

    return proc;
  }
}

lcl_value *lcl_c_spec_new(const char *name, lcl_c_spec_fn fn) {
  lcl_value *proc = (lcl_value *)calloc(1, sizeof(*proc));

  if (!proc) {
    return NULL;
  }

  {
    lcl_c_func *func = (lcl_c_func *)calloc(1, sizeof(*func));

    if (!func) {
      free(proc);
      return NULL;
    }

    func->kind = LCL_CK_SPECIAL;
    func->name = "";
    func->fn.spec = fn;

    proc->type = LCL_CPROC;
    proc->refc = 1;
    proc->str_repr = strndup(name, strlen(name));
    proc->as.c_proc.fn = func;

    return proc;
  }
}

