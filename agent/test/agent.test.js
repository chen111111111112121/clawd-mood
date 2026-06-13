const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const os = require('os');
const http = require('http');
const { readConfig, writeActiveTool, readState, startServer, resolveDeviceTarget, DEFAULT_TOOLS } = require('../clawd-agent.js');

function tmpDir() { return fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-agent-')); }

// ── 配置读写 ──────────────────────────────────────────────────
test('readConfig: 缺文件返回默认工具表 + activeTool=null', () => {
  const cfg = readConfig(tmpDir());
  assert.strictEqual(cfg.activeTool, null);
  assert.deepStrictEqual(cfg.tools, DEFAULT_TOOLS);
});
test('writeActiveTool: 写后可读回', () => {
  const dir = tmpDir();
  writeActiveTool('cursor', dir);
  assert.strictEqual(readConfig(dir).activeTool, 'cursor');
});
test('writeActiveTool: 传 null 解绑', () => {
  const dir = tmpDir();
  writeActiveTool('cc', dir);
  writeActiveTool(null, dir);
  assert.strictEqual(readConfig(dir).activeTool, null);
});
test('readState: 缺文件返回空 lastSeen', () => {
  assert.deepStrictEqual(readState(tmpDir()), { lastSeen: {} });
});

// ── 设备目标解析 ──────────────────────────────────────────────
test('resolveDeviceTarget: 读全局 hook/device.json 的 IP', () => {
  const dir = tmpDir();
  fs.mkdirSync(path.join(dir, 'hook'), { recursive: true });
  fs.writeFileSync(path.join(dir, 'hook', 'device.json'), JSON.stringify({ device_ip: '192.168.1.7' }));
  process.env.CLAWD_CONFIG_DIR = dir; delete process.env.CLAWD_DEVICE_IP;
  try { assert.strictEqual(resolveDeviceTarget(), '192.168.1.7'); }
  finally { delete process.env.CLAWD_CONFIG_DIR; }
});
test('resolveDeviceTarget: device_ip=clawd.local 时回退到缓存 IP', () => {
  const dir = tmpDir();
  fs.mkdirSync(path.join(dir, 'hook'), { recursive: true });
  fs.writeFileSync(path.join(dir, 'hook', 'device.json'), JSON.stringify({ device_ip: 'clawd.local' }));
  fs.writeFileSync(path.join(dir, 'hook', 'device-cache.json'), JSON.stringify({ host: 'clawd.local', ip: '10.0.0.5' }));
  process.env.CLAWD_CONFIG_DIR = dir; delete process.env.CLAWD_DEVICE_IP;
  try { assert.strictEqual(resolveDeviceTarget(), '10.0.0.5'); }
  finally { delete process.env.CLAWD_CONFIG_DIR; }
});

// ── HTTP 集成 ─────────────────────────────────────────────────
function req(method, port, p, body) {
  return new Promise((resolve, reject) => {
    const r = http.request({ host: '127.0.0.1', port, path: p, method }, (res) => {
      let d = ''; res.on('data', (c) => d += c); res.on('end', () => resolve({ status: res.statusCode, body: d }));
    });
    r.on('error', reject);
    if (body) r.end(body); else r.end();
  });
}

test('HTTP: /state 返回配置;POST /active 写入后 /state 反映', async () => {
  const dir = tmpDir();
  process.env.CLAWD_CONFIG_DIR = dir;
  const server = startServer(0);                       // 0 = 随机空闲端口
  await new Promise((r) => server.once('listening', r));
  const port = server.address().port;
  try {
    let s = await req('GET', port, '/state');
    assert.strictEqual(JSON.parse(s.body).activeTool, null);
    const a = await req('POST', port, '/active', JSON.stringify({ tool: 'cursor' }));
    assert.strictEqual(a.status, 200);
    s = await req('GET', port, '/state');
    assert.strictEqual(JSON.parse(s.body).activeTool, 'cursor');
  } finally { server.close(); delete process.env.CLAWD_CONFIG_DIR; }
});

test('HTTP: GET / 返回控制台 HTML', async () => {
  const server = startServer(0);
  await new Promise((r) => server.once('listening', r));
  const port = server.address().port;
  try {
    const res = await req('GET', port, '/');
    assert.strictEqual(res.status, 200);
    assert.ok(res.body.includes('Clawd'));
  } finally { server.close(); }
});
