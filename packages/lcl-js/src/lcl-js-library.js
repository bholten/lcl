/*
 * lcl-js: the JavaScript half of `Js::`, linked into the wasm module
 * as an Emscripten JS library (--js-library). It owns the one value
 * bridge between Lcl and JavaScript -- Js:: operations, the callback
 * wrappers for Lcl procedures, and the handle table for JavaScript
 * objects -- and exposes it as Module.LclJs so a host wrapper
 * (wasm/lcl.mjs) marshals through the same code.
 *
 * Values cross by type. JS -> Lcl: string, number (int when integral
 * and safe, else float), bigint (int), boolean (1/0), null and
 * undefined (""), arrays and plain objects by value (list, dict --
 * recursively), everything else by handle -- an opaque "Js::ref"
 * whose finalizer releases the handle. Lcl -> JS: string, int
 * (number, or BigInt beyond 2^53), float, list (fresh Array), dict
 * (fresh object), cell (its contents), proc (a callable function
 * that calls back into the interpreter), Js::ref (the object it
 * refers to), other opaques and namespaces (a handle object).
 */

mergeInto(LibraryManager.library, {
  $LclJs__deps: [
    '$UTF8ToString', '$stringToNewUTF8', 'free',
    'lcl_js_ref_new', 'lcl_js_ref_id', 'lcl_js_call_proc', 'lcl_js_int_of',
    'lcl_js_float_of', 'lcl_js_list_push_take', 'lcl_js_dict_put_take',
    'lcl_js_dict_keys', 'lcl_ref_inc', 'lcl_ref_dec', 'lcl_value_type_of',
    'lcl_value_to_string', 'lcl_string_new', 'lcl_int_new', 'lcl_float_new',
    'lcl_list_new', 'lcl_list_len', 'lcl_list_peek', 'lcl_dict_new',
    'lcl_dict_peek', 'lcl_cell_peek', 'lcl_set_error', 'lcl_interp_error_msg',
    'lcl_interp_error_file', 'lcl_interp_error_line', 'lcl_clear_error',
  ],
  /* Emscripten serialises this object with JSON.stringify, so the
   * non-JSON state (Maps, the Symbol, BigInt bounds, the error class)
   * is created by init() at module load. */
  $LclJs__postset: 'LclJs.init(); Module["LclJs"] = LclJs;',
  $LclJs: {
    /* lcl_type tags (include/lcl.h). */
    T_STRING: 0, T_INT: 1, T_FLOAT: 2, T_LIST: 3, T_DICT: 4, T_CELL: 5,
    T_PROC: 6, T_CPROC: 7,
    /* Keep in step with the enum in lcl-js.c. */
    OP: { GLOBAL: 0, GET: 1, SET: 2, DEL: 3, CALL: 4, INVOKE: 5, NEW: 6,
          EVAL: 7, FN: 8, TYPEOF: 9, TO_LIST: 10, TO_DICT: 11, RELEASE: 12,
          OBJECT: 13, ARRAY: 14 },
    nextId: 1,

    init() {
      /* The pointer an Lcl-backed JS value (proc wrapper, handle) carries. */
      LclJs.PTR = Symbol('lcl.ptr');
      LclJs.SAFE = BigInt(Number.MAX_SAFE_INTEGER);
      LclJs.INT_MIN = -(2n ** 63n);
      LclJs.INT_MAX = 2n ** 63n - 1n;
      /* JavaScript objects held by Lcl: id -> {obj, refs}; obj -> id. */
      LclJs.handles = new Map();
      LclJs.ids = new WeakMap();
      /* Plain objects and arrays a script asked to hold by reference
       * (Js::object, Js::array) rather than copy. */
      LclJs.byRef = new WeakSet();
      /* Interpreters that are alive; proc wrappers check before calling. */
      LclJs.open = new Set();
      LclJs.registry = typeof FinalizationRegistry === 'function'
        ? new FinalizationRegistry(({ interp, ptr }) => {
            if (LclJs.open.has(interp)) _lcl_ref_dec(ptr);
          })
        : null;
      /* Both overridable by a host wrapper. */
      LclJs.LclError = class LclError extends Error {
        constructor(message, file, line) {
          super(message);
          this.name = 'LclError';
          this.file = file;
          this.line = line;
        }
      };
      LclJs.makeHandle = null;
    },

    retain(obj) {
      if (typeof obj === 'symbol') {
        const sid = LclJs.nextId++;
        LclJs.handles.set(sid, { obj, refs: 1 });
        return sid;
      }
      let id = LclJs.ids.get(obj);
      if (id !== undefined && LclJs.handles.has(id)) {
        LclJs.handles.get(id).refs++;
        return id;
      }
      id = LclJs.nextId++;
      LclJs.handles.set(id, { obj, refs: 1 });
      LclJs.ids.set(obj, id);
      return id;
    },

    release(id) {
      const h = LclJs.handles.get(id);
      if (!h) return;
      if (--h.refs === 0) {
        LclJs.handles.delete(id);
        if (typeof h.obj !== 'symbol') LclJs.ids.delete(h.obj);
      }
    },

    object(id) {
      const h = LclJs.handles.get(id);
      if (!h) throw new Error('Js: reference has been released');
      return h.obj;
    },

    str(ptr) { return UTF8ToString(_lcl_value_to_string(ptr)); },

    cstring(s) { return stringToNewUTF8(String(s)); },

    setError(interp, e) {
      const msg = e instanceof Error ? e.message : String(e);
      const p = LclJs.cstring(msg);
      _lcl_set_error(interp, p);
      _free(p);
    },

    takeError(interp) {
      const msgPtr = _lcl_interp_error_msg(interp);
      const filePtr = _lcl_interp_error_file(interp);
      const err = new LclJs.LclError(
        msgPtr ? UTF8ToString(msgPtr) : 'evaluation failed',
        filePtr ? UTF8ToString(filePtr) : null,
        _lcl_interp_error_line(interp));
      _lcl_clear_error(interp);
      return err;
    },

    /* Call an Lcl procedure (borrowed pointer) with JS arguments. */
    callProc(interp, procPtr, args) {
      if (!LclJs.open.has(interp)) throw new Error('Js: interpreter has been freed');
      const list = LclJs.fromJs(interp, args);
      let r;
      try {
        r = _lcl_js_call_proc(interp, procPtr, list);
      } finally {
        _lcl_ref_dec(list);
      }
      if (!r) throw LclJs.takeError(interp);
      try {
        return LclJs.toJs(interp, r);
      } finally {
        _lcl_ref_dec(r);
      }
    },

    /* A JS function that calls the Lcl procedure; owns one reference. */
    wrapProc(interp, ptr) {
      _lcl_ref_inc(ptr);
      const f = (...args) => {
        if (!f[LclJs.PTR]) throw new Error('Js: procedure has been released');
        return LclJs.callProc(interp, f[LclJs.PTR], args);
      };
      f[LclJs.PTR] = ptr;
      f.release = () => {
        if (!f[LclJs.PTR]) return;
        LclJs.registry?.unregister(f);
        if (LclJs.open.has(interp)) _lcl_ref_dec(f[LclJs.PTR]);
        f[LclJs.PTR] = 0;
      };
      f.toString = () => LclJs.str(ptr);
      LclJs.registry?.register(f, { interp, ptr }, f);
      return f;
    },

    /* Borrowed Lcl pointer -> JS value. */
    toJs(interp, ptr) {
      if (!ptr) return '';
      switch (_lcl_value_type_of(ptr)) {
        case LclJs.T_STRING: return LclJs.str(ptr);
        case LclJs.T_INT: {
          const i = _lcl_js_int_of(ptr);
          return i >= -LclJs.SAFE && i <= LclJs.SAFE ? Number(i) : i;
        }
        case LclJs.T_FLOAT: return _lcl_js_float_of(ptr);
        case LclJs.T_LIST: {
          const n = _lcl_list_len(ptr), out = new Array(n);
          for (let i = 0; i < n; i++) out[i] = LclJs.toJs(interp, _lcl_list_peek(ptr, i));
          return out;
        }
        case LclJs.T_DICT: {
          const keys = _lcl_js_dict_keys(ptr);
          if (!keys) throw new Error('Js: out of memory');
          const out = {};
          try {
            const n = _lcl_list_len(keys);
            for (let i = 0; i < n; i++) {
              const kPtr = _lcl_value_to_string(_lcl_list_peek(keys, i));
              out[UTF8ToString(kPtr)] = LclJs.toJs(interp, _lcl_dict_peek(ptr, kPtr));
            }
          } finally {
            _lcl_ref_dec(keys);
          }
          return out;
        }
        case LclJs.T_CELL: return LclJs.toJs(interp, _lcl_cell_peek(ptr));
        case LclJs.T_PROC:
        case LclJs.T_CPROC: return LclJs.wrapProc(interp, ptr);
        default: {
          const id = _lcl_js_ref_id(ptr);
          if (id >= 0) return LclJs.object(id);
          _lcl_ref_inc(ptr);
          if (LclJs.makeHandle) return LclJs.makeHandle(interp, ptr);
          return { [LclJs.PTR]: ptr, toString: () => LclJs.str(ptr),
                   release() { if (this[LclJs.PTR]) { _lcl_ref_dec(this[LclJs.PTR]); this[LclJs.PTR] = 0; } } };
        }
      }
    },

    /* JS value -> new Lcl pointer (+1). */
    fromJs(interp, v) {
      let ptr;
      if (v === null || v === undefined) v = '';
      if (typeof v === 'string') {
        const p = LclJs.cstring(v);
        ptr = _lcl_string_new(p);
        _free(p);
      } else if (typeof v === 'number') {
        ptr = Number.isSafeInteger(v) ? _lcl_int_new(BigInt(v)) : _lcl_float_new(v);
      } else if (typeof v === 'boolean') {
        ptr = _lcl_int_new(v ? 1n : 0n);
      } else if (typeof v === 'bigint') {
        if (v < LclJs.INT_MIN || v > LclJs.INT_MAX) throw new RangeError('Js: integer out of range');
        ptr = _lcl_int_new(v);
      } else if (v[LclJs.PTR]) {
        ptr = _lcl_ref_inc(v[LclJs.PTR]);
      } else if (LclJs.byRef.has(v)) {
        ptr = _lcl_js_ref_new(LclJs.retain(v));
      } else if (Array.isArray(v)) {
        ptr = _lcl_list_new();
        for (const item of v) {
          ptr = ptr && _lcl_js_list_push_take(ptr, LclJs.fromJs(interp, item));
        }
      } else if (LclJs.isPlain(v)) {
        ptr = _lcl_dict_new();
        for (const [k, item] of Object.entries(v)) {
          if (!ptr) break;
          const kPtr = LclJs.cstring(k);
          ptr = _lcl_js_dict_put_take(ptr, kPtr, LclJs.fromJs(interp, item));
          _free(kPtr);
        }
      } else {
        ptr = _lcl_js_ref_new(LclJs.retain(v));
      }
      if (!ptr) throw new Error('Js: out of memory');
      return ptr;
    },

    /* JSON-shaped data crosses by value: a plain object is one whose
     * prototype is Object.prototype and that carries no toStringTag
     * (Math, JSON, console and friends do, and stay references). */
    isPlain(v) {
      return typeof v === 'object' && Object.getPrototypeOf(v) === Object.prototype &&
             v[Symbol.toStringTag] === undefined;
    },

    /* argv[i] (borrowed) as JS values. */
    args(interp, argc, argv, from) {
      const out = [];
      for (let i = from; i < argc; i++) {
        out.push(LclJs.toJs(interp, {{{ makeGetValue('argv', 'i * 4', '*') }}}));
      }
      return out;
    },

    /* The receiver argument: a Js::ref's object, or any value (a
     * primitive receiver works the way it does in JavaScript). */
    target(interp, argc, argv, i, what) {
      if (i >= argc) throw new Error(`Js: expected ${what}`);
      const v = LclJs.toJs(interp, {{{ makeGetValue('argv', 'i * 4', '*') }}});
      if (v === null || v === undefined) throw new Error(`Js: expected ${what}, got ${v}`);
      return v;
    },

    key(interp, argc, argv, i) {
      if (i >= argc) throw new Error('Js: expected a property name');
      return LclJs.toJs(interp, {{{ makeGetValue('argv', 'i * 4', '*') }}});
    },

    walk(obj, keys) {
      for (const k of keys) {
        if (obj === null || obj === undefined) throw new Error(`Js: cannot read "${k}" of ${obj}`);
        obj = obj[k];
      }
      return obj;
    },

    op(interp, op, argc, argv, out) {
      const OP = LclJs.OP;
      const need = (n, usage) => { if (argc < n) throw new Error(`Js: ${usage}`); };
      let result;
      switch (op) {
        case OP.GLOBAL:
          result = LclJs.walk(globalThis, LclJs.args(interp, argc, argv, 0));
          break;
        case OP.GET: {
          need(1, 'expected: Js::get object ?key ...?');
          result = LclJs.walk(LclJs.target(interp, argc, argv, 0, 'object'),
                              LclJs.args(interp, argc, argv, 1));
          break;
        }
        case OP.SET: {
          need(3, 'expected: Js::set object key value');
          const obj = LclJs.target(interp, argc, argv, 0, 'object');
          const a = LclJs.args(interp, argc, argv, 1);
          const val = a.pop();
          const key = a.pop();
          LclJs.walk(obj, a)[key] = val;
          result = '';
          break;
        }
        case OP.DEL: {
          need(2, 'expected: Js::del object key');
          delete LclJs.target(interp, argc, argv, 0, 'object')[LclJs.key(interp, argc, argv, 1)];
          result = '';
          break;
        }
        case OP.CALL: {
          need(2, 'expected: Js::call object method ?arg ...?');
          const obj = LclJs.target(interp, argc, argv, 0, 'object');
          const key = LclJs.key(interp, argc, argv, 1);
          const fn = obj[key];
          if (typeof fn !== 'function') throw new Error(`Js: "${key}" is not a function`);
          result = fn.apply(obj, LclJs.args(interp, argc, argv, 2));
          break;
        }
        case OP.INVOKE: {
          need(1, 'expected: Js::invoke function ?arg ...?');
          const fn = LclJs.target(interp, argc, argv, 0, 'function');
          if (typeof fn !== 'function') throw new Error('Js: value is not a function');
          result = fn(...LclJs.args(interp, argc, argv, 1));
          break;
        }
        case OP.NEW: {
          need(1, 'expected: Js::new constructor ?arg ...?');
          const first = LclJs.toJs(interp, {{{ makeGetValue('argv', '0', '*') }}});
          const ctor = typeof first === 'string' ? LclJs.walk(globalThis, first.split('.')) : first;
          if (typeof ctor !== 'function') throw new Error(`Js: ${first} is not a constructor`);
          result = new ctor(...LclJs.args(interp, argc, argv, 1));
          break;
        }
        case OP.EVAL: {
          need(1, 'expected: Js::eval source');
          result = (0, eval)(LclJs.key(interp, argc, argv, 0));
          break;
        }
        case OP.FN: {
          need(1, 'expected: Js::fn proc');
          const f = LclJs.toJs(interp, {{{ makeGetValue('argv', '0', '*') }}});
          if (typeof f !== 'function') throw new Error('Js: expected a procedure');
          result = f;
          break;
        }
        case OP.TYPEOF: {
          need(1, 'expected: Js::typeof value ?key ...?');
          const v = LclJs.walk(LclJs.toJs(interp, {{{ makeGetValue('argv', '0', '*') }}}),
                               LclJs.args(interp, argc, argv, 1));
          result = v === null ? 'null' : typeof v;
          break;
        }
        case OP.TO_LIST: {
          need(1, 'expected: Js::to_list array');
          result = Array.from(LclJs.target(interp, argc, argv, 0, 'array'));
          break;
        }
        case OP.TO_DICT: {
          need(1, 'expected: Js::to_dict object');
          const src = LclJs.target(interp, argc, argv, 0, 'object');
          result = Object.fromEntries(src instanceof Map ? src : Object.entries(src));
          break;
        }
        case OP.RELEASE: {
          need(1, 'expected: Js::release ref');
          const id = _lcl_js_ref_id({{{ makeGetValue('argv', '0', '*') }}});
          if (id >= 0) LclJs.release(id);
          result = '';
          break;
        }
        case OP.OBJECT: {
          const o = argc > 0 ? { ...LclJs.toJs(interp, {{{ makeGetValue('argv', '0', '*') }}}) } : {};
          LclJs.byRef.add(o);
          result = o;
          break;
        }
        case OP.ARRAY: {
          const a = argc > 0 ? Array.from(LclJs.toJs(interp, {{{ makeGetValue('argv', '0', '*') }}})) : [];
          LclJs.byRef.add(a);
          result = a;
          break;
        }
        default:
          throw new Error(`Js: unknown operation ${op}`);
      }
      {{{ makeSetValue('out', '0', 'LclJs.fromJs(interp, result)', '*') }}};
    },
  },

  lcl_js_op__deps: ['$LclJs'],
  lcl_js_op: (interp, op, argc, argv, out) => {
    try {
      LclJs.op(interp, op, argc, argv, out);
      return 0;
    } catch (e) {
      LclJs.setError(interp, e);
      return 1;
    }
  },

  lcl_js_ref_release__deps: ['$LclJs'],
  lcl_js_ref_release: (id) => LclJs.release(id),

  lcl_js_interp_open__deps: ['$LclJs'],
  lcl_js_interp_open: (interp) => { LclJs.open.add(interp); },

  lcl_js_interp_closed__deps: ['$LclJs'],
  lcl_js_interp_closed: (interp) => { LclJs.open.delete(interp); },
});
