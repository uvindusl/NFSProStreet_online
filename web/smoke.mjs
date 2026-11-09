import fs from 'node:fs/promises';

const file = process.argv[2] ?? 'out/web/prostreet.wasm';
const bytes = await fs.readFile(file);
const { instance } = await WebAssembly.instantiate(bytes);
const count = instance.exports.prostreet_diagnostic();
if (count !== 27097 || instance.exports.prostreet_status() !== 0)
  throw new Error(`unexpected diagnostic result: functions=${count}, status=${instance.exports.prostreet_status()}`);
console.log(`wasm smoke OK: ${file} (${count} lifted functions)`);
