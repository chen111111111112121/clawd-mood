#!/usr/bin/env node
/**
 * Clawd Mochi — Claude Code & Cursor → ESP32 status hook
 * GET http://<device-ip>/status?s=idle|thinking|working|done|alert|offline
 *   working 时附带 &act=read|edit|run|net|agent|work&info=<短文本>;CLAWD_DRY=1 仅打印 URL
 *
 * Config: hook/device.json(device_ip 可填 IP 或 clawd.local) or env CLAWD_DEVICE_IP
 *
 * Claude Code: argv event + stdin JSON (hook_event_name PascalCase)
 * Cursor:      stdin JSON only (hook_event_name camelCase)
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');
const dgram = require('dgram');

const CONFIG_PATH = path.join(__dirname, 'device.json');
const CACHE_PATH = path.join(__dirname, 'device-cache.json');
const TIMEOUT_MS = 2500;
const MDNS_TIMEOUT_MS = 700;
const CACHE_TTL_MS = 10 * 60 * 1000;

// Claude Code (PascalCase) + Cursor (camelCase)
const STATE_MAP = {
  // Claude Code
  SessionStart: 'idle',
  SessionEnd: 'offline',
  UserPromptSubmit: 'thinking',
  PreToolUse: 'working',
  PostToolUse: 'working',
  Stop: 'done',
  Notification: 'alert',
  Elicitation: 'alert',
  PostToolUseFailure: 'working',   // 普通工具失败不再弹 alert,保持工作态(只有真需确认才 alert)
  StopFailure: 'idle',             // 失败收尾回 idle,不庆祝也不弹 alert
  SubagentStart: 'working',
  SubagentStop: 'working',
  PreCompact: 'thinking',
  PostCompact: 'idle',
  // Cursor
  sessionStart: 'idle',
  sessionEnd: 'offline',
  beforeSubmitPrompt: 'thinking',
  preToolUse: 'working',
  postToolUse: 'working',
  stop: 'done',
  postToolUseFailure: 'working',   // 同上:工具失败保持工作态
  subagentStart: 'working',
  subagentStop: 'working',
  preCompact: 'thinking',
};

// Notification 误报过滤:Claude Code 的 Notification 不只在报错时触发,
// "Claude is waiting for your input"(空闲待命)、收尾提醒等日常通知也会发,
// 这些不该弹出 ALERT 的 logo。仅"权限/确认"类(消息明确需要你操作)才保留 alert。
const NOTIF_QUIET_RE = /waiting|idle|elapsed|still\s|finish|complete|\bdone\b/i;
function notificationIsAlert(payload) {
  const msg = (payload && typeof payload.message === 'string') ? payload.message : '';
  if (!msg) return false;                 // 无消息:多为待命提醒,忽略
  if (NOTIF_QUIET_RE.test(msg)) return false;  // 等待输入/收尾等日常通知,忽略
  return true;                            // 权限确认等真正需要注意的 → 保留 alert
}

// ── Tool semantics (state=working 時随 /status 附带) ─────────
const READ_TOOLS = new Set(['Read', 'Glob', 'Grep', 'NotebookRead']);
const EDIT_TOOLS = new Set(['Edit', 'Write', 'MultiEdit', 'NotebookEdit']);

function basename(p) {
  if (typeof p !== 'string' || !p) return '';
  const parts = p.replace(/\\/g, '/').split('/');
  return parts[parts.length - 1] || '';
}

function classifyTool(toolName, toolInput) {
  const name = typeof toolName === 'string' ? toolName : '';
  const input = (toolInput && typeof toolInput === 'object') ? toolInput : {};
  if (READ_TOOLS.has(name)) {
    return { act: 'read', info: basename(input.file_path) || (typeof input.pattern === 'string' ? input.pattern : '') };
  }
  if (EDIT_TOOLS.has(name)) {
    return { act: 'edit', info: basename(input.file_path) || basename(input.notebook_path) };
  }
  if (name === 'Bash') {
    const cmd = typeof input.command === 'string' ? input.command.trim() : '';
    return { act: 'run', info: cmd.split(/\s+/)[0] || '' };
  }
  if (name === 'WebFetch' || name === 'WebSearch') {
    let info = '';
    if (typeof input.url === 'string') {
      try { info = new URL(input.url).hostname; } catch (_) { info = input.url; }
    } else if (typeof input.query === 'string') {
      info = input.query;
    }
    return { act: 'net', info };
  }
  if (name === 'Task' || name === 'Agent') {
    return { act: 'agent', info: typeof input.description === 'string' ? input.description : '' };
  }
  return { act: 'work', info: name };
}

function sanitizeInfo(s) {
  if (typeof s !== 'string') return '';
  let out = '';
  for (const ch of s) {
    const code = ch.codePointAt(0);
    if (code >= 0x20 && code <= 0x7e) out += ch;
  }
  return out.trim().slice(0, 22);
}

function resolveSemantics(payload) {
  if (!payload || typeof payload !== 'object') return null;
  const { act, info } = classifyTool(payload.tool_name, payload.tool_input);
  const clean = sanitizeInfo(info) || sanitizeInfo(payload.tool_name || '');
  return { act, info: clean };
}

function buildStatusUrl(ip, state, semantics) {
  let url = `http://${ip}/status?s=${encodeURIComponent(state)}`;
  if (state === 'working' && semantics) {
    url += `&act=${encodeURIComponent(semantics.act)}`;
    if (semantics.info) url += `&info=${encodeURIComponent(semantics.info)}`;
  }
  return url;
}

// ── 多 AI 工具绑定:门控 ───────────────────────────────────────
function configDir() {
  return process.env.CLAWD_CONFIG_DIR || path.join(os.homedir(), '.clawd-mood');
}

// 确定本次 hook 来自哪款 AI 工具:CLAWD_SOURCE 环境变量 > --source= argv > 按事件名大小写推断
function resolveSource(event, env = process.env, argv = process.argv) {
  if (env.CLAWD_SOURCE) return String(env.CLAWD_SOURCE).trim();
  const a = (argv || []).find((x) => typeof x === 'string' && x.startsWith('--source='));
  if (a) return a.slice('--source='.length);
  if (typeof event === 'string' && /^[A-Z]/.test(event)) return 'cc';   // Claude Code: PascalCase
  return 'cursor';                                                       // Cursor: camelCase
}

// 读绑定配置;缺文件/坏 JSON/未设 -> null(=不门控,向后兼容)
function readActiveTool(dir = configDir()) {
  try {
    const cfg = JSON.parse(fs.readFileSync(path.join(dir, 'agent.json'), 'utf8'));
    const t = cfg && cfg.activeTool;
    return (typeof t === 'string' && t.length) ? t : null;
  } catch (_) { return null; }
}

// 设了 activeTool 且不等于本次来源 -> 门控
function isGated(activeTool, source) {
  return !!activeTool && activeTool !== source;
}

// hook 独占写 agent-state.json 的 lastSeen(给面板做活动指示);失败静默
function touchLastSeen(source, now, dir = configDir()) {
  try {
    const p = path.join(dir, 'agent-state.json');
    let st = {};
    try { st = JSON.parse(fs.readFileSync(p, 'utf8')) || {}; } catch (_) {}
    if (!st.lastSeen) st.lastSeen = {};
    st.lastSeen[source] = now;
    fs.mkdirSync(dir, { recursive: true });
    const tmp = `${p}.${process.pid}.tmp`;
    fs.writeFileSync(tmp, JSON.stringify(st));
    fs.renameSync(tmp, p);
  } catch (_) {}
}

function getConfiguredTarget() {
  if (process.env.CLAWD_DEVICE_IP) return process.env.CLAWD_DEVICE_IP.trim();
  try {
    const cfg = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
    if (cfg.device_ip) return String(cfg.device_ip).trim();
  } catch (_) {}
  return 'clawd.local';
}

function isIPv4(s) {
  return typeof s === 'string' && /^\d{1,3}(\.\d{1,3}){3}$/.test(s);
}

function buildMdnsQuery(name) {
  const labels = String(name).split('.').filter(Boolean);
  let qlen = 1;
  for (const l of labels) qlen += 1 + l.length;
  const buf = Buffer.alloc(12 + qlen + 4);
  buf.writeUInt16BE(1, 4);                          // QDCOUNT
  let off = 12;
  for (const l of labels) {
    buf.writeUInt8(l.length, off++);
    buf.write(l, off, 'ascii');
    off += l.length;
  }
  buf.writeUInt8(0, off++);
  buf.writeUInt16BE(1, off); off += 2;              // QTYPE A
  buf.writeUInt16BE(0x8001, off);                   // QCLASS IN + QU(单播应答)
  return buf;
}

function readDnsName(buf, off) {
  const parts = [];
  let pos = off, jumped = false, jumps = 0;
  for (;;) {
    if (pos >= buf.length || jumps > 8) return null;
    const len = buf[pos];
    if (len === 0) { pos++; break; }
    if ((len & 0xc0) === 0xc0) {                    // 压缩指针
      if (pos + 1 >= buf.length) return null;
      const ptr = ((len & 0x3f) << 8) | buf[pos + 1];
      if (!jumped) off = pos + 2;
      pos = ptr; jumped = true; jumps++;
      continue;
    }
    if (pos + 1 + len > buf.length) return null;
    parts.push(buf.toString('ascii', pos + 1, pos + 1 + len));
    pos += 1 + len;
  }
  return { name: parts.join('.'), next: jumped ? off : pos };
}

function parseMdnsAnswer(buf, wantName) {
  if (!Buffer.isBuffer(buf) || buf.length < 12) return null;
  const qd = buf.readUInt16BE(4);
  const an = buf.readUInt16BE(6);
  let off = 12;
  try {
    for (let i = 0; i < qd; i++) {                  // 跳过 question 区
      const n = readDnsName(buf, off);
      if (!n) return null;
      off = n.next + 4;
    }
    for (let i = 0; i < an; i++) {
      const n = readDnsName(buf, off);
      if (!n) return null;
      off = n.next;
      if (off + 10 > buf.length) return null;
      const type = buf.readUInt16BE(off);
      const rdlen = buf.readUInt16BE(off + 8);
      off += 10;
      if (off + rdlen > buf.length) return null;
      if (type === 1 && rdlen === 4 && n.name.toLowerCase() === String(wantName).toLowerCase()) {
        return `${buf[off]}.${buf[off + 1]}.${buf[off + 2]}.${buf[off + 3]}`;
      }
      off += rdlen;
    }
  } catch (_) {}
  return null;
}

function mdnsLookup(name, timeoutMs) {
  return new Promise((resolve) => {
    let sock;
    let finished = false;
    const done = (ip) => {
      if (finished) return;
      finished = true;
      try { sock.close(); } catch (_) {}
      resolve(ip);
    };
    try { sock = dgram.createSocket('udp4'); } catch (_) { resolve(null); return; }
    const timer = setTimeout(() => done(null), timeoutMs);
    sock.on('error', () => { clearTimeout(timer); done(null); });
    sock.on('message', (msg) => {
      const ip = parseMdnsAnswer(msg, name);
      if (ip) { clearTimeout(timer); done(ip); }
    });
    sock.bind(0, () => {
      try {
        sock.send(buildMdnsQuery(name), 5353, '224.0.0.251');
      } catch (_) { clearTimeout(timer); done(null); }
    });
  });
}

function readIpCache(host) {
  try {
    const c = JSON.parse(fs.readFileSync(CACHE_PATH, 'utf8'));
    if (c && c.host === host && isIPv4(c.ip)) return c;
  } catch (_) {}
  return null;
}

function writeIpCache(host, ip) {
  try {
    fs.writeFileSync(CACHE_PATH, JSON.stringify({ host, ip, ts: Date.now() }));
  } catch (_) {}
}

async function resolveDeviceIP() {
  const target = getConfiguredTarget();
  if (isIPv4(target)) return target;
  // 主机名(如 clawd.local):mDNS 组播直查,绕开被代理劫持的系统 DNS
  const cached = readIpCache(target);
  if (cached && Date.now() - cached.ts < CACHE_TTL_MS) return cached.ip;
  const ip = await mdnsLookup(target, MDNS_TIMEOUT_MS);
  if (ip) { writeIpCache(target, ip); return ip; }
  if (cached) return cached.ip;                     // 过期缓存兜底
  return target;                                    // 交给系统解析碰运气
}

function readStdin() {
  return new Promise((resolve) => {
    if (process.stdin.isTTY) {
      resolve('');
      return;
    }
    const chunks = [];
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (chunk) => chunks.push(chunk));
    process.stdin.on('end', () => resolve(chunks.join('')));
    process.stdin.on('error', () => resolve(chunks.join('')));
    setTimeout(() => resolve(chunks.join('')), 120);
  });
}

function writeCursorStdout(event) {
  // Cursor gating hooks expect valid JSON on stdout
  if (event === 'beforeSubmitPrompt') {
    process.stdout.write(JSON.stringify({ continue: true }) + '\n');
  }
}

async function pushState(state, semantics) {
  const url = buildStatusUrl(await resolveDeviceIP(), state, semantics);
  if (process.env.CLAWD_DRY === '1') {
    process.stdout.write(url + '\n');
    return true;
  }
  return new Promise((resolve) => {
    const req = http.get(url, (res) => {
      res.resume();
      resolve(true);
    });
    req.on('error', () => resolve(false));
    req.setTimeout(TIMEOUT_MS, () => {
      req.destroy();
      resolve(false);
    });
  });
}

async function main() {
  const stdinText = await readStdin();
  let payload = null;
  const text = (stdinText || '').trim();
  if (text) {
    try { payload = JSON.parse(text); } catch (_) {}
  }
  const argEvent = process.argv.slice(2).find((a) => a && !a.startsWith('--'));
  const event = (payload && payload.hook_event_name) || argEvent;
  writeCursorStdout(event);                       // 必须在门控前:Cursor 门控钩子要求始终输出 {"continue":true}
  const source = resolveSource(event);
  if (isGated(readActiveTool(), source)) process.exit(0);   // 非当前绑定工具:静默退出
  const state = STATE_MAP[event];
  if (!state) process.exit(0);
  // Notification 误报过滤:日常待命/收尾通知不推 alert,设备保持当前状态
  if (state === 'alert' && event === 'Notification' && !notificationIsAlert(payload)) {
    process.exit(0);
  }
  const semantics = state === 'working' ? resolveSemantics(payload) : null;
  await pushState(state, semantics);
  touchLastSeen(source, Date.now());
  process.exit(0);
}

module.exports = { classifyTool, sanitizeInfo, buildStatusUrl, resolveSemantics, STATE_MAP, buildMdnsQuery, parseMdnsAnswer, readDnsName, isIPv4, notificationIsAlert, resolveSource, readActiveTool, isGated, configDir, touchLastSeen };

if (require.main === module) {
  main().catch(() => process.exit(0));
}
