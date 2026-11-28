// The page's unit tests (web/test/*.test.mjs) under node --test, with a per-file time limit so a hang fails that test
// instead of holding the run (2 minutes with --fast, 5 minutes otherwise):
//   node web/tools/unit-tests.mjs           every file
//   node web/tools/unit-tests.mjs --fast    same files; the flag is kept for existing scripts and CI
// Other arguments go to node --test (--test-reporter=spec ...).  Also npm test / npm run test:fast in web/.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const TEST = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../test');
// No slow tests are ported yet; keep the flag accepted by scripts that still pass it.
const SLOW = new Set();
const argv = process.argv.slice(2);
const fast = argv.includes('--fast');
const files = fs.readdirSync(TEST).filter(f => f.endsWith('.test.mjs') && !(fast && SLOW.has(f.slice(0, -'.test.mjs'.length))))
  .sort().map(f => path.join(TEST, f));
const r = spawnSync(process.execPath, ['--test', `--test-timeout=${fast ? 120_000 : 300_000}`, ...argv.filter(a => a !== '--fast'), ...files], { stdio: 'inherit' });
process.exit(r.status ?? 1);
