/*
 * Exercises the JavaScript host end to end under node:
 *
 *   node wasm/test/host.mjs build-emcc/lcl.mjs
 *
 * Every assertion here is about the boundary -- marshalling in both
 * directions, errors crossing it, host procedures, callbacks, the
 * step budget, output capture -- not about the language, which the
 * conformance suite covers through the CLI build.
 */

import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const entry = process.argv[2];
if (!entry) {
  console.error('usage: node host.mjs <path-to-lcl.mjs>');
  process.exit(2);
}
const { default: createLcl, LclError, LclValue } =
  await import(pathToFileURL(resolve(entry)).href);

const lines = [];
const lcl = await createLcl({ print: line => lines.push(line) });
let passed = 0;

function test(name, fn) {
  try {
    fn();
    passed++;
  } catch (e) {
    console.error(`FAIL: ${name}\n  ${e.stack ?? e}`);
    process.exitCode = 1;
  }
}

test('version is the project version', () => {
  assert.match(lcl.version, /^\d+\.\d+\.\d+$/);
});

test('eval returns typed values', () => {
  assert.equal(lcl.eval('+ 1 2'), 3);
  assert.equal(lcl.eval('/ 1 4.0'), 0.25);
  assert.equal(lcl.eval('String::from 42'), '42');
  assert.deepEqual(lcl.eval('list 1 two (3 4)'), [1, 'two', [3, 4]]);
  assert.deepEqual(lcl.eval('dict a 1 b (x y)'), { a: 1, b: ['x', 'y'] });
  assert.equal(lcl.eval(''), '');
});

test('integers are 64-bit across the boundary', () => {
  assert.equal(lcl.eval('9007199254740991'), 9007199254740991);
  assert.equal(lcl.eval('9223372036854775807'), 9223372036854775807n);
  assert.equal(lcl.eval('-9223372036854775808'), -9223372036854775808n);
  assert.equal(lcl.eval('- 9223372036854775807 1'), 9223372036854775806n);
  assert.throws(() => lcl.eval('+ 9223372036854775807 1'), /overflow/);
  lcl.define('big', 2n ** 62n);
  assert.equal(lcl.eval('+ $big [- $big 1]'), 2n ** 63n - 1n);
  assert.throws(() => lcl.eval('* $big 2'), /overflow/);
  assert.equal(lcl.eval('String::from $big'), '4611686018427387904');
  lcl.define('n', 12);
  assert.equal(lcl.eval('type $n'), 'int');
  assert.throws(() => lcl.define('toobig', 2n ** 63n), RangeError);
  lcl.define('echo_int', x => x);
  assert.equal(lcl.eval('echo_int 9223372036854775807'), 9223372036854775807n);
  assert.equal(lcl.eval('+ [echo_int 5] 1'), 6);
});

test('puts goes to the print option', () => {
  lines.length = 0;
  lcl.eval('puts hello; puts "two words"');
  assert.deepEqual(lines, ['hello', 'two words']);
});

test('errors become LclError with file and line', () => {
  assert.throws(() => lcl.eval('puts one\nnosuch 1 2', 'sample.lcl'), e => {
    assert.ok(e instanceof LclError);
    assert.match(e.message, /nosuch/);
    assert.equal(e.file, 'sample.lcl');
    assert.equal(e.line, 2);
    return true;
  });
  /* The interpreter is usable again afterwards. */
  assert.equal(lcl.eval('+ 2 2'), 4);
});

test('define binds JS values by type', () => {
  lcl.define('answer', 42);
  lcl.define('ratio', 0.5);
  lcl.define('name', 'lcl');
  lcl.define('items', [1, 'b', { c: [true, false, null] }]);
  assert.equal(lcl.eval('+ $answer 1'), 43);
  assert.equal(lcl.eval('* $ratio 4'), 2);
  assert.equal(lcl.eval('let s "$name!"; $s'), 'lcl!');
  assert.equal(lcl.eval('get [get [get $items 2] c] 0'), 1);
  assert.equal(lcl.eval('get [get [get $items 2] c] 2'), '');
  assert.deepEqual(lcl.get('items'), [1, 'b', { c: [1, 0, ''] }]);
});

