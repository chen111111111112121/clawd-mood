const test = require('node:test');
const assert = require('node:assert');
const {
  classifyTool, sanitizeInfo, buildStatusUrl, resolveSemantics,
  buildMdnsQuery, parseMdnsAnswer, isIPv4,
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

// ── mDNS 报文 ─────────────────────────────────────────────────
test('buildMdnsQuery: clawd.local 报文结构正确', () => {
  const b = buildMdnsQuery('clawd.local');
  assert.strictEqual(b.readUInt16BE(4), 1);                    // QDCOUNT=1
  assert.strictEqual(b[12], 5);                                // "clawd" 长度
  assert.strictEqual(b.toString('ascii', 13, 18), 'clawd');
  assert.strictEqual(b[18], 5);                                // "local" 长度
  assert.strictEqual(b.toString('ascii', 19, 24), 'local');
  assert.strictEqual(b[24], 0);                                // 根标签
  assert.strictEqual(b.readUInt16BE(25), 1);                   // QTYPE A
  assert.strictEqual(b.readUInt16BE(27), 0x8001);              // QCLASS IN + QU
  assert.strictEqual(b.length, 29);
});

test('parseMdnsAnswer: 标准应答提取 A 记录', () => {
  // 手工构造:header(an=1) + answer(无压缩): clawd.local A 192.168.1.50
  const name = Buffer.from([5, 99, 108, 97, 119, 100, 5, 108, 111, 99, 97, 108, 0]);
  const hdr = Buffer.alloc(12); hdr.writeUInt16BE(0x8400, 2); hdr.writeUInt16BE(1, 6); // QR+AA, ANCOUNT=1
  const rr = Buffer.alloc(10); rr.writeUInt16BE(1, 0); rr.writeUInt16BE(0x8001, 2);    // TYPE A, CLASS
  rr.writeUInt32BE(120, 4); rr.writeUInt16BE(4, 8);                                    // TTL, RDLEN
  const ip = Buffer.from([192, 168, 1, 50]);
  const msg = Buffer.concat([hdr, name, rr, ip]);
  assert.strictEqual(parseMdnsAnswer(msg, 'clawd.local'), '192.168.1.50');
});

test('parseMdnsAnswer: 压缩指针应答可解析', () => {
  // header(qd=1, an=1) + question(clawd.local) + answer 名字用 0xC00C 指回 question
  const hdr = Buffer.alloc(12); hdr.writeUInt16BE(1, 4); hdr.writeUInt16BE(1, 6);
  const qname = Buffer.from([5, 99, 108, 97, 119, 100, 5, 108, 111, 99, 97, 108, 0]);
  const qtail = Buffer.alloc(4); qtail.writeUInt16BE(1, 0); qtail.writeUInt16BE(1, 2);
  const ptr = Buffer.from([0xc0, 0x0c]);
  const rr = Buffer.alloc(10); rr.writeUInt16BE(1, 0); rr.writeUInt16BE(1, 2);
  rr.writeUInt32BE(120, 4); rr.writeUInt16BE(4, 8);
  const ip = Buffer.from([10, 0, 0, 7]);
  const msg = Buffer.concat([hdr, qname, qtail, ptr, rr, ip]);
  assert.strictEqual(parseMdnsAnswer(msg, 'clawd.local'), '10.0.0.7');
});

test('parseMdnsAnswer: 名字不匹配返回 null', () => {
  const name = Buffer.from([5, 111, 116, 104, 101, 114, 5, 108, 111, 99, 97, 108, 0]); // other.local
  const hdr = Buffer.alloc(12); hdr.writeUInt16BE(1, 6);
  const rr = Buffer.alloc(10); rr.writeUInt16BE(1, 0); rr.writeUInt16BE(4, 8);
  const msg = Buffer.concat([hdr, name, rr, Buffer.from([1, 2, 3, 4])]);
  assert.strictEqual(parseMdnsAnswer(msg, 'clawd.local'), null);
});

test('parseMdnsAnswer: 垃圾输入不抛异常', () => {
  assert.strictEqual(parseMdnsAnswer(Buffer.from([1, 2, 3]), 'clawd.local'), null);
  assert.strictEqual(parseMdnsAnswer(Buffer.alloc(0), 'clawd.local'), null);
});

test('isIPv4: 判定', () => {
  assert.strictEqual(isIPv4('192.168.1.1'), true);
  assert.strictEqual(isIPv4('clawd.local'), false);
  assert.strictEqual(isIPv4(''), false);
});

// ── 默认主机名走 mDNS 路径 ─────────────────────────────────────
test('clawd.local 不是 IPv4,会进入 mDNS 解析分支', () => {
  assert.strictEqual(isIPv4('clawd.local'), false);
  assert.strictEqual(isIPv4('192.168.1.5'), true);
});
