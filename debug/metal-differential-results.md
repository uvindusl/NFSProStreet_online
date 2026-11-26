# Shader differential on Apple Metal (M2 Pro)

The same harness as `debug/shader-residual-classes.md`, run on a second platform:
`lifter/tools/shader_vec_diff.sh` over the 591 extracted shader blobs, WGSL emitter vs the CPU
oracle, on Chrome with WebGPU on Apple Metal. It is a data point on how much of the earlier
residual depends on the GPU and driver. It changes no code.

## Setup

- Machine: MacBook Pro, Apple M2 Pro, macOS; Google Chrome 158; adapter `apple metal-3`.
- Commit: `e3d54de` (checkout of `main`).
- Blobs: `lifter/tools/extract_shaders.py` on a local copy of the game (591 blobs; vs_1_1 27,
  vs_2_0 199, vs_3_0 71, ps_2_0 223, ps_3_0 71). Not in the repository.
- Command: `sh lifter/tools/shader_vec_diff.sh <blobs> <vec-dir>`.

## Result

```
modules 593, compileErrors 0, pipelines 1183, pipelineErrors 0, pull 3328/3328 exact (maxUlp 0)
vertex differential: 297 modules, 19008 vertices, 346 of 855360 components over 1e-4 relative
pixel differential:  287 modules, 73472 pixels, 4 of 315512 components over 1e-3 relative,
                     max 2.11e-3 (ps_258 pixel 224.r: gpu -29224.26 ref -29286.04), 0 alpha-test flips
```

`ok` is still `false`, for two reasons:

1. **Seven derivative shaders are not measured.** `ps_249 250 251 252 289 290 291` use `dsx`/`dsy`.
   The harness builds a compute twin of each pixel shader, and WGSL rejects `dpdx`/`dpdy` in a
   compute stage (`built-in cannot be used by compute pipeline stage`). As
   `shader-residual-classes.md` §2a says, the CPU oracle has no `O_DSX`/`O_DSY` case either.
2. **The vertex differential has 346 components over 1e-4** (36 FAIL lines are printed).

## Reading it

- **Pixels.** At the 1e-3 tolerance in `web/src/test/shadercheck.ts`, 4 of 315,512 components
  differ here. `shader-residual-classes.md` reports 3,869 bad components on the platform it was
  measured on, mostly in 1e-3..1e-2. If that run used this harness version, most of that residual
  depends on the GPU or driver, and the emitter is not the cause for those components. The remaining four are in shaders whose output
  is around 3e4 on the synthetic inputs (`ps_258` and similar). I have not root-caused them.
- **Vertices.** The picture is different. 346 components still differ, and not all of them look like
  rounding: `vs_292`/`vs_293`/`vs_294` (`exp`/`log`/`frc`/`pow`/`rcp`) differ by whole tenths
  (e.g. gpu 0.574 vs cpu 0.012), and `vs_290` gives `Infinity` where the oracle gives 1. The
  `0^negative` case is the one `shader-residual-classes.md` calls ambiguous. The others are not
  explained by it, and are worth a closer look.

## Not covered

- Texture sampling uses the harness's synthetic textures, so this says nothing about real assets.
- Compared with the CPU oracle, not with D3D9 output. Neither path has a D3D9 reference capture yet.
