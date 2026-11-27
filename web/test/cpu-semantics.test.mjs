// Synthetic CPU shader semantics: cmp, lrp, +0/-0/NaN sources and dest masks.
//   node --test web/test/cpu-semantics.test.mjs
// Skipped when bash or a C compiler is unavailable; set CC to choose one.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SCRIPT = path.join(REPO, 'lifter/tools/cpu_shader_semantics_test.sh');
const VSEMU = path.join(REPO, 'web/runtime/web/runtime/vsemu.c');
const OUT = path.join(REPO, 'out/web/test/cpu-semantics');
fs.mkdirSync(OUT, { recursive: true });
const haveBash = spawnSync('bash', ['--version'], { encoding: 'utf8' }).status === 0;
const run = (src = VSEMU) => spawnSync('bash', [SCRIPT, src], {
  cwd: REPO, encoding: 'utf8', timeout: 120_000,
  env: { ...process.env, CC: process.env.CC || 'cc' },
});

test('CPU shader semantics: cmp/lrp/edge/mask outputs match D3D9', t => {
  if (!haveBash) return t.skip('no bash');
  const r = run();
  if (r.status === 77) return t.skip(r.stderr.trim());
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /oT0\(cmp\) = \(21, 12, 23, 14\)/);
  assert.match(r.stdout, /oT1\(lrp\) = \(31, 12, 28, 4\)/);
  assert.match(r.stdout, /oT2\(edges\) = \(11, 12, 23, 14\)/);
  assert.match(r.stdout, /oT3\(mask\) = \(21, 0, 0, 0\)/);
});

test('CPU shader semantics: removing O_CMP fails the synthetic test', t => {
  if (!haveBash) return t.skip('no bash');
  const original = fs.readFileSync(VSEMU, 'utf8');
  const patched = original.replace(/        case O_CMP:[^\r\n]*\r?\n[ \t]*DSTW\(x,d\); break;\r?\n/, '');
  assert.notEqual(patched, original, 'O_CMP case not found in vsemu.c');
  const src = path.join(OUT, 'vsemu-no-cmp.c');
  fs.writeFileSync(src, patched);
  const r = run(src);
  if (r.status === 77) return t.skip(r.stderr.trim());
  assert.equal(r.status, 1, r.stdout + r.stderr);
  assert.match(r.stderr, /oT0\(cmp\): FAIL/);
});

test('CPU shader semantics: swapping LRP operands fails the synthetic test', t => {
  if (!haveBash) return t.skip('no bash');
  const original = fs.readFileSync(VSEMU, 'utf8');
  const good = 'd[i]=s[2][i]+s[0][i]*(s[1][i]-s[2][i])';
  const bad = 'd[i]=s[1][i]+s[0][i]*(s[2][i]-s[1][i])';
  assert.ok(original.includes(good), 'correct LRP formula not found in vsemu.c');
  const src = path.join(OUT, 'vsemu-swap-lrp.c');
  fs.writeFileSync(src, original.replace(good, bad));
  const r = run(src);
  if (r.status === 77) return t.skip(r.stderr.trim());
  assert.equal(r.status, 1, r.stdout + r.stderr);
  assert.match(r.stderr, /oT1\(lrp\): FAIL/);
});
