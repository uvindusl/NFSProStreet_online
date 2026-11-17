// Compare the page side of the GPU structures (src/gpu/proto.ts) with the C
// side (runtime/web/gpu_proto.h).  proto.ts says "Keep in step with the C
// header; web/tools/check-gpu-proto.mjs compares the two" -- this is that file.
//
// It reads both sources as text (proto.ts uses `const enum`, which node's type
// stripping cannot import) and checks:
//   * GW_MAGIC, GW_VERSION
//   * sizeof(GwDraw) / sizeof(GwFrame) / sizeof(GwCtl) against
//     DRAW_BYTES / FRAME_BYTES / CTL_BYTES
//   * every _Static_assert(offsetof(GwDraw, field) == N) in the header against
//     the D word index * 4
//   * the GW_LAYOUT / LAYOUT layout word
//
// Usage: node web/tools/check-gpu-proto.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const h = fs.readFileSync(path.join(web, 'runtime/web/gpu_proto.h'), 'utf8');
const ts = fs.readFileSync(path.join(web, 'src/gpu/proto.ts'), 'utf8');

const fail = [];
const ok = [];
const eq = (what, a, b) => { (a === b ? ok : fail).push(`${what}: C ${a} vs TS ${b}`); };

// --- #define GW_X in the header -> number
const hdef = (name) => {
  const m = h.match(new RegExp(`^#define\\s+${name}\\s+(0x[0-9A-Fa-f]+|[0-9]+)u?`, 'm'));
  if (!m) throw new Error(`gpu_proto.h: no #define ${name}`);
  return Number(m[1]);
};
// --- export const NAME = <number> in proto.ts (also NAME in a comma list)
const tconst = (name) => {
  const m = ts.match(new RegExp(`(?:export const\\s+|,\\s*)${name}\\s*=\\s*(0x[0-9A-Fa-f]+|[0-9]+)`));
  if (!m) throw new Error(`proto.ts: no export const ${name}`);
  return Number(m[1]);
};

eq('GW_MAGIC', hdef('GW_MAGIC'), tconst('GW_MAGIC'));
eq('GW_VERSION', hdef('GW_VERSION'), tconst('GW_VERSION'));

const hSize = (tag) => {
  const m = h.match(new RegExp(`_Static_assert\\(sizeof\\(${tag}\\)\\s*==\\s*([0-9]+)`));
  if (!m) throw new Error(`gpu_proto.h: no sizeof(${tag}) assert`);
  return Number(m[1]);
};
eq('sizeof(GwDraw)', hSize('GwDraw'), tconst('DRAW_BYTES'));
eq('sizeof(GwFrame)', hSize('GwFrame'), tconst('FRAME_BYTES'));
eq('sizeof(GwCtl)', hSize('GwCtl'), tconst('CTL_BYTES'));
eq('DRAW_WORDS', tconst('DRAW_BYTES') / 4, tconst('DRAW_WORDS'));

// --- the D enum: name -> word index
const dEnum = ts.match(/export const enum D\s*\{([^}]*)\}/);
if (!dEnum) throw new Error('proto.ts: no enum D');
const D = {};
for (const part of dEnum[1].split(',')) {
  const m = part.match(/(\w+)\s*=\s*(\d+)/);
  if (m) D[m[1]] = Number(m[2]);
}

// --- every offsetof(GwDraw, field) == N assert, against D[field] * 4
const camel = (s) => s.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
let n = 0;
for (const m of h.matchAll(/_Static_assert\(offsetof\(GwDraw,\s*(\w+)\)\s*==\s*(\d+)/g)) {
  const field = m[1], off = Number(m[2]);
  if (!(camel(field) in D)) { fail.push(`GwDraw.${field}: no D.${camel(field)} in proto.ts`); continue; }
  eq(`offsetof(GwDraw, ${field})`, off, D[camel(field)] * 4);
  n++;
}
if (!n) throw new Error('gpu_proto.h: no offsetof(GwDraw, ...) asserts found');

// --- the layout word
const hLayout = h.match(/#define\s+GW_LAYOUT\s+(.*)/);
const tLayout = ts.match(/export const LAYOUT\s*=\s*(.*)/);
if (!hLayout || !tLayout) throw new Error('no GW_LAYOUT / LAYOUT');
const hVal = (hSize('GwDraw') | (hSize('GwFrame') << 8) | ((hSize('GwCtl') & 0xFFF) << 16)) ^ (hdef('GW_VERSION') << 28);
const tVal = (tconst('DRAW_BYTES') | (tconst('FRAME_BYTES') << 8) | ((tconst('CTL_BYTES') & 0xFFF) << 16)) ^ (tconst('GW_VERSION') << 28);
eq('LAYOUT', hVal >>> 0, tVal >>> 0);

for (const l of ok) console.log(`  ok   ${l}`);
for (const l of fail) console.log(`  FAIL ${l}`);
console.log(`${ok.length} ok, ${fail.length} failed`);
process.exit(fail.length ? 1 : 0);
