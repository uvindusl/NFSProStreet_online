const output = document.querySelector('#status');

async function load() {
  try {
    const response = await fetch('prostreet.wasm');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes);
    const count = instance.exports.prostreet_diagnostic();
    output.textContent = `Diagnostic module loaded: ${count} lifted functions`;
  } catch (error) {
    output.textContent = `Module load failed: ${error}`;
  }
}

load();
