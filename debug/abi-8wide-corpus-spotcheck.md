# 8-wide ABI spot-check on real corpus shaders

Independent re-verification (2026-11-18) that the widened inter-stage ABI reaches
real shaders, not just the harness. The corpus in `/tmp/opencode/ps-shaders` was
regenerated today from the retail image with
`python3 lifter/tools/extract_shaders.py <nfs.exe> /tmp/opencode/ps-shaders`
(591 blobs: ps_2_0 223, ps_3_0 71, vs_1_1 27, vs_2_0 199, vs_3_0 71).

## Pixel shaders that use samplers 4+

26 ps_* blobs declare a sampler register `s>=4`; the largest are `s4,s5,s6`
(e.g. `ps_3_0_004`). Emitting one through the current emitter:

```
out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_3_0_004.bin -o /tmp/ps304.wgsl
```

emits the widened group-1 declarations and samples stages 4..6:

```
@group(1) @binding(4)  var tex4: texture_2d<f32>;
@group(1) @binding(5)  var tex5: texture_2d<f32>;
@group(1) @binding(6)  var tex6: texture_2d<f32>;
@group(1) @binding(12) var smp4: sampler;
@group(1) @binding(13) var smp5: sampler;
@group(1) @binding(14) var smp6: sampler;
...
r1 = textureSample(tex6, smp6, (r0.xwzw).xy);
r3 = textureSample(tex4, smp4, (v0).xy);
r7 = textureSample(tex5, smp5, (v0).xy);
```

`~/.cargo/bin/naga /tmp/ps304.wgsl` → `Validation successful` (exit 0).

## Pixel shaders that use texture-coordinate registers 4+

8 ps_2_0 blobs declare `a4..a6` (`ps_2_0_027/028` use `a6`). Emitting `ps_2_0_027`:

```
out/tools/shader2wgsl /tmp/opencode/ps-shaders/ps_2_0_027.bin -o /tmp/ps027.wgsl
```

the vertex input now carries the widened uv set and the shader reads uv6:

```
@location(6) uv4: vec4f, @location(7) uv5: vec4f, @location(8) uv6: vec4f, @location(9) uv7: vec4f,
...
t6 = select(vec4f(0.0), i.uv6, PS20_TEXCOORD);
```

`~/.cargo/bin/naga /tmp/ps027.wgsl` → `Validation successful` (exit 0).

## What this shows

Both halves of the widening are reachable from real corpus input: samplers/`tex`
above stage 3 (bindings 4..7 / 12..15) and texture coordinates above slot 3
(`a4..a7` → `uv4..uv7`). The emitted modules validate under naga. The full-corpus
gate (`lifter/tools/corpus_check.sh`) and the GPU differential
(`lifter/tools/shader_vec_diff.sh`) cover the rest.
