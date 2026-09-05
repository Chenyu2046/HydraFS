#!/usr/bin/env node
import assert from 'node:assert/strict';
import { createAiMarkdownBlock } from './benchmark_workload.mjs';

const block = createAiMarkdownBlock(128 * 1024, 0, 12345);
assert.equal(block.length, 128 * 1024);
assert.ok(block.includes(Buffer.from('# Knowledge Base Document')));
assert.ok(block.includes(Buffer.from('```cpp')));
assert.ok(block.includes(Buffer.from('| field | value |')));
assert.ok(block.includes(Buffer.from('[source](')));

const same = createAiMarkdownBlock(128 * 1024, 0, 12345);
assert.deepEqual(block, same, 'AI MD workload must be deterministic');
assert.notDeepEqual(block, createAiMarkdownBlock(128 * 1024, 64 * 1024, 12345),
  'different document offsets must not collapse to one payload');
console.log('PASS: AI Markdown workload shape and determinism');
