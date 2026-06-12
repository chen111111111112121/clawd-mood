const test = require('node:test');
const assert = require('node:assert');
const {
  classifyTool, sanitizeInfo, buildStatusUrl, resolveSemantics,
} = require('../clawd-hook.js');

// ── classifyTool ──────────────────────────────────────────────
test('Edit -> act=edit, info=basename (Windows path)', () => {
  assert.deepStrictEqual(
    classifyTool('Edit', { file_path: 'D:\\proj\\src\\main.cpp' }),
    { act: 'edit', info: 'main.cpp' });
});

test('Write/NotebookEdit 也归 edit', () => {
  assert.strictEqual(classifyTool('Write', { file_path: 'a/b.md' }).act, 'edit');
  assert.strictEqual(classifyTool('NotebookEdit', { notebook_path: 'n.ipynb' }).act, 'edit');
});

test('Read -> act=read, info=basename', () => {
  assert.deepStrictEqual(
    classifyTool('Read', { file_path: '/home/u/x/notes.md' }),
    { act: 'read', info: 'notes.md' });
});

test('Grep 无 file_path 时用 pattern', () => {
  assert.deepStrictEqual(
    classifyTool('Grep', { pattern: 'TODO' }),
    { act: 'read', info: 'TODO' });
});

test('Bash -> act=run, info=命令首词', () => {
  assert.deepStrictEqual(
    classifyTool('Bash', { command: '  git log --oneline -5' }),
    { act: 'run', info: 'git' });
});

test('WebFetch -> act=net, info=域名', () => {
  assert.deepStrictEqual(
    classifyTool('WebFetch', { url: 'https://docs.arduino.cc/x/y' }),
    { act: 'net', info: 'docs.arduino.cc' });
});

test('WebSearch -> act=net, info=query', () => {
  assert.deepStrictEqual(
    classifyTool('WebSearch', { query: 'esp32 spring animation' }),
    { act: 'net', info: 'esp32 spring animation' });
});

test('Task -> act=agent, info=description', () => {
  assert.deepStrictEqual(
    classifyTool('Task', { description: 'explore codebase' }),
    { act: 'agent', info: 'explore codebase' });
});

test('未知工具 -> act=work, info=工具名', () => {
  assert.deepStrictEqual(
    classifyTool('SomeNewTool', {}),
    { act: 'work', info: 'SomeNewTool' });
});

test('缺 tool_name/tool_input 不抛异常', () => {
  assert.strictEqual(classifyTool(undefined, undefined).act, 'work');
});

// ── sanitizeInfo ──────────────────────────────────────────────
test('sanitize: 中文剔除只剩扩展名', () => {
  assert.strictEqual(sanitizeInfo('工程分析.md'), '.md');
});

test('sanitize: 超长截断 22 字符', () => {
  assert.strictEqual(sanitizeInfo('a'.repeat(40)).length, 22);
});

test('sanitize: 控制字符剔除、首尾空白去除', () => {
  assert.strictEqual(sanitizeInfo('  a\tb\nc  '), 'abc');
});

test('sanitize: 非字符串返回空串', () => {
  assert.strictEqual(sanitizeInfo(null), '');
});

// ── resolveSemantics ──────────────────────────────────────────
test('resolveSemantics: 纯中文 info 退化为 tool_name', () => {
  assert.deepStrictEqual(
    resolveSemantics({ tool_name: 'Read', tool_input: { file_path: '工程分析' } }),
    { act: 'read', info: 'Read' });
});

test('resolveSemantics: payload 为 null 返回 null', () => {
  assert.strictEqual(resolveSemantics(null), null);
});

// ── buildStatusUrl ────────────────────────────────────────────
test('working + 语义 -> 带 act/info 且 URL 编码', () => {
  assert.strictEqual(
    buildStatusUrl('1.2.3.4', 'working', { act: 'edit', info: 'a b.cpp' }),
    'http://1.2.3.4/status?s=working&act=edit&info=a%20b.cpp');
});

test('非 working 状态忽略语义', () => {
  assert.strictEqual(
    buildStatusUrl('1.2.3.4', 'thinking', { act: 'edit', info: 'x' }),
    'http://1.2.3.4/status?s=thinking');
});

test('info 为空时省略 info 参数', () => {
  assert.strictEqual(
    buildStatusUrl('1.2.3.4', 'working', { act: 'run', info: '' }),
    'http://1.2.3.4/status?s=working&act=run');
});
