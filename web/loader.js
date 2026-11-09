const dataStatus = document.querySelector('#data-status');
const moduleStatus = document.querySelector('#module-status');
const fileCount = document.querySelector('#file-count');
const launch = document.querySelector('#launch');
const copyCommand = document.querySelector('#copy-command');
const menuItems = [...document.querySelectorAll('.menu-item')];
const menuHint = document.querySelector('#menu-hint');
const titleScreen = document.querySelector('.title-screen');
const inputStatus = document.querySelector('#input-status');

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

async function load() {
  try {
    const game = await fetch('game.json');
    if (!game.ok) throw new Error(`Game data HTTP ${game.status}`);
    const manifest = await game.json();
    const files = Array.isArray(manifest.files) ? manifest.files.length : 0;
    dataStatus.textContent = 'Local data ready';
    fileCount.textContent = `${files} FILES`;

    const response = await fetch('prostreet.wasm');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    if (instance.exports.memory && instance.exports.prostreet_input_ptr) {
      inputMemory = instance.exports.memory;
      inputOffset = instance.exports.prostreet_input_ptr();
      inputStatus.textContent = 'KEYBOARD / GAMEPAD READY';
      pollGamepad();
    } else {
      inputStatus.textContent = 'INPUT BRIDGE UNAVAILABLE';
    }
    const count = instance.exports.prostreet_diagnostic();
    moduleStatus.textContent = `${count.toLocaleString()} FUNCTIONS LOADED`;
    launch.disabled = false;
    launch.innerHTML = '<span class="button-light"></span>Runtime diagnostic ready';
    launch.addEventListener('click', () => {
      launch.innerHTML = '<span class="button-light"></span>Gameplay coming in S2';
    }, { once: true });
  } catch (error) {
    dataStatus.textContent = 'Start the local host';
    moduleStatus.textContent = 'Waiting for runtime';
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
