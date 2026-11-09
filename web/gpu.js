const FORMAT = 'bgra8unorm';

export async function createBrowserRenderer(canvas) {
  if (!navigator.gpu) {
    return { available: false, reason: 'WebGPU is not available in this browser' };
  }
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) {
    return { available: false, reason: 'No WebGPU adapter was found' };
  }
  const device = await adapter.requestDevice();
  const context = canvas.getContext('webgpu');
  if (!context) {
    return { available: false, reason: 'WebGPU canvas context is unavailable' };
  }
  const format = navigator.gpu.getPreferredCanvasFormat?.() || FORMAT;
  context.configure({ device, format, alphaMode: 'opaque' });
  return {
    available: true,
    adapter,
    device,
    context,
    format,
    resize() {
      const scale = Math.min(window.devicePixelRatio || 1, 2);
      const width = Math.max(1, Math.floor(canvas.clientWidth * scale));
      const height = Math.max(1, Math.floor(canvas.clientHeight * scale));
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
        context.configure({ device, format, alphaMode: 'opaque' });
      }
    }
  };
}
