const dataStatus = document.querySelector('#data-status');
const moduleStatus = document.querySelector('#module-status');
const fileCount = document.querySelector('#file-count');
const launch = document.querySelector('#launch');
const copyCommand = document.querySelector('#copy-command');
const menuItems = [...document.querySelectorAll('.menu-item')];
const menuHint = document.querySelector('#menu-hint');
const titleScreen = document.querySelector('.title-screen');
const inputStatus = document.querySelector('#input-status');
const gameCanvas = document.querySelector('#game-canvas');
const rendererStatus = document.querySelector('#module-status');
import { createBrowserRenderer } from './gpu.js';

const menuDescriptions = {
  career: 'Build your driver profile and start a career.',
  race: 'Race-day mode will be connected after runtime bring-up.',
  quick: 'Jump straight into a quick local match.',
  leaderboards: 'Online leaderboards are planned for a later stage.',
  options: 'Configure display, controls and audio.',
  setup: 'Check local game data and browser runtime status.',
  about: 'Open the project build status and credits.'
};

function selectMenu(index) {
  const next = Math.max(0, Math.min(menuItems.length - 1, index));
  menuItems.forEach((item, itemIndex) => {
    item.classList.toggle('active', itemIndex === next);
  });
  menuItems[next].focus({ preventScroll: true });
  menuHint.textContent = menuDescriptions[menuItems[next].dataset.panel] || '';
}

menuItems.forEach((item, index) => {
  item.addEventListener('click', () => selectMenu(index));
  item.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowDown' || event.key === 'ArrowRight') {
      event.preventDefault();
      selectMenu((index + 1) % menuItems.length);
    } else if (event.key === 'ArrowUp' || event.key === 'ArrowLeft') {
      event.preventDefault();
      selectMenu((index - 1 + menuItems.length) % menuItems.length);
    }
  });
});

const wallArt = new Image();
wallArt.onload = () => titleScreen.classList.add('has-wall-art');
wallArt.src = 'wall-art';

const KEY_BITS = {
  ArrowUp: 1, ArrowDown: 2, ArrowLeft: 4, ArrowRight: 8,
  Enter: 16, Escape: 32, Shift: 64, ' ': 128
};
let inputMemory;
let inputOffset = 0;
let inputButtons = 0;
let inputSequence = 0;
let wasmInstance;
const HOST_KIND_INPUT = 3;

const hostImports = {
  register(kind, ptr, size) {
    const memory = wasmInstance?.exports.memory;
    if (!memory || kind !== HOST_KIND_INPUT || size < 20 ||
        ptr + size > memory.buffer.byteLength) return -1;
    inputMemory = wasmInstance.exports.memory;
    inputOffset = ptr;
    inputStatus.textContent = 'KEYBOARD / GAMEPAD READY';
    return 0;
  },
  now_ms() {
    return performance.now();
  },
  blocked_ms() {
    return 0;
  }
};

// The module is linked against wasi-libc for the host side of the runtime
// (its printf/clock/fopen go through these).  The page supplies the same
// stubs web/boot.mjs uses, so the headless harness and the browser exercise
// one code path.
let wasmMemory = null;

function memoryData() {
  return new Uint8Array(wasmMemory.buffer);
}

function writeConsole(bytes, fd) {
  const text = new TextDecoder().decode(bytes);
  for (const line of text.split('\n')) {
    if (line) console.log(fd === 2 ? `[game] ${line}` : line);
  }
}

const wasiImports = {
  args_sizes_get(argc, size) {
    const view = new DataView(wasmMemory.buffer);
    view.setUint32(argc, 0, true);
    view.setUint32(size, 0, true);
    return 0;
  },
  args_get() { return 0; },
  clock_time_get(_id, _precision, out) {
    const view = new DataView(wasmMemory.buffer);
    view.setBigUint64(out, BigInt(Math.round(performance.now() * 1e6)), true);
    return 0;
  },
  fd_close() { return 0; },
  fd_seek() { return 0; },
  fd_write(fd, iovs, count, written) {
    const view = new DataView(wasmMemory.buffer);
    let total = 0;
    for (let index = 0; index < count; index++) {
      const at = view.getUint32(iovs + index * 8, true);
      const length = view.getUint32(iovs + index * 8 + 4, true);
      writeConsole(memoryData().subarray(at, at + length), fd);
      total += length;
    }
    view.setUint32(written, total, true);
    return 0;
  },
  proc_exit(code) {
    throw new Error(`guest exited (${code})`);
  },
  sched_yield() { return 0; }
};

