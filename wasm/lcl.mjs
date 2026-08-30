/*
 * lcl.mjs -- the JavaScript side of the WebAssembly host.
 *
 *   import createLcl from './lcl.mjs';
 *   const lcl = await createLcl({ print: line => term.write(line) });
 *   lcl.define('greet', name => `hello, ${name}`);
 *   lcl.eval('puts [greet world]');
 *
 * Values cross the boundary by type, not by text: an Lcl int is a JS
 * number, a list an Array, a dict a plain object, a proc a callable
 * function. Anything else (namespaces, opaques) comes back as an
 * LclValue handle that stringifies like the interpreter would.
 *
 * Going the other way, integral JS numbers become Lcl ints and the
 * rest floats, booleans become 1/0, null and undefined the empty
 * string, Arrays lists, plain objects dicts, functions host procs.
 * The int/float distinction of a value that round-trips through JS
 * is therefore not preserved (2.0 comes back as 2); JS has one
 * number type.
 *
 * Every Lcl reference JS holds is released when the wrapper that
 * owns it is released -- explicitly via .release(), or by the
 * garbage collector as a backstop.
 */

import createLclCore from './lcl-core.mjs';

const T_STRING = 0, T_INT = 1, T_FLOAT = 2, T_LIST = 3, T_DICT = 4,
      T_CELL = 5, T_PROC = 6, T_CPROC = 7;

/* Lcl ints are C `long`s: 32 bits under wasm32. Integral JS numbers
 * outside this range are passed as floats. */
const INT_MIN = -2147483648, INT_MAX = 2147483647;

const PTR = Symbol('lcl.ptr');

export class LclError extends Error {
  constructor(message, file, line) {
    super(message);
    this.name = 'LclError';
    this.file = file;
    this.line = line;
  }
}

