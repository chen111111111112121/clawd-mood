const { test } = require('node:test');
const assert = require('node:assert');
const { aggregateEvents } = require('../clawd-agent.js');

const MIN = 60 * 1000;
const base = new Date(2026, 5, 17, 9, 0).getTime();
const at = (m) => base + m * MIN;

test('空数组 → 全 0 空集', () => {
  const r = aggregateEvents([]);
  assert.strictEqual(r.tool, null);
  assert.strictEqual(r.activeMs, 0);
  assert.strictEqual(r.sessions, 0);
  assert.strictEqual(r.asks, 0);
  assert.strictEqual(r.longestFocusMs, 0);
  assert.deepStrictEqual(r.naps, []);
  assert.deepStrictEqual(r.segments, []);
});

test('计数:会话=SessionStart,提问=UserPromptSubmit/beforeSubmitPrompt', () => {
  const r = aggregateEvents([
    { ts: at(0), tool: 'cc', event: 'SessionStart' },
    { ts: at(1), tool: 'cc', event: 'UserPromptSubmit' },
    { ts: at(2), tool: 'cursor', event: 'beforeSubmitPrompt' },
    { ts: at(3), tool: 'cc', event: 'Stop' },
  ]);
  assert.strictEqual(r.sessions, 1);
  assert.strictEqual(r.asks, 2);
  assert.strictEqual(r.tool, 'cc'); // 出现最多
});

test('活跃段:>20min 间隙切段,扣间隙得真实活跃时长', () => {
  const r = aggregateEvents([
    { ts: at(0),  tool: 'cc', event: 'UserPromptSubmit' },  // 段1 09:00
    { ts: at(10), tool: 'cc', event: 'UserPromptSubmit' },  // 段1 09:10 (间隔10<=20)
    { ts: at(40), tool: 'cc', event: 'UserPromptSubmit' },  // 间隔30>20 → 切段;段2 09:40
    { ts: at(45), tool: 'cc', event: 'Stop' },              // 段2 09:45
  ]);
  assert.strictEqual(r.segments.length, 2);
  assert.strictEqual(r.activeMs, (10 + 5) * MIN);           // 10min + 5min
  assert.strictEqual(r.longestFocusMs, 10 * MIN);
  assert.strictEqual(r.naps.length, 1);
  assert.strictEqual(r.naps[0].ms, 30 * MIN);               // 09:10→09:40 它睡了
  assert.strictEqual(r.firstTs, at(0));
  assert.strictEqual(r.lastTs, at(45));
});

test('边界:间隔恰好 20min 归同段(条件是 >gap 才切)', () => {
  const r = aggregateEvents([
    { ts: at(0),  tool: 'cc', event: 'Stop' },
    { ts: at(20), tool: 'cc', event: 'Stop' },
  ]);
  assert.strictEqual(r.segments.length, 1);
  assert.strictEqual(r.naps.length, 0);
  assert.strictEqual(r.activeMs, 20 * MIN);
});

test('乱序输入按 ts 排序后聚合', () => {
  const r = aggregateEvents([
    { ts: at(5), tool: 'cc', event: 'Stop' },
    { ts: at(0), tool: 'cc', event: 'UserPromptSubmit' },
  ]);
  assert.strictEqual(r.firstTs, at(0));
  assert.strictEqual(r.activeMs, 5 * MIN);
});