function setKey(event, pressed) {
  const bit = KEY_BITS[event.key];
  if (!bit) return;
  event.preventDefault();
  inputButtons = pressed ? inputButtons | bit : inputButtons & ~bit;
  writeInput();
}

function writeInput() {
  if (!inputMemory) return;
  const view = new DataView(inputMemory.buffer);
  view.setUint32(inputOffset, inputButtons, true);
  view.setUint32(inputOffset + 16, ++inputSequence, true);
}

window.addEventListener('keydown', (event) => setKey(event, true), { passive: false });
window.addEventListener('keyup', (event) => setKey(event, false), { passive: false });
window.addEventListener('blur', () => {
  inputButtons = 0;
  writeInput();
});

function pollGamepad() {
  if (!inputMemory) return;
  const pad = navigator.getGamepads?.().find(Boolean);
  const view = new DataView(inputMemory.buffer);
  if (!pad) {
    view.setUint16(inputOffset + 14, 0, true);
    requestAnimationFrame(pollGamepad);
    return;
  }
  const axis = (value) => Math.max(-1, Math.min(1, value || 0));
  view.setInt16(inputOffset + 8, axis(pad.axes[0]) * 32767, true);
  view.setInt16(inputOffset + 10, Math.max(0, -axis(pad.axes[1])) * 32767, true);
  view.setInt16(inputOffset + 12, Math.max(0, axis(pad.axes[1])) * 32767, true);
  view.setUint16(inputOffset + 14, 1, true);
  view.setUint32(inputOffset + 4, pad.buttons.reduce(
    (bits, button, index) => button.pressed ? bits | (1 << index) : bits, 0), true);
  view.setUint32(inputOffset + 16, ++inputSequence, true);
  requestAnimationFrame(pollGamepad);
}

// DRM sections are never mapped or executed (AGENTS.md hard rule 3).  The
// .text image is already decrypted; the protection sections are not part of
// the game.
const DRM_SECTIONS = new Set(['ars', 'est', 'artem', 'celare', 'securom']);

function loadPeImage(bytes, memory) {
  const file = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (file.getUint16(0, true) !== 0x5a4d) throw new Error('nfsps.exe is not PE32');
  const pe = file.getUint32(0x3c, true);
  if (file.getUint32(pe, true) !== 0x00004550) throw new Error('invalid PE signature');
  const sections = file.getUint16(pe + 6, true);
  const optionalSize = file.getUint16(pe + 20, true);
  const optional = pe + 24;
  if (file.getUint16(optional, true) !== 0x10b) throw new Error('nfsps.exe is not 32-bit PE');
  const imageBase = file.getUint32(optional + 28, true);
  if (imageBase !== 0x00400000) throw new Error('unexpected PE image base');
  const headerSize = file.getUint32(optional + 60, true);
  const sectionTable = optional + optionalSize;
  const target = new Uint8Array(memory.buffer);
  target.set(bytes.subarray(0, headerSize), imageBase);
  for (let index = 0; index < sections; ++index) {
    const section = sectionTable + index * 40;
    let name = '';
    for (let c = 0; c < 8 && file.getUint8(section + c); c++)
      name += String.fromCharCode(file.getUint8(section + c));
    if (DRM_SECTIONS.has(name.replace(/^\./, '').toLowerCase())) {
      console.log(`[boot] skipping DRM section ${name}`);
      continue;
    }
    const virtualAddress = file.getUint32(section + 12, true);
    const rawSize = file.getUint32(section + 16, true);
    const rawOffset = file.getUint32(section + 20, true);
    if (rawSize && rawOffset + rawSize <= bytes.length)
      target.set(bytes.subarray(rawOffset, rawOffset + rawSize),
        imageBase + virtualAddress);
  }
}