test('host procedures receive marshalled args and return values', () => {
  lcl.define('greet', (who, times = 1) => `hello, ${who}`.repeat(times));
  lcl.define('sum', (...xs) => xs.reduce((a, b) => a + b, 0));
  lcl.define('keys', obj => Object.keys(obj).sort());
  assert.equal(lcl.eval('greet world'), 'hello, world');
  assert.equal(lcl.eval('greet hi 2'), 'hello, hihello, hi');
  assert.equal(lcl.eval('sum 1 2 3 4'), 10);
  assert.deepEqual(lcl.eval('keys #{b 1 a 2}'), ['a', 'b']);
  assert.equal(lcl.eval('proc twice {x} { sum $x $x }; twice 21'), 42);
});

test('a throwing host procedure is an ordinary Lcl error', () => {
  lcl.define('boom', () => { throw new Error('kaboom'); });
  assert.throws(() => lcl.eval('boom'), /kaboom/);
  assert.equal(lcl.eval('catch { boom } res e; $e'), 'kaboom');
});

test('Lcl procedures come back as callable functions', () => {
  const add = lcl.eval('lambda {a b} { + $a $b }');
  assert.equal(typeof add, 'function');
  assert.equal(add(2, 3), 5);
  assert.equal(lcl.call(add, 10, 20), 30);
  assert.throws(() => add(1), LclError);
  add.release();
  assert.throws(() => add(1, 2), /released/);
});

test('JS functions pass through as procedure values', () => {
  const doubled = lcl.eval('List::map (1 2 3) [lambda {x} { * $x 2 }]');
  assert.deepEqual(doubled, [2, 4, 6]);
  lcl.define('apply_twice', (f, x) => f(f(x)));
  assert.equal(lcl.eval('apply_twice [lambda {x} { + $x 1 }] 5'), 7);
  lcl.define('call_it', f => f('from lcl'));
  assert.equal(lcl.eval('call_it [lambda {s} { let r "got:$s"; $r }]'),
               'got:from lcl');
  const echo = (...a) => a.join('|');
  lcl.define('bridge', f => f('a', 'b'));
  lcl.define('echo', echo);
  assert.equal(lcl.eval('bridge $echo'), 'a|b');
});

test('namespaces surface as handles that stringify', () => {
  lcl.eval('namespace Shape { proc area {r} { * $r $r } }');
  const ns = lcl.get('Shape');
  assert.ok(ns instanceof LclValue);
  assert.match(String(ns), /Shape/);
  ns.release();
  assert.equal(lcl.eval('Shape::area 3'), 9);
});

test('the step budget aborts runaway scripts', () => {
  lcl.setBudget(5000);
  assert.throws(() => lcl.eval('while {1} {}'), /aborted/);
  assert.throws(() => lcl.eval('catch { while {1} {} }'), /aborted/);
  assert.equal(lcl.eval('+ 1 1'), 2);
  lcl.setStepHook(null);
});

test('a step hook can watch and abort', () => {
  let ticks = 0;
  lcl.setStepHook(() => ++ticks > 3, 100);
  assert.throws(() => lcl.eval('var i 0; while {1} { set! i [+ $i 1] }'), /aborted/);
  assert.equal(ticks, 4);
  lcl.setStepHook(null);
});

test('abort from inside a host procedure', () => {
  lcl.define('stop', () => { lcl.abort(); return 'stopping'; });
  assert.throws(() => lcl.eval('stop; puts unreachable'), /aborted/);
});

test('free releases the interpreter', () => {
  lcl.free();
  lcl.free();
});

console.log(`wasm-host: ${passed} passed${process.exitCode ? ', some FAILED' : ''}`);
