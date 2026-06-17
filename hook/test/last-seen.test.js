const { test } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { touchLastSeen } = require('../clawd-hook.js');

function tmpDir() { return fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-ls-')); }
function leftoverTmps(dir) { return fs.readdirSync(dir).filter((f) => f.endsWith('.tmp')); }
function readState(dir) { return JSON.parse(fs.readFileSync(path.join(dir, 'agent-state.json'), 'utf8')); }

test('touchLastSeen 写入 lastSeen 且不留 .tmp', () => {
  const dir = tmpDir();
  touchLastSeen('cc', 123, dir);
  assert.strictEqual(readState(dir).lastSeen.cc, 123);
  assert.deepStrictEqual(leftoverTmps(dir), []);
});

test('rename 失败时退化为直接覆盖写,且清掉 tmp(不再堆垃圾)', () => {
  const dir = tmpDir();
  const orig = fs.renameSync;
  fs.renameSync = () => { throw Object.assign(new Error('EPERM simulated'), { code: 'EPERM' }); };
  try {
    touchLastSeen('cursor', 456, dir);
  } finally {
    fs.renameSync = orig;
  }
  assert.strictEqual(readState(dir).lastSeen.cursor, 456);   // 数据仍写进去了
  assert.deepStrictEqual(leftoverTmps(dir), []);             // 关键:没有残留 .tmp
});

test('rename 持续失败下多次调用也不堆 tmp', () => {
  const dir = tmpDir();
  const orig = fs.renameSync;
  fs.renameSync = () => { throw Object.assign(new Error('EBUSY simulated'), { code: 'EBUSY' }); };
  try {
    touchLastSeen('cc', 1, dir);
    touchLastSeen('cursor', 2, dir);
    touchLastSeen('cc', 3, dir);
  } finally {
    fs.renameSync = orig;
  }
  const st = readState(dir);
  assert.strictEqual(st.lastSeen.cc, 3);
  assert.strictEqual(st.lastSeen.cursor, 2);
  assert.deepStrictEqual(leftoverTmps(dir), []);
});
