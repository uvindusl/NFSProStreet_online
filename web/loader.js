const dataStatus = document.querySelector('#data-status');
const moduleStatus = document.querySelector('#module-status');
const fileCount = document.querySelector('#file-count');
const launch = document.querySelector('#launch');
const copyCommand = document.querySelector('#copy-command');

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
