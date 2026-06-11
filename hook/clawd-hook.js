#!/usr/bin/env node
/**
 * Clawd Mochi — Claude Code status hook
 * Pushes session events to the ESP32 via HTTP GET /status?s=
 *
 * Configure device IP in hook/device.json or CLAWD_DEVICE_IP env var.
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const CONFIG_PATH = path.join(__dirname, 'device.json');
const TIMEOUT_MS = 2000;

const STATE_MAP = {
  SessionStart: 'idle',
  UserPromptSubmit: 'thinking',
  PreToolUse: 'working',
  PostToolUse: 'working',
  Stop: 'idle',
  Notification: 'alert',
  SessionEnd: 'offline',
};

function getDeviceIP() {
  if (process.env.CLAWD_DEVICE_IP) return process.env.CLAWD_DEVICE_IP;
  try {
    const cfg = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
    if (cfg.device_ip) return cfg.device_ip;
  } catch (_) {}
  return '192.168.1.100';
}

const event = process.argv[2];
const state = STATE_MAP[event];
if (!state) process.exit(0);

const ip = getDeviceIP();
const url = `http://${ip}/status?s=${state}`;

const req = http.get(url, { timeout: TIMEOUT_MS }, (res) => {
  res.resume();
});
req.on('error', () => {});
req.on('timeout', () => {
  req.destroy();
});
