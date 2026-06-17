const { test } = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { appendEvent, eventLogPath } = require('../clawd-hook.js');

function tmpDir() { return fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-evt-')); }

test('appendEvent 按天文件名追加一行 JSON', () => {
  const dir = tmpDir();
  const now = new Date(2026, 5, 17, 10, 30).getTime(); // 本地 2026-06-17
  appendEvent('cc', 'UserPromptSubmit', now, dir);
  const p = eventLogPath(now, dir);
  assert.ok(p.endsWith('events-2026-06-17.jsonl'));
  const lines = fs.readFileSync(p, 'utf8').trim().split('\n');
  assert.strictEqual(lines.length, 1);
  assert.deepStrictEqual(JSON.parse(lines[0]), { ts: now, tool: 'cc', event: 'UserPromptSubmit' });
});

test('appendEvent 多次追加累加行', () => {
  const dir = tmpDir();
  const now = new Date(2026, 5, 17, 10, 0).getTime();
  appendEvent('cc', 'SessionStart', now, dir);
  appendEvent('cc', 'Stop', now + 1000, dir);
  const lines = fs.readFileSync(eventLogPath(now, dir), 'utf8').trim().split('\n');
  assert.strictEqual(lines.length, 2);
});

test('appendEvent 写入失败静默不抛', () => {
  // 指向一个「父路径是文件」的非法目录,mkdir/append 必失败
  const f = path.join(os.tmpdir(), `clawd-notdir-${process.pid}`);
  fs.writeFileSync(f, 'x');
  assert.doesNotThrow(() => appendEvent('cc', 'Stop', Date.now(), path.join(f, 'sub')));
});