async function load() {
  try {
    const renderer = await createBrowserRenderer(gameCanvas);
    if (!renderer.available) {
      rendererStatus.textContent = 'WEBGPU UNAVAILABLE';
    } else {
      renderer.resize();
      window.addEventListener('resize', renderer.resize);
      rendererStatus.textContent = 'WEBGPU READY';
    }
    const game = await fetch('game.json');
    if (!game.ok) throw new Error(`Game data HTTP ${game.status}`);
    const manifest = await game.json();
    const files = Array.isArray(manifest.files) ? manifest.files.length : 0;
    dataStatus.textContent = 'Local data ready';
    fileCount.textContent = `${files} FILES`;

    const response = await fetch('prostreet.wasm');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const bytes = await response.arrayBuffer();
    const result = await WebAssembly.instantiate(bytes, {
      nfsu2: hostImports,
      wasi_snapshot_preview1: wasiImports
    });
    wasmInstance = result.instance;
    wasmMemory = wasmInstance.exports.memory;
    if (!wasmMemory) throw new Error('module does not export memory');
    if (wasmInstance.exports.prostreet_input_init) {
      if (wasmInstance.exports.prostreet_input_init() !== 0) {
        throw new Error('WASM input mailbox registration failed');
      }
      pollGamepad();
    } else {
      inputStatus.textContent = 'INPUT BRIDGE UNAVAILABLE';
    }
    const count = wasmInstance.exports.prostreet_diagnostic();
    moduleStatus.textContent = `${count.toLocaleString()} FUNCTIONS LOADED`;
    const exe = await fetch('data/nfsps.exe');
    if (!exe.ok) throw new Error(`Executable HTTP ${exe.status}`);
    const image = new Uint8Array(await exe.arrayBuffer());
    loadPeImage(image, wasmInstance.exports.memory);
    if (wasmInstance.exports.prostreet_boot) {
      // An unsupported import traps on purpose; the diagnostic exports still
      // read, so the page can name the exact shim that stopped the boot.
      let bootStatus = 6;
      try {
        bootStatus = wasmInstance.exports.prostreet_boot();
      } catch (error) {
        if (!(error instanceof WebAssembly.RuntimeError)) throw error;
        bootStatus = wasmInstance.exports.prostreet_status();
      }
      let last = '';
      if (wasmInstance.exports.prostreet_last_import) {
        const address = wasmInstance.exports.prostreet_last_import();
        if (address) {
          const bytesAt = memoryData().subarray(address, address + 192);
          const end = bytesAt.indexOf(0);
          last = new TextDecoder().decode(
            bytesAt.subarray(0, end < 0 ? bytesAt.length : end));
        }
      }
      moduleStatus.textContent = last && last !== 'none'
        ? `STOPPED AT ${last}`
        : `BOOT STATUS ${bootStatus}`;
      console.log(`[boot] status=${bootStatus} stopped at: ${last || 'none'}`);
    }
    launch.disabled = false;
    launch.innerHTML = '<span class="button-light"></span>Runtime diagnostic ready';
    launch.addEventListener('click', () => {
      launch.innerHTML = '<span class="button-light"></span>Gameplay coming in S2';
    }, { once: true });
  } catch (error) {
    dataStatus.textContent = 'Start the local host';
    moduleStatus.textContent = String(error?.message || error);
    fileCount.textContent = 'OFFLINE';
    launch.innerHTML = '<span class="button-light"></span>Local host required';
    console.error(error);
  }
}

copyCommand?.addEventListener('click', async () => {
  if (!navigator.clipboard) return;
  await navigator.clipboard.writeText('web/start.sh --data "/path/to/ProStreet/app"');
  copyCommand.textContent = 'COPIED';
  setTimeout(() => { copyCommand.textContent = 'COPY'; }, 1400);
});

load();
