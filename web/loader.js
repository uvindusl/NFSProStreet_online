const output = document.querySelector('#status');

async function load() {
  try {
    const game = await fetch('game.json');
    if (!game.ok) throw new Error(`Game data HTTP ${game.status}`);
    const manifest = await game.json();
    const files = Array.isArray(manifest.files) ? manifest.files.length : 0;
    output.textContent = `Game data ready: ${files} files. Loading WebAssembly…`;

    const response = await fetch('prostreet.wasm');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    const count = instance.exports.prostreet_diagnostic();
    output.textContent = `Runtime loaded: ${count} lifted functions; game data is available`;
  } catch (error) {
    output.textContent = `Module load failed: ${error}`;
  }
}

load();
