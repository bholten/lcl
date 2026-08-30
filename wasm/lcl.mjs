/*
 * lcl.mjs -- the JavaScript side of the WebAssembly host.
 *
 *   import createLcl from './lcl.mjs';
 *   const lcl = await createLcl({ print: line => term.write(line) });
 *   lcl.define('greet', name => `hello, ${name}`);
 *   lcl.eval('puts [greet world]');
 *
 * Values cross the boundary by type, not by text, through lcl-js's
 * bridge (Module.LclJs -- the same code that backs `Js::`): an Lcl
 * int is a JS number (BigInt beyond 2^53), a list an Array, a dict a
 * plain object, a proc a callable function, a Js::ref the JavaScript
 * object it refers to. Namespaces and other opaques come back as an
 * LclValue handle that stringifies like the interpreter would.
 *
 * Going the other way, integral JS numbers and BigInts become Lcl
 * ints and other numbers floats, booleans 1/0, null and undefined
 * the empty string, Arrays lists, plain objects dicts, and any other
 * object -- a DOM node, a Map, a function -- a Js::ref the script
 * drives with `Js::`. The int/float distinction of a value that
 * round-trips through JS is not preserved (2.0 comes back as 2).
 *
 * Every Lcl reference JS holds is released when the wrapper that
 * owns it is released -- explicitly via .release(), or by the
 * garbage collector as a backstop.
 */

import createLclCore from './lcl-core.mjs';

export class LclError extends Error {
  constructor(message, file, line) {
    super(message);
    this.name = 'LclError';
    this.file = file;
    this.line = line;
  }
}

const registry = typeof FinalizationRegistry === 'function'
  ? new FinalizationRegistry(({ core, interp, ptr }) => {
      if (core.LclJs.open.has(interp)) core._lcl_ref_dec(ptr);
    })
  : null;

/* A namespace or non-JavaScript opaque value, held by reference. */
export class LclValue {
  #core;
  #interp;
  constructor(core, interp, ptr) {
    this.#core = core;
    this.#interp = interp;
    this[core.LclJs.PTR] = ptr;
    registry?.register(this, { core, interp, ptr }, this);
  }
  toString() { return this.#core.LclJs.str(this[this.#core.LclJs.PTR]); }
  release() {
    const PTR = this.#core.LclJs.PTR;
    if (!this[PTR]) return;
    registry?.unregister(this);
    if (this.#core.LclJs.open.has(this.#interp)) this.#core._lcl_ref_dec(this[PTR]);
    this[PTR] = 0;
  }
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
  #js;
  #interp;
  #hosts = new Map();
  #nextHostId = 1;
  #hostFnPtr;
  #stepFnPtr = 0;
  #sourceFnPtr = 0;

  constructor(core) {
    this.#core = core;
    this.#js = core.LclJs;
    if (!this.#js) throw new Error('lcl: this build lacks lcl-js (LCL_BUILD_JS)');
    this.#js.LclError = LclError;
    this.#js.makeHandle = (interp, ptr) => new LclValue(core, interp, ptr);

    this.#interp = core._lclw_new();
    if (!this.#interp) throw new Error('lcl: failed to create interpreter');

    /* int host(interp, id, args, out) -- see lclw_host_fn in lcl-wasm.c. */
    this.#hostFnPtr = core.addFunction((interp, id, argsPtr, outPtr) => {
      const fn = this.#hosts.get(id);
      try {
        if (!fn) throw new Error(`host procedure ${id} is not registered`);
        const result = fn(...this.#js.toJs(interp, argsPtr));
        core.HEAPU32[outPtr >> 2] = this.#js.fromJs(interp, result);
        return 0;
      } catch (e) {
        this.#js.setError(interp, e);
        return 1;
      }
    }, 'iiiii');
    core._lclw_set_host_fn(this.#interp, this.#hostFnPtr);
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
      if (typeof value === 'function' && !value[this.#js.PTR]) {
        const id = this.#nextHostId++;
        this.#hosts.set(id, value);
        if (this.#core._lclw_define_host_proc(this.#interp, namePtr, id) !== 0) {
          this.#hosts.delete(id);
          throw this.#js.takeError(this.#interp);
        }
        return;
      }
      const ptr = this.#js.fromJs(this.#interp, value);
      if (this.#core._lclw_define_take(this.#interp, namePtr, ptr) !== 0) {
        throw this.#js.takeError(this.#interp);
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
    if (typeof proc !== 'function' || !proc[this.#js.PTR]) {
      throw new TypeError('lcl.call: not an Lcl procedure');
    }
    return proc(...args);
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

  /*
   * Where `require` and `load` get module text: `source(path)` returns
   * the module's source as a string, or null/undefined for "no such
   * module"; throwing reports the exception's message instead of the
   * list of paths tried. A plain object maps paths to sources. Pass
   * null to go back to the (virtual) filesystem.
   */
  setModuleSource(source) {
    if (this.#sourceFnPtr) {
      this.#core.removeFunction(this.#sourceFnPtr);
      this.#sourceFnPtr = 0;
    }
    if (source) {
      const lookup = typeof source === 'function'
        ? source
        : path => (Object.hasOwn(source, path) ? source[path] : null);
      this.#sourceFnPtr = this.#core.addFunction((interp, pathPtr, lenPtr) => {
        try {
          const text = lookup(this.#core.UTF8ToString(pathPtr));
          if (text === null || text === undefined) return 0;
          if (lenPtr) this.#core.HEAPU32[lenPtr >> 2] = this.#core.lengthBytesUTF8(String(text));
          return this.#cstr(text);
        } catch (e) {
          this.#js.setError(interp, e);
          return 0;
        }
      }, 'iiii');
    }
    this.#core._lclw_set_module_source_fn(this.#interp, this.#sourceFnPtr);
  }

  /* A directory bare `require` names are looked up under, in order. */
  addRequireRoot(dir) {
    const p = this.#cstr(dir);
    this.#core._lcl_add_require_root(this.#interp, p);
    this.#core._free(p);
  }

  /* A hard per-eval command budget. */
  setBudget(commands) {
    this.setStepHook(commands > 0 ? () => true : null, commands);
  }

  /* Abort the evaluation in progress (from a host proc or step hook). */
  abort() { this.#core._lclw_abort(this.#interp); }

  free() {
    if (!this.#interp) return;
    this.setStepHook(null);
    this.setModuleSource(null);
    this.#core._lclw_free(this.#interp);
    this.#interp = 0;
    this.#core.removeFunction(this.#hostFnPtr);
    this.#hosts.clear();
  }

  /* ---- internal ------------------------------------------------------ */

  #cstr(s) { return this.#core.stringToNewUTF8(String(s)); }

  /* A +1 result pointer (or 0 on error) into a JS value. */
  #takeResult(r) {
    if (!r) throw this.#js.takeError(this.#interp);
    try {
      return this.#js.toJs(this.#interp, r);
    } finally {
      this.#core._lcl_ref_dec(r);
    }
  }
}
