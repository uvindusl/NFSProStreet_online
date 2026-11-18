WebGPU HLE Finalization — ProStreet (2026-11-17)

(a) DrawPrimitive/DrawIndexedPrimitive stubs already exist in d3d9_hle.c:1350-1374, mapped in g_device_vtbl:1671-1674 as logging stubs returning D3D_OK — no action needed.

(b) INDIRECT3DEVICE9Tbl does not exist; COM vtable is g_device_vtbl in d3d9_hle.c (all 107 slots populated). Premise referenced NFSU2 structure.

(c) FULL=1 web/build.sh succeeded — wasm rebuilt, no new shader errors.

Remaining gaps & effort:
1. MSL backend (vsemu.c) lacks R_DEPTHOUT in msl_reg/msl_dst/epilogue — blocks oDepth on native (~1 day).
2. D3DX9 HLE — 14 imports unimplemented; D3DXCreateEffectFromResourceA critical (~3-5 days).
3. DrawPrimitive/DrawIndexedPrimitive are stubs — real WebGPU draw submission needs VB/IB binding, pipeline state, render passes (~1-2 weeks).
4. Web oDepth complete: WGSL backend handles R_DEPTHOUT; encoder.ts detects PSOut.

Ship M1 with (a)/(b)/(c) satisfied; schedule MSL oDepth fix and D3DX9 HLE post-M1.