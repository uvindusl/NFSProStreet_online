const dataStatus = document.querySelector('#data-status');
const moduleStatus = document.querySelector('#module-status');
const fileCount = document.querySelector('#file-count');
const launch = document.querySelector('#launch');
const copyCommand = document.querySelector('#copy-command');
const menuItems = [...document.querySelectorAll('.menu-item')];
const menuHint = document.querySelector('#menu-hint');
const titleScreen = document.querySelector('.title-screen');

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
