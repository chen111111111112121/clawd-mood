#!/usr/bin/env node
/**
 * Clawd Agent — PC 端工具绑定控制台
 * 绑定"Mochi 当前响应哪一款 AI 工具"。Node 零依赖,默认端口 6624。
 *
 *   agent.json       (面板独占写 / hook 只读): { activeTool, tools }
 *   agent-state.json (hook 独占写 / 面板只读): { lastSeen }
 * 目录: CLAWD_CONFIG_DIR || ~/.clawd-mood
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');

const DEFAULT_PORT = 6624;
const DEFAULT_TOOLS = [
  { id: 'cc', name: 'Claude Code', installed: true },
  { id: 'cursor', name: 'Cursor', installed: true },
];

function configDir() {
  return process.env.CLAWD_CONFIG_DIR || path.join(os.homedir(), '.clawd-mood');
}

function readConfig(dir = configDir()) {
  try {
    const cfg = JSON.parse(fs.readFileSync(path.join(dir, 'agent.json'), 'utf8'));
    return {
      activeTool: (typeof cfg.activeTool === 'string' && cfg.activeTool) ? cfg.activeTool : null,
      tools: (Array.isArray(cfg.tools) && cfg.tools.length) ? cfg.tools : DEFAULT_TOOLS,
    };
  } catch (_) {
    return { activeTool: null, tools: DEFAULT_TOOLS };
  }
}

// 面板独占写 agent.json(原子替换)
function writeActiveTool(tool, dir = configDir()) {
  const cfg = readConfig(dir);
  cfg.activeTool = (typeof tool === 'string' && tool) ? tool : null;
  fs.mkdirSync(dir, { recursive: true });
  const p = path.join(dir, 'agent.json');
  const tmp = `${p}.${process.pid}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(cfg, null, 2));
  fs.renameSync(tmp, p);
  return cfg;
}

function readState(dir = configDir()) {
  try {
    const st = JSON.parse(fs.readFileSync(path.join(dir, 'agent-state.json'), 'utf8'));
    return { lastSeen: (st && st.lastSeen) || {} };
  } catch (_) { return { lastSeen: {} }; }
}

// hook mDNS 解析后会把 IP 缓存到 device-cache.json;.local 自身 Node 解析不了(还可能被 Clash 劫持),用缓存 IP
function resolveCachedIp() {
  for (const p of [
    path.join(configDir(), 'hook', 'device-cache.json'),   // 全局副本(hook 实际写入处)
    path.join(__dirname, '..', 'hook', 'device-cache.json'),
  ]) {
    try { const c = JSON.parse(fs.readFileSync(p, 'utf8')); if (c && c.ip) return String(c.ip); } catch (_) {}
  }
  return null;
}

// 设备目标解析,与 hook 实际所用对齐:CLAWD_DEVICE_IP > 全局 device.json > 仓库 device.json > 缓存 IP > clawd.local
// 若取到的是 clawd.local,优先换成缓存里的真实 IP(Node http 解析不了 mDNS 主机名)
function resolveDeviceTarget() {
  if (process.env.CLAWD_DEVICE_IP) return process.env.CLAWD_DEVICE_IP.trim();
  for (const p of [
    path.join(configDir(), 'hook', 'device.json'),         // 全局副本优先(hook 真正读的那份)
    path.join(__dirname, '..', 'hook', 'device.json'),
  ]) {
    try {
      const cfg = JSON.parse(fs.readFileSync(p, 'utf8'));
      if (cfg && cfg.device_ip) {
        const t = String(cfg.device_ip).trim();
        return (t === 'clawd.local') ? (resolveCachedIp() || t) : t;
      }
    } catch (_) {}
  }
  return resolveCachedIp() || 'clawd.local';
}

function deviceTest() {
  return new Promise((resolve) => {
    const target = resolveDeviceTarget();
    const r = http.get(`http://${target}/state`, (res) => { res.resume(); resolve({ ok: true, target }); });
    r.on('error', () => resolve({ ok: false, target }));
    r.setTimeout(1500, () => { r.destroy(); resolve({ ok: false, target }); });
  });
}

function startServer(port = DEFAULT_PORT) {
  let panel = '<h1>Clawd Agent</h1>';
  try { panel = fs.readFileSync(path.join(__dirname, 'panel.html'), 'utf8'); } catch (_) {}
  const server = http.createServer(async (rq, rs) => {
    const u = new URL(rq.url, 'http://127.0.0.1');
    if (rq.method === 'GET' && u.pathname === '/') {
      rs.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); rs.end(panel); return;
    }
    if (rq.method === 'GET' && u.pathname === '/state') {
      const cfg = readConfig(); const st = readState();
      rs.writeHead(200, { 'Content-Type': 'application/json' });
      rs.end(JSON.stringify({ ...cfg, lastSeen: st.lastSeen })); return;
    }
    if (rq.method === 'POST' && u.pathname === '/active') {
      let body = ''; rq.on('data', (c) => body += c);
      rq.on('end', () => {
        let tool = null; try { tool = JSON.parse(body).tool; } catch (_) {}
        const cfg = writeActiveTool(tool);
        rs.writeHead(200, { 'Content-Type': 'application/json' }); rs.end(JSON.stringify(cfg));
      });
      return;
    }
    if (rq.method === 'GET' && u.pathname === '/device/test') {
      const r = await deviceTest();
      rs.writeHead(200, { 'Content-Type': 'application/json' }); rs.end(JSON.stringify(r)); return;
    }
    rs.writeHead(404); rs.end('not found');
  });
  server.listen(port);
  return server;
}

if (require.main === module) {
  const port = Number(process.env.CLAWD_AGENT_PORT) || DEFAULT_PORT;
  startServer(port);
  console.log(`Clawd Agent 控制台: http://127.0.0.1:${port}`);
}

module.exports = { configDir, readConfig, writeActiveTool, readState, startServer, resolveDeviceTarget, DEFAULT_TOOLS, DEFAULT_PORT };
