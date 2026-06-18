const test = require('node:test');
const assert = require('node:assert');
const { resolveSource, readActiveTool, isGated } = require('../clawd-hook.js');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFileSync } = require('child_process');

// ── resolveSource ─────────────────────────────────────────────
test('resolveSource: CLAWD_SOURCE 环境变量最优先', () => {
  assert.strictEqual(resolveSource('SessionStart', { CLAWD_SOURCE: 'lingma' }, []), 'lingma');
});
test('resolveSource: --source= 次之', () => {
  assert.strictEqual(resolveSource('SessionStart', {}, ['node', 'h.js', '--source=trae', 'SessionStart']), 'trae');
});
test('resolveSource: PascalCase 事件推断为 cc', () => {
  assert.strictEqual(resolveSource('UserPromptSubmit', {}, []), 'cc');
});
test('resolveSource: camelCase 事件推断为 cursor', () => {
  assert.strictEqual(resolveSource('beforeSubmitPrompt', {}, []), 'cursor');
});

// ── isGated / readActiveTool ──────────────────────────────────
test('isGated: 设了别的工具 -> 门控(true)', () => {
  assert.strictEqual(isGated('cc', 'cursor'), true);
});
test('isGated: 命中当前工具 -> 放行(false)', () => {
  assert.strictEqual(isGated('cc', 'cc'), false);
});
test('isGated: activeTool 为空 -> 放行(false,向后兼容)', () => {
  assert.strictEqual(isGated(null, 'cursor'), false);
  assert.strictEqual(isGated('', 'cursor'), false);
});
test('readActiveTool: 读临时目录 agent.json', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-cfg-'));
  fs.writeFileSync(path.join(dir, 'agent.json'), JSON.stringify({ activeTool: 'cc' }));
  assert.strictEqual(readActiveTool(dir), 'cc');
});
test('readActiveTool: 缺文件/坏 JSON -> null', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-cfg-'));
  assert.strictEqual(readActiveTool(dir), null);                 // 不存在
  fs.writeFileSync(path.join(dir, 'agent.json'), '{ broken');
  assert.strictEqual(readActiveTool(dir), null);                 // 坏 JSON
});

// ── main() 门控功能测试(子进程 + CLAWD_DRY) ────────────────────
const HOOK = path.join(__dirname, '..', 'clawd-hook.js');
function runHook(eventArgs, env) {
  try {
    return execFileSync('node', [HOOK, ...eventArgs], {
      env: { ...process.env, CLAWD_DRY: '1', ...env }, encoding: 'utf8', input: '',
    });
  } catch (e) { return (e.stdout || '').toString(); }
}

test('门控: activeTool=cc 时 cursor 事件不推送', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-cfg-'));
  fs.writeFileSync(path.join(dir, 'agent.json'), JSON.stringify({ activeTool: 'cc' }));
  const out = runHook(['--source=cursor', 'preToolUse'], { CLAWD_CONFIG_DIR: dir });
  assert.ok(!out.includes('/status'), '应被门控,无推送 URL');
});
test('门控: activeTool=cc 时 cc 事件照常推送', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-cfg-'));
  fs.writeFileSync(path.join(dir, 'agent.json'), JSON.stringify({ activeTool: 'cc' }));
  const out = runHook(['UserPromptSubmit'], { CLAWD_CONFIG_DIR: dir });
  assert.ok(out.includes('/status?s=thinking'), '应放行并推送 thinking');
});
test('门控: activeTool 未设时所有来源放行', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-cfg-'));
  const out = runHook(['--source=cursor', 'preToolUse'], { CLAWD_CONFIG_DIR: dir });
  assert.ok(out.includes('/status?s=working'), '无配置应放行');
});
test('门控命中后写 agent-state.json lastSeen', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-cfg-'));
  runHook(['UserPromptSubmit'], { CLAWD_CONFIG_DIR: dir });
  const st = JSON.parse(fs.readFileSync(path.join(dir, 'agent-state.json'), 'utf8'));
  assert.ok(st.lastSeen && typeof st.lastSeen.cc === 'number');
});

// ── 装机布局:不带 CLAWD_CONFIG_DIR 时,hook 按自身位置(<cfg>/hook/)定位配置目录 ──
// 复现宿主 AI 工具 spawn hook 的真实环境:它不会传 CLAWD_CONFIG_DIR,
// hook 须靠 <cfg>/hook/clawd-hook.js 的上级目录读到面板写的 agent.json,否则门控失效。
test('装机布局: 无 CLAWD_CONFIG_DIR 时按 hook 位置读 agent.json 门控', () => {
  const cfg = fs.mkdtempSync(path.join(os.tmpdir(), 'clawd-install-'));
  const hookDir = path.join(cfg, 'hook');
  fs.mkdirSync(hookDir, { recursive: true });
  fs.copyFileSync(HOOK, path.join(hookDir, 'clawd-hook.js'));               // 模拟安装复制
  fs.writeFileSync(path.join(cfg, 'agent.json'), JSON.stringify({ activeTool: 'cc' }));
  // 显式清掉 CLAWD_CONFIG_DIR,运行 <cfg>/hook/ 下的副本
  const env = { ...process.env, CLAWD_DRY: '1' };
  delete env.CLAWD_CONFIG_DIR;
  let out = '';
  try {
    out = execFileSync('node', [path.join(hookDir, 'clawd-hook.js'), '--source=cursor', 'preToolUse'],
                       { env, encoding: 'utf8', input: '' });
  } catch (e) { out = (e.stdout || '').toString(); }
  assert.ok(!out.includes('/status'), 'cursor 事件应被 activeTool=cc 门控(按 hook 位置读到了 agent.json)');
});
