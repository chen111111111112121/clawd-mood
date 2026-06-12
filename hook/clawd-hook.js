#!/usr/bin/env node
/**
 * Clawd Mochi — Claude Code & Cursor → ESP32 status hook
 * GET http://<device-ip>/status?s=idle|thinking|working|done|alert|offline
 *
 * Config: hook/device.json  or  env CLAWD_DEVICE_IP
 *
 * Claude Code: argv event + stdin JSON (hook_event_name PascalCase)
 * Cursor:      stdin JSON only (hook_event_name camelCase)
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const CONFIG_PATH = path.join(__dirname, 'device.json');
const TIMEOUT_MS = 2500;

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
  PostToolUseFailure: 'alert',
  StopFailure: 'alert',
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
  postToolUseFailure: 'alert',
  subagentStart: 'working',
  subagentStop: 'working',
  preCompact: 'thinking',
};

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

function getDeviceIP() {
  if (process.env.CLAWD_DEVICE_IP) return process.env.CLAWD_DEVICE_IP.trim();
  try {
    const cfg = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
    if (cfg.device_ip) return String(cfg.device_ip).trim();
  } catch (_) {}
  return '192.168.150.21';
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

function resolveEvent(stdinText) {
  let event = process.argv[2];
  const text = (stdinText || '').trim();
  if (text) {
    try {
      const payload = JSON.parse(text);
      if (payload.hook_event_name) event = payload.hook_event_name;
    } catch (_) {}
  }
  return event;
}

function writeCursorStdout(event) {
  // Cursor gating hooks expect valid JSON on stdout
  if (event === 'beforeSubmitPrompt') {
    process.stdout.write(JSON.stringify({ continue: true }) + '\n');
  }
}

function pushState(state) {
  return new Promise((resolve) => {
    const ip = getDeviceIP();
    const url = `http://${ip}/status?s=${encodeURIComponent(state)}`;
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
  const event = resolveEvent(stdinText);
  writeCursorStdout(event);
  const state = STATE_MAP[event];
  if (!state) process.exit(0);
  await pushState(state);
  process.exit(0);
}

if (require.main === module) {
  main().catch(() => process.exit(0));
} else {
  module.exports = { classifyTool, sanitizeInfo, buildStatusUrl, resolveSemantics, STATE_MAP };
}
