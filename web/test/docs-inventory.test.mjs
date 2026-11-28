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

test('listed inventory files exist', () => {
  const inventory = fs.readFileSync(path.join(WEB, 'test', 'PORTED.md'), 'utf8');
  const missing = [];

  for (const section of ['Copied files', 'New in this port']) {
    const marker = `## ${section}`;
    const start = inventory.indexOf(marker);
    assert.notEqual(start, -1, `missing section: ${section}`);
    const rest = inventory.slice(start + marker.length);
    const next = rest.search(/^## /m);
    const block = next === -1 ? rest : rest.slice(0, next);
    const names = block.split(/\r?\n/)
      .map(line => line.trim())
      .filter(line => line.startsWith('|'))
      .slice(2)
      .map(line => line.split('|')[1].trim().replaceAll('`', ''))
      .filter(Boolean);

    for (const name of names) {
      const absolute = name.startsWith('web/') ? path.join(ROOT, name) : path.join(WEB, 'test', name);
      if (!fs.existsSync(absolute)) missing.push({ section, name });
    }
  }

  assert.deepEqual(missing, []);
});
