import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const WEB = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(WEB, '..');

test('debug README only indexes notes that exist', () => {
  const text = fs.readFileSync(path.join(ROOT, 'debug', 'README.md'), 'utf8');
  const indexed = text.split(/\r?\n/)
    .map(line => line.trim())
    .filter(line => line.startsWith('| `'))
    .map(line => line.split('`')[1])
    .filter(name => name.endsWith('.md'));
  const missing = [...new Set(indexed)].filter(name => !fs.existsSync(path.join(ROOT, 'debug', name)));
  assert.deepEqual(missing, []);
});

test('test inventory lists every test and the real runner', () => {
  const inventory = fs.readFileSync(path.join(WEB, 'test', 'PORTED.md'), 'utf8');
  const tests = fs.readdirSync(path.join(WEB, 'test')).filter(name => name.endsWith('.test.mjs'));
  assert.deepEqual(tests.filter(name => !inventory.includes(name)), []);
  assert.ok(inventory.includes('web/tools/unit-tests.mjs'));
});
