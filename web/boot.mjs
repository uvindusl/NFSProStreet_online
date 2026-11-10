// Headless boot harness for the browser module.
//
//   node web/boot.mjs [game/nfsps.exe] [out/web/prostreet.wasm]
//
// Loads the user's PE image into the module's linear memory the same way the
// page does (DRM sections never mapped), calls prostreet_boot(), and prints
// the status plus the first import the runtime has no shim for.  This is the
// S2/S3 measurement loop: it runs without a browser and without committing
// anything derived from the game.

import fs from 'node:fs';
import path from 'node:path';

const exePath = process.argv[2] ?? 'game/nfsps.exe';
const wasmPath = process.argv[3] ?? 'out/web/prostreet.wasm';

// The DRM sections are never part of the image (AGENTS.md hard rule 3).
const DRM = new Set(['ars', 'est', 'artem', 'celare', 'securom']);

function peSections(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint16(0, true) !== 0x5a4d) throw new Error('not PE32');
  const pe = view.getUint32(0x3c, true);
  if (view.getUint32(pe, true) !== 0x00004550) throw new Error('bad PE signature');
  const count = view.getUint16(pe + 6, true);
  const optionalSize = view.getUint16(pe + 20, true);
  const optional = pe + 24;
  if (view.getUint16(optional, true) !== 0x10b) throw new Error('not 32-bit PE');
  const imageBase = view.getUint32(optional + 28, true);
  const entry = view.getUint32(optional + 16, true);
  const headerSize = view.getUint32(optional + 60, true);
  const table = optional + optionalSize;
  const sections = [];
  for (let i = 0; i < count; i++) {
    const at = table + i * 40;
    let name = '';
    for (let c = 0; c < 8 && view.getUint8(at + c); c++)
      name += String.fromCharCode(view.getUint8(at + c));
    const rva = view.getUint32(at + 12, true);
    const rawSize = view.getUint32(at + 16, true);
    const rawOffset = view.getUint32(at + 20, true);
    const virtSize = view.getUint32(at + 8, true);
    const kind = name.replace(/^\./, '').toLowerCase();
    sections.push({ name, kind, rva, rawSize, rawOffset, virtSize,
                    drm: DRM.has(kind) });
  }
  return { imageBase, entry, headerSize, sections };
}

async function main() {
  const wasm = await fs.promises.readFile(wasmPath);
  const exe = await fs.promises.readFile(exePath);
  const pe = peSections(exe);
  if (pe.imageBase !== 0x00400000) throw new Error(`image base ${pe.imageBase}`);

  let memory;
  let instance;
  const text = new TextDecoder();
  const imports = {
    nfsu2: {
      register() { return 0; },
      now_ms() { return performance.now(); },
      blocked_ms() { return 0; }
    },
    wasi_snapshot_preview1: {
      args_sizes_get(argc, size) {
        new DataView(memory.buffer).setUint32(argc, 0, true);
        new DataView(memory.buffer).setUint32(size, 0, true);
        return 0;
      },
      args_get() { return 0; },
      fd_fdstat_get() { return 0; },
      fd_fdstat_set_flags() { return 0; },
      fd_prestat_get() { return 0; },
      fd_prestat_dir_name() { return 0; },
      path_open() { return 0; },
      clock_time_get(_id, _precision, out) {
        new DataView(memory.buffer).setBigUint64(out,
          BigInt(Math.round(performance.now() * 1e6)), true);
        return 0;
      },
      fd_close() { return 0; },
      fd_seek() { return 0; },
      fd_write(fd, iovs, count, written) {
        const view = new DataView(memory.buffer);
        let total = 0;
        for (let i = 0; i < count; i++) {
          const at = view.getUint32(iovs + i * 8, true);
          const len = view.getUint32(iovs + i * 8 + 4, true);
          const data = new Uint8Array(memory.buffer, at, len);
          (fd === 2 ? process.stderr : process.stdout).write(data);
          total += len;
        }
        view.setUint32(written, total, true);
        return 0;
      },
      fd_read() { return 0; },
      fd_pridevent() { return 0; },
      proc_exit(code) { throw new Error(`proc_exit(${code})`); },
      sched_yield() { return 0; }
    }
  };

  ({ instance } = await WebAssembly.instantiate(wasm, imports));
  memory = instance.exports.memory;
  if (!memory) throw new Error('module does not export memory');

  // Map the PE image exactly as the page's loader does.
  const target = new Uint8Array(memory.buffer);
  target.set(exe.subarray(0, pe.headerSize), pe.imageBase);
  let mapped = 0;
  for (const s of pe.sections) {
    if (s.drm) {
      console.log(`[boot] skipping DRM section ${s.name}`);
      continue;
    }
    if (s.rawSize && s.rawOffset + s.rawSize <= exe.length) {
      target.set(exe.subarray(s.rawOffset, s.rawOffset + s.rawSize),
                 pe.imageBase + s.rva);
      mapped++;
    }
  }
  console.log(`[boot] image 0x${pe.imageBase.toString(16)} ` +
              `entry 0x${(pe.imageBase + pe.entry).toString(16)} ` +
              `${mapped}/${pe.sections.length} sections mapped, ` +
              `memory ${(memory.buffer.byteLength / 1048576).toFixed(0)} MiB`);

  const functions = instance.exports.prostreet_diagnostic();
  console.log(`[boot] dispatch table: ${functions} lifted functions`);

  let status;
  if (process.env.NFSPS_TRACE) instance.exports.prostreet_set_trace(1);
  try {
    status = instance.exports.prostreet_boot();
  } catch (error) {
    // An unknown import traps on purpose; the diagnostic exports still read.
    status = instance.exports.prostreet_status();
    console.log(`[boot] trap: ${error.stack ?? error.message}`);
  }
  const last = instance.exports.prostreet_last_import();
  const unknown = instance.exports.prostreet_unknown_imports();
  const heap = instance.exports.prostreet_heap_used();
  console.log(`[boot] status=${status} unknown_imports=${unknown} ` +
              `heap=${heap} bytes`);
  if (last) {
    const bytes = new Uint8Array(memory.buffer, last, 192);
    let end = bytes.indexOf(0);
    if (end < 0) end = bytes.length;
    console.log(`[boot] stopped at: ${text.decode(bytes.subarray(0, end))}`);
  }
  process.exitCode = status === 0 ? 0 : 1;
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 2;
});
