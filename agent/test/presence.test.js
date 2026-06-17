const { test } = require('node:test');
const assert = require('node:assert');
const fs = require('fs'); const os = require('os'); const path = require('path');
const http = require('http');
const { startServer, readConfig } = require('../clawd-agent.js');

function tmpCfg() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-pres-'));
  process.env.CLAWD_CONFIG_DIR = dir;
  return dir;
}
function post(port, body) {
  return new Promise((resolve) => {
    const data = JSON.stringify(body);
    const r = http.request({ host: '127.0.0.1', port, path: '/device/presence', method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) } },
      (res) => { let b = ''; res.on('data', c => b += c); res.on('end', () => resolve({ status: res.statusCode, body: b })); });
    r.end(data);
  });
}

test('合法 state 写入 agent.json.presence 并尝试推设备', async () => {
  const dir = tmpCfg();
  let hit = null;
  const dev = http.createServer((q, s) => { hit = q.url; s.end('ok'); }).listen(0);
  process.env.CLAWD_DEVICE_IP = `127.0.0.1:${dev.address().port}`;
  const srv = startServer(0); const port = srv.address().port;
  try {
    const r = await post(port, { state: 'meeting' });
    assert.strictEqual(r.status, 200);
    assert.strictEqual(readConfig(dir).presence, 'meeting');
    assert.ok(hit && hit.includes('/presence?s=meeting'));
  } finally { srv.close(); dev.close(); delete process.env.CLAWD_DEVICE_IP; delete process.env.CLAWD_CONFIG_DIR; }
});

test('非法 state → 400，不改 agent.json', async () => {
  const dir = tmpCfg();
  const srv = startServer(0); const port = srv.address().port;
  try {
    const r = await post(port, { state: 'foo' });
    assert.strictEqual(r.status, 400);
    assert.strictEqual(readConfig(dir).presence, 'auto');
  } finally { srv.close(); delete process.env.CLAWD_CONFIG_DIR; }
});

test('readConfig 缺 presence → 默认 auto', () => {
  const dir = tmpCfg();
  assert.strictEqual(readConfig(dir).presence, 'auto');
  delete process.env.CLAWD_CONFIG_DIR;
});

test('设备不可达仍写入 presence（ok:false）', async () => {
  const dir = tmpCfg();
  process.env.CLAWD_DEVICE_IP = '127.0.0.1:1';
  const srv = startServer(0); const port = srv.address().port;
  try {
    const r = await post(port, { state: 'rest' });
    assert.strictEqual(r.status, 200);
    assert.strictEqual(JSON.parse(r.body).ok, false);
    assert.strictEqual(readConfig(dir).presence, 'rest');
  } finally { srv.close(); delete process.env.CLAWD_DEVICE_IP; delete process.env.CLAWD_CONFIG_DIR; }
});
