w1F:p8 WebGPU Port Summary (ProStreet)

- Translated 591 D3D9 shader blobs → WGSL via offline shader2wgsl (294 PS, 297 VS).
- All core WGSL features validate: frag_depth, texture_2d, textureSample, sampler, override constants, group/binding layouts, switch on overrides, discard, mix/select.
- Native triangle/quad tests use DrawPrimitiveUP (immediate verts); HLE stubs don't render. WebGPU needs real vertex buffers + bind groups — geometry upload gap documented.
- 58 oDepth shaders now emit @builtin(frag_depth) per depth spec fix; 2 remaining (ps_2_0_111/112) need dest register allow-list expansion.
- Chromium 155 in this env lacks WebGPU (no adapter with any flag combo). Test page at analysis/webgpu-test/wgsl-validate.html ready for WebGPU browser.
- Created: prostreet-webgpu-port-report.md + wgsl-validate.html in analysis/.
- No source changes outside analysis/; ProStreet scope only.