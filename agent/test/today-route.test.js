const { test } = require('node:test');
const assert = require('node:assert');
const http = require('node:http');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { startServer, readEvents } = require('../clawd-agent.js');

const MIN = 60 * 1000;

// 建临时配置目录,configDir() 在请求时实时读 CLAWD_CONFIG_DIR,故每测前设置即可
function mkTmpDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-today-'));
}

// 启服务于临时端口,返回 { server, port }
function listen() {
  return new Promise((resolve) => {
    const server = startServer(0);
    server.on('listening', () => resolve({ server, port: server.address().port }));
  });
}

function getJSON(port, urlPath) {
  return new Promise((resolve, reject) => {
    http.get(`http://127.0.0.1:${port}${urlPath}`, (r) => {
      let b = '';
      r.on('data', (c) => (b += c));
      r.on('end', () => {
        try { resolve(JSON.parse(b)); } catch (e) { reject(e); }
      });
    }).on('error', reject);
  });
}

test('GET /today:无事件文件 → 全 0 空集 + date 字段', async () => {
  const dir = mkTmpDir();
  process.env.CLAWD_CONFIG_DIR = dir;
  const { server, port } = await listen();
  try {
    const r = await getJSON(port, '/today?date=2099-01-01');
    assert.strictEqual(r.date, '2099-01-01');
    assert.strictEqual(r.activeMs, 0);
    assert.strictEqual(r.sessions, 0);
    assert.strictEqual(r.asks, 0);
    assert.strictEqual(r.tool, null);
    assert.deepStrictEqual(r.naps, []);
    assert.deepStrictEqual(r.segments, []);
  } finally {
    server.close();
    delete process.env.CLAWD_CONFIG_DIR;
  }
});

test('GET /today?date=:读 fixture jsonl → 正确 sessions/asks/activeMs', async () => {
  const dir = mkTmpDir();
  process.env.CLAWD_CONFIG_DIR = dir;
  const base = new Date(2026, 5, 17, 9, 0).getTime();
  const at = (m) => base + m * MIN;
  const lines = [
    { ts: at(0),  tool: 'cc', event: 'SessionStart' },
    { ts: at(1),  tool: 'cc', event: 'UserPromptSubmit' },
    { ts: at(5),  tool: 'cc', event: 'Stop' },
    { ts: at(40), tool: 'cc', event: 'UserPromptSubmit' }, // 间隔35>20 → 切段
    { ts: at(45), tool: 'cc', event: 'Stop' },
  ].map((e) => JSON.stringify(e)).join('\n') + '\n';
  fs.writeFileSync(path.join(dir, 'events-2026-06-17.jsonl'), lines);

  const { server, port } = await listen();
  try {
    const r = await getJSON(port, '/today?date=2026-06-17');
    assert.strictEqual(r.date, '2026-06-17');
    assert.strictEqual(r.sessions, 1);
    assert.strictEqual(r.asks, 2);
    assert.strictEqual(r.tool, 'cc');
    assert.strictEqual(r.segments.length, 2);
    assert.strictEqual(r.activeMs, (5 + 5) * MIN); // 段1 09:00→09:05, 段2 09:40→09:45
    assert.strictEqual(r.naps.length, 1);
  } finally {
    server.close();
    delete process.env.CLAWD_CONFIG_DIR;
  }
});

test('readEvents:跳过坏行(非 JSON)而不抛错', () => {
  const dir = mkTmpDir();
  const file = path.join(dir, 'events-2026-06-17.jsonl');
  fs.writeFileSync(file,
    '{"ts":1,"tool":"cc","event":"Stop"}\n' +
    'this is not json\n' +
    '\n' +
    '{"ts":2,"tool":"cc","event":"Stop"}\n');
  const evs = readEvents('2026-06-17', dir);
  assert.strictEqual(evs.length, 2);
  assert.strictEqual(evs[0].ts, 1);
  assert.strictEqual(evs[1].ts, 2);
});

test('readEvents:缺文件 → 空数组', () => {
  const dir = mkTmpDir();
  assert.deepStrictEqual(readEvents('2099-12-31', dir), []);
});
