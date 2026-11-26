# Shader differential on Apple Metal (M2 Pro)

The same harness as [`shader-residual-classes.md`](shader-residual-classes.md), run on a second platform:
`lifter/tools/shader_vec_diff.sh` over the 591 extracted shader blobs, WGSL emitter vs the CPU
oracle, on Chrome with WebGPU on Apple Metal. It is a data point on how much of the earlier
residual depends on the GPU and driver. It changes no code.

## Setup

- Machine: MacBook Pro, Apple M2 Pro, macOS; Google Chrome 158; adapter `apple metal-3`.
- Commit: `e3d54de` (checkout of `main`).
- Blobs: `lifter/tools/extract_shaders.py` on a local copy of the game (591 blobs; vs_1_1 27,
  vs_2_0 199, vs_3_0 71, ps_2_0 223, ps_3_0 71). Not in the repository.
- Command: `sh lifter/tools/shader_vec_diff.sh <blobs> <vec-dir>`. It exits non-zero by design.
- Line references below are to `e3d54de`; `web/src/test/shadercheck.ts` has changed since (#10).

## Result

```
modules 593, compileErrors 0, pipelines 1183, pipelineErrors 0, pull 3328/3328 exact (maxUlp 0)
vertex differential: 297 modules, 19008 vertices, 346 of 855360 components over 1e-4 relative
pixel differential:  287 modules, 73472 pixels, 4 of 315512 components over 1e-3 relative,
                     max 2.11e-3 (ps_258 pixel 224.r: gpu -29224.26 ref -29286.04), 0 alpha-test flips
```

`ok` is `false`. In `web/src/test/shadercheck.ts:416` it is true only when `failures`,
`compileErrors`, `pipelineErrors`, `pull.bad`, `diff.bad` and `psdiff.bad` are all zero. Three of
those are non-zero in this run, each on its own enough to fail it:

1. **`failures` is non-empty: seven derivative shaders.** `ps_249 250 251 252 289 290 291` use
   `dsx`/`dsy`. The harness builds a compute twin of each pixel shader, and WGSL rejects
   `dpdx`/`dpdy` in a compute stage (`built-in cannot be used by compute pipeline stage`). That
   compile error is pushed to `failures` (`shadercheck.ts:256-258`), so these shaders fail the run;
   they are not just unmeasured. As [`shader-residual-classes.md`](shader-residual-classes.md) §2a
   says, the CPU oracle has no `O_DSX`/`O_DSY` case either.
2. **`diff.bad` is non-zero: the vertex differential has 346 components over 1e-4**
   (36 FAIL lines are printed).
3. **`psdiff.bad` is non-zero: 4 pixel components are over 1e-3 relative.**

`compileErrors`, `pipelineErrors` and `pull.bad` are zero (see the first line of the result).

The lines above are the summary lines of the run. The full output (about 277 KB) also holds
emitted WGSL from game shaders, so it is not committed; `AGENTS.md` keeps extracted shaders out of
the repository. To reproduce, run the command above on your own blob dump. The final
`SHADERCHECK {...}` JSON line has `"ok":false`, `"diff":{"bad":346,...}` and `"psdiff":{"bad":4,...}`.

## After #10

PR #10 added a render-pass check for the seven `dsx`/`dsy` shaders. A run on a later `main` that
includes it reports for them `7 modules, 1792 pixels, 0 mismatches` (max 4.62e-4), so cause 1 above
no longer applies there. The vertex (346) and pixel (4) counts did not change in that run, so
causes 2 and 3 still do.

## Reading it

- **Pixels.** At the 1e-3 tolerance in `web/src/test/shadercheck.ts`, 4 of 315,512 components
  differ here. [`shader-residual-classes.md`](shader-residual-classes.md) reports 3,869 bad
  components on the platform it was measured on, mostly in 1e-3..1e-2. If that run used this
  harness version, most of that residual depends on the GPU or driver, and the emitter is not the
  cause for those components. The remaining four are in shaders whose output is around 3e4 on the
  synthetic inputs (`ps_258` and similar). I have not root-caused them.
- **Vertices.** The picture is different. 346 components still differ, and not all of them look
  like rounding. Lines from the run, with `vs_NNN vertex V out O` as the harness prints them:
  - `FAIL vs_290 vertex 5 out 18: gpu Infinity cpu 1` (vertices 6 and 7 the same).
  - `FAIL vs_292 vertex 0 out 19: gpu 0.75 cpu 1.8310546875`.
  - `FAIL vs_293 vertex 0 out 19: gpu 0.57421875 cpu 0.01171875`.
  - `FAIL vs_294 vertex 1 out 19: gpu 2.296875 cpu 0.1435546875`.

  The `vs_292`/`vs_293`/`vs_294` values differ by whole tenths or more, which is not rounding. I
  have not traced where the oracle's `1` for `vs_290` comes from, or what these shaders compute, so
  I make no claim about the cause. In particular this run does not show that `pow` is involved: the
  oracle returns 0 for a zero base (`web/runtime/web/runtime/vsemu.c:483`). The `0^negative` case
  that [`shader-residual-classes.md`](shader-residual-classes.md) calls ambiguous may or may not
  be related. These are worth a closer look.

## Not covered

- Texture sampling uses the harness's synthetic textures, so this says nothing about real assets.
- Compared with the CPU oracle, not with D3D9 output. Neither path has a D3D9 reference capture yet.
