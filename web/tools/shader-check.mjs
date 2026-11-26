// Validate the WGSL shader set in Chrome for ProStreet.
//
//   node web/tools/shader-check.mjs --from <dir>
//       check a dump on disk (emitted WGSL + test vectors)
// Options: --port <http port> (8612), --save <OPFS dir> (shadercheck), --timeout <ms> (300000),
//          --query '&flag' (extra shadercheck.html query, e.g. '&all2d' for a corpus dump)
// Exit code 0 only when every module compiles, every pipeline builds, the
// vertex-pulling test matches and the SHADERCHECK result has ok=true.
import { chromium } from 'playwright';
import { pickBrowser } from './browser.mjs';
import { createServer } from 'node:http';
import { readFileSync, statSync } from 'node:fs';
import { resolve, extname, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..');

const argv = process.argv.slice(2);
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const port = +opt('--port', 8612), save = opt('--save', 'shadercheck');
const from = opt('--from', null);
const timeout = +opt('--timeout', 300000);
const query = opt('--query', '');            // e.g. '&all2d' or '&cross'
const origin = `http://localhost:${port}`;

if (!from) { console.log('need --from <dir>'); process.exit(2); }
const fromDir = resolve(from);
const distDir = resolve(ROOT, 'dist-dev');

console.log(`serving ${distDir} on ${origin}`);

const server = createServer((req, res) => {
  const url = new URL(req.url, origin);
  let filePath = join(distDir, url.pathname);
  // Prevent path traversal
  if (!filePath.startsWith(distDir)) { res.writeHead(403); return res.end('Forbidden'); }
  try {
    const st = statSync(filePath);
    if (st.isDirectory()) { res.writeHead(404); return res.end('Not found'); }
    const mime = {
      '.js': 'application/javascript',
      '.mjs': 'application/javascript',
      '.html': 'text/html',
      '.css': 'text/css',
      '.wasm': 'application/wasm',
      '.map': 'application/json',
      '.json': 'application/json',
      '.txt': 'text/plain',
    }[extname(filePath)] || 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': mime });
    res.end(readFileSync(filePath));
  } catch {
    res.writeHead(404);
    res.end('Not found');
  }
});

await new Promise((resolve, reject) => {
  server.listen(port, '127.0.0.1', resolve);
  server.on('error', reject);
});

const browser = await chromium.launch({
  ...pickBrowser(),
  headless: !argv.includes('--headed'),
  args: ['--enable-unsafe-webgpu', ...(process.env.CHROME_ARGS ? process.env.CHROME_ARGS.split(' ') : [])]
});
const page = await browser.newPage();
let result = null;
page.on('console', m => {
  const t = m.text();
  if (t.startsWith('SHADERCHECK ')) { result = JSON.parse(t.slice(12)); return; }
  if (argv.includes('--quiet') && !/ERROR|PAGEERROR/.test(t)) return;
  console.log(t);
});
page.on('pageerror', e => console.log('PAGEERROR ' + e));

async function upload(dir) {
  await page.goto(`${origin}/`);
  const files = {};
  const fs = await import('node:fs');
  for (const n of fs.readdirSync(dir)) {
    if (n.endsWith('.wgsl') || n.endsWith('.vec') || n.endsWith('.pvec') || n.endsWith('.dvec')) {
      files[n] = readFileSync(join(dir, n)).toString('base64');
    }
  }
  await page.evaluate(async ([dir, files]) => {
    const root = await navigator.storage.getDirectory();
    const [a, b] = dir.split('/');
    try { await (await root.getDirectoryHandle(a)).removeEntry(b, { recursive: true }); } catch {}
    const d = await (await root.getDirectoryHandle(a, { create: true })).getDirectoryHandle(b, { create: true });
    for (const [n, b64] of Object.entries(files)) {
      const w = await (await d.getFileHandle(n, { create: true })).createWritable();
      await w.write(Uint8Array.from(atob(b64), c => c.charCodeAt(0))); await w.close();
    }
  }, [`${save}/shaders`, files]);
  console.log(`uploaded ${Object.keys(files).length} files from ${dir}`);
}

const waitDone = async ms => {
  try { await page.waitForFunction(() => document.title === 'DONE', null, { timeout: ms }); return true; }
  catch { return false; }
};

await upload(fromDir);

await page.goto(`${origin}/test/shadercheck.html?opfs=${save}/shaders${query}`);
if (!await waitDone(timeout)) console.log('TIMEOUT (shadercheck)');

await browser.close();
server.close();

if (!result) { console.log('no SHADERCHECK result'); process.exit(1); }
const { failures, ...summary } = result;
console.log('SHADERCHECK ' + JSON.stringify(summary));
for (const f of failures ?? []) console.log('  FAIL ' + f);
const ok = result.ok;
console.log(ok ? 'PASS' : 'FAIL');
process.exit(ok ? 0 : 1);