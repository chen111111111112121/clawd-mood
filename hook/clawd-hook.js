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

main().catch(() => process.exit(0));
