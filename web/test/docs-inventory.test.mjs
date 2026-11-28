import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const WEB = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = path.resolve(WEB, '..');
const inventoryText = fs.readFileSync(path.join(WEB, 'test', 'PORTED.md'), 'utf8');

const rowsFrom = (text, section) => {
  const marker = `## ${section}`;
  const start = text.indexOf(marker);
  assert.notEqual(start, -1, `missing section: ${section}`);
  const rest = text.slice(start + marker.length);
  const next = rest.search(/^## /m);
  const block = next === -1 ? rest : rest.slice(0, next);
  return block.split(/\r?\n/)
    .map(line => line.trim())
    .filter(line => line.startsWith('|'))
    .slice(2)
    .map(line => line.split('|')[1]?.trim().replaceAll('`', ''))
    .filter(Boolean);
};

const listedFiles = [
  ...rowsFrom(inventoryText, 'Copied files'),
  ...rowsFrom(inventoryText, 'New in this port'),
];
const absoluteFor = name => name.startsWith('web/') ? path.join(ROOT, name) : path.join(WEB, 'test', name);

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

test('test inventory table lists every test and the real runner', () => {
  const tests = fs.readdirSync(path.join(WEB, 'test')).filter(name => name.endsWith('.test.mjs'));
  assert.deepEqual(tests.filter(name => !listedFiles.includes(name)), []);
  assert.ok(listedFiles.includes('web/tools/unit-tests.mjs'));
});

test('listed inventory files exist', () => {
  assert.deepEqual(listedFiles.filter(name => !fs.existsSync(absoluteFor(name))), []);
});

test('inventory parser ignores prose mentions', () => {
  const fake = [
    '# Fake inventory',
    '',
    '## Copied files',
    '',
    '| File | Reason |',
    '|------|--------|',
    '| real.test.mjs | listed in the table |',
    '',
    'Prose ghost.test.mjs is not a row.',
  ].join('\n');
  assert.deepEqual(rowsFrom(fake, 'Copied files'), ['real.test.mjs']);
});