/* A namespace or opaque value held by reference. */
export class LclValue {
  #lcl;
  constructor(lcl, ptr) {
    this.#lcl = lcl;
    this[PTR] = ptr;
    lcl._track(this, ptr);
  }
  toString() { return this.#lcl._str(this[PTR]); }
  release() { this.#lcl._release(this); }
}

export default async function createLcl(options = {}) {
  const core = await createLclCore({
    print: options.print ?? (line => console.log(line)),
    printErr: options.printErr ?? (line => console.error(line)),
  });
  return new Lcl(core);
}

export class Lcl {
  #core;
  #interp;
  #hosts = new Map();
  #nextHostId = 1;
  #hostFnPtr;
  #stepFnPtr = 0;
  #registry;
  #alive = true;

  constructor(core) {
    this.#core = core;
    this.#interp = core._lclw_new();
    if (!this.#interp) throw new Error('lcl: failed to create interpreter');

    /* int host(interp, id, args, out) -- see lclw_host_fn in lcl-wasm.c. */
    this.#hostFnPtr = core.addFunction((interp, id, argsPtr, outPtr) => {
      const fn = this.#hosts.get(id);
      try {
        if (!fn) throw new Error(`host procedure ${id} is not registered`);
        const result = fn(...this.#toJs(argsPtr));
        core.HEAPU32[outPtr >> 2] = this.#fromJs(result);
        return 0;
      } catch (e) {
        this.#setError(e instanceof Error ? e.message : String(e));
        return 1;
      }
    }, 'iiiii');
    core._lclw_set_host_fn(this.#interp, this.#hostFnPtr);

    this.#registry = typeof FinalizationRegistry === 'function'
      ? new FinalizationRegistry(ptr => { if (this.#alive) core._lcl_ref_dec(ptr); })
      : null;
  }

  get version() { return this.#core.UTF8ToString(this.#core._lclw_version()); }

  /* Evaluate `src` as file `file`; returns the result as a JS value. */
  eval(src, file = '<eval>') {
    const srcPtr = this.#cstr(src), filePtr = this.#cstr(file);
    let r;
    try {
      r = this.#core._lclw_eval(this.#interp, srcPtr, filePtr);
    } finally {
      this.#core._free(srcPtr);
      this.#core._free(filePtr);
    }
    return this.#takeResult(r);
  }

  /* Bind `name` at the root: a JS function becomes a host procedure. */
  define(name, value) {
    const namePtr = this.#cstr(name);
    try {
      if (typeof value === 'function' && !value[PTR]) {
        const id = this.#nextHostId++;
        this.#hosts.set(id, value);
        if (this.#core._lclw_define_host_proc(this.#interp, namePtr, id) !== 0) {
          this.#hosts.delete(id);
          throw this.#takeError();
        }
        return;
      }
      const ptr = this.#fromJs(value);
      if (this.#core._lclw_define_take(this.#interp, namePtr, ptr) !== 0) {
        throw this.#takeError();
      }
    } finally {
      this.#core._free(namePtr);
    }
  }

  /* Read a root binding (`Ns::name` allowed). */
  get(name) {
    const namePtr = this.#cstr(name);
    let r;
    try {
      r = this.#core._lclw_get(this.#interp, namePtr);
    } finally {
      this.#core._free(namePtr);
    }
    return this.#takeResult(r);
  }

  /* Call an Lcl procedure value (as returned by eval/get) with JS args. */
  call(proc, ...args) {
    const procPtr = proc?.[PTR];
    if (!procPtr) throw new TypeError('lcl.call: not an Lcl procedure');
    return this.#callPtr(procPtr, args);
  }

  /*
   * Install a step hook: `fn(lcl)` runs every `interval` commands and
   * aborts the current evaluation when it returns truthy. The abort
   * is sticky (uncatchable by the script) until eval returns.
   */
  setStepHook(fn, interval = 1000) {
    if (this.#stepFnPtr) {
      this.#core.removeFunction(this.#stepFnPtr);
      this.#stepFnPtr = 0;
    }
    if (fn) {
      this.#stepFnPtr = this.#core.addFunction(() => (fn(this) ? 1 : 0), 'ii');
    }
    this.#core._lclw_set_step_hook(this.#interp, this.#stepFnPtr, interval);
  }

  /* A hard per-eval command budget. */
  setBudget(commands) {
    this.setStepHook(commands > 0 ? () => true : null, commands);
  }

  /* Abort the evaluation in progress (from a host proc or step hook). */
  abort() { this.#core._lclw_abort(this.#interp); }

  free() {
    if (!this.#alive) return;
    this.#alive = false;
    this.setStepHook(null);
    this.#core._lclw_free(this.#interp);
    this.#core.removeFunction(this.#hostFnPtr);
    this.#hosts.clear();
  }

  /* ---- internal ------------------------------------------------------ */

  _track(owner, ptr) { this.#registry?.register(owner, ptr, owner); }

  _release(owner) {
    const ptr = owner[PTR];
    if (!ptr) return;
    owner[PTR] = 0;
    this.#registry?.unregister(owner);
    if (this.#alive) this.#core._lcl_ref_dec(ptr);
  }

  _str(ptr) { return this.#core.UTF8ToString(this.#core._lcl_value_to_string(ptr)); }

  #cstr(s) { return this.#core.stringToNewUTF8(String(s)); }

  #setError(message) {
    const p = this.#cstr(message);
    this.#core._lcl_set_error(this.#interp, p);
    this.#core._free(p);
  }

  #takeError() {
    const c = this.#core;
    const msgPtr = c._lcl_interp_error_msg(this.#interp);
    const filePtr = c._lcl_interp_error_file(this.#interp);
    const err = new LclError(
      msgPtr ? c.UTF8ToString(msgPtr) : 'evaluation failed',
      filePtr ? c.UTF8ToString(filePtr) : null,
      c._lcl_interp_error_line(this.#interp));
    c._lcl_clear_error(this.#interp);
    return err;
  }

  /* A +1 result pointer (or 0 on error) into a JS value. */
  #takeResult(r) {
    if (!r) throw this.#takeError();
    try {
      return this.#toJs(r);
    } finally {
      this.#core._lcl_ref_dec(r);
    }
  }

  #callPtr(procPtr, args) {
    const listPtr = this.#fromJs(args);
    let r;
    try {
      r = this.#core._lclw_call(this.#interp, procPtr, listPtr);
    } finally {
      this.#core._lcl_ref_dec(listPtr);
    }
    return this.#takeResult(r);
  }

  #wrapProc(ptr) {
    const c = this.#core;
    c._lcl_ref_inc(ptr);
    const f = (...args) => {
      if (!f[PTR]) throw new Error('lcl: procedure has been released');
      return this.#callPtr(f[PTR], args);
    };
    f[PTR] = ptr;
    f.release = () => this._release(f);
    f.toString = () => this._str(ptr);
    this._track(f, ptr);
    return f;
  }

  /* Borrowed pointer -> JS value. */
  #toJs(ptr) {
    const c = this.#core;
    if (!ptr) return '';
    switch (c._lcl_value_type_of(ptr)) {
      case T_STRING: return this._str(ptr);
      case T_INT:
      case T_FLOAT: return c._lclw_number_of(ptr);
      case T_LIST: {
        const n = c._lcl_list_len(ptr), out = new Array(n);
        for (let i = 0; i < n; i++) out[i] = this.#toJs(c._lcl_list_peek(ptr, i));
        return out;
      }
      case T_DICT: {
        const keys = c._lclw_dict_keys(ptr);
        if (!keys) throw new Error('lcl: out of memory');
        const out = {};
        try {
          const n = c._lcl_list_len(keys);
          for (let i = 0; i < n; i++) {
            const kPtr = c._lcl_value_to_string(c._lcl_list_peek(keys, i));
            out[c.UTF8ToString(kPtr)] = this.#toJs(c._lcl_dict_peek(ptr, kPtr));
          }
        } finally {
          c._lcl_ref_dec(keys);
        }
        return out;
      }
      case T_CELL: return this.#toJs(c._lcl_cell_peek(ptr));
      case T_PROC:
      case T_CPROC: return this.#wrapProc(ptr);
      default:
        c._lcl_ref_inc(ptr);
        return new LclValue(this, ptr);
    }
  }

  /* JS value -> new +1 pointer. */
  #fromJs(v) {
    const c = this.#core;
    let ptr;
    if (v === null || v === undefined) v = '';
    if (typeof v === 'string') {
      const p = this.#cstr(v);
      ptr = c._lcl_string_new(p);
      c._free(p);
    } else if (typeof v === 'number') {
      ptr = Number.isInteger(v) && v >= INT_MIN && v <= INT_MAX
        ? c._lcl_int_new(v) : c._lcl_float_new(v);
    } else if (typeof v === 'boolean') {
      ptr = c._lcl_int_new(v ? 1 : 0);
    } else if (typeof v === 'bigint') {
      throw new TypeError('lcl: BigInt values are not supported');
    } else if (v[PTR]) {
      ptr = c._lcl_ref_inc(v[PTR]);
    } else if (typeof v === 'function') {
      ptr = this.#anonymousHostProc(v);
    } else if (Array.isArray(v)) {
      ptr = c._lcl_list_new();
      for (const item of v) {
        ptr = ptr && c._lclw_list_push_take(ptr, this.#fromJs(item));
      }
    } else if (typeof v === 'object') {
      ptr = c._lcl_dict_new();
      for (const [k, item] of Object.entries(v)) {
        if (!ptr) break;
        const kPtr = this.#cstr(k);
        ptr = c._lclw_dict_put_take(ptr, kPtr, this.#fromJs(item));
        c._free(kPtr);
      }
    } else {
      throw new TypeError(`lcl: cannot convert ${typeof v}`);
    }
    if (!ptr) throw new Error('lcl: out of memory');
    return ptr;
  }

  /* A JS function passed by value: a lambda forwarding to the host. */
  #anonymousHostProc(fn) {
    const id = this.#nextHostId++;
    this.#hosts.set(id, fn);
    const src = this.#cstr(`lambda {*args} { ::Wasm::_host ${id} $args }`);
    const file = this.#cstr('<host>');
    let r;
    try {
      r = this.#core._lclw_eval(this.#interp, src, file);
    } finally {
      this.#core._free(src);
      this.#core._free(file);
    }
    if (!r) {
      this.#hosts.delete(id);
      throw this.#takeError();
    }
    return r;
  }
}
