# The vs_3_0 output-register / TEXCOORD-usage gap (out of scope)

Status: **measured, recorded, deliberately not fixed.** This is the vs_3_0 half of
the story told in `debug/abi-8wide-ot8.md`; that file is authoritative for the oT8
resolution and the 8-slot sufficiency argument.

## What the port does

Every `R_OUTPUT` register is named `oT<n>` and mapped by **register number**:
`R_OUTPUT n -> VSOut.tex[n]` (`web/runtime/web/runtime/vsemu.c`), i.e. `oT3 -> uv3`.
The WGSL emitter (`web/runtime/web/vsemu_wgsl.inc`, `w_vsdst`) does the same and
masks `n & 7`; the CPU oracle drops `n >= 8`. Both sides use the *same* rule, so the
differential stays consistent.

## What D3D9 actually declares

In vs_3_0 the output **register number is not the TEXCOORD usage index**. The `dcl`
instruction carries a declaration token (usage + usage index) and a destination token
(the output register):

```
dcl  usage_token  dst_token
```

Parsing the 71 vs_3_0 blobs in `/tmp/opencode/ps-shaders` with

```
usage       = ut & 0x1F
usage_index = (ut >> 16) & 0xF
RN          = dst & 0x7FF
RT          = ((dst & 0x1800) >> 8) | ((dst & 0x70000000) >> 28)   # 6 = R_TEXCRDOUT (output)
```

and counting only **output** TEXCOORD dcls (`usage == 5 && RT == 6`):

- **331** TEXCOORD output dcls total,
- **262** of them have `RN != usage_index + 3` — the register number does not follow
  the usage index.

Worked example, `vs_3_0_003.bin`:

| output reg | declared usage | port maps to | usage-correct target |
|-----------:|----------------|--------------|----------------------|
| o3 | TEXCOORD0 | uv3 | uv0 |
| o4 | TEXCOORD1 | uv4 | uv1 |
| o5 | TEXCOORD2 | uv5 | uv2 |
| o6 | TEXCOORD4 | uv6 | uv4 |
| o7 | TEXCOORD5 | uv7 | uv5 |
| o8 | TEXCOORD6 | (dropped, n>=8) | uv6 |

A usage-correct port would map `TEXCOORDk -> uvk` (and POSITION/COLOR to their own
outputs), which neither the CPU oracle nor either emitter does today.

## Why it is out of scope

`com.c fill_caps()` reports VS `0xFFFE0200` (vs_2_0) and PS `0xFFFF0200` (ps_2_0) by
default — only `D3D_SM` overrides. The game therefore never selects vs_3_0/ps_3_0
under this HLE, and vs_1_1 / vs_2_0 declare no output registers at all (their outputs
go to oPos/oD0/oD1/oT0..oT7, where the register number *is* the slot). Fixing this
would only matter if the port ever advertises SM3 caps.

## Bounds (measured)

- Max TEXCOORD usage index anywhere in the corpus (all vs_* profiles): **8**.
- Max TEXCOORD output register in vs_3_0: **8**.

So 8 uv slots remain sufficient even if the mapping is ever corrected; no shader in
the corpus needs uv8.

## Reproduce

Blobs are game-derived and live outside the repo. Regenerate them from the retail
image with `python3 lifter/tools/extract_shaders.py <nfs.exe> /tmp/opencode/ps-shaders`
(591 blobs), then walk each `dcl` (opcode 31) as above. Cross-check against
`lifter/tools/shader_disasm` for the input dcls (`dcl v# <- TEXCOORD#`).
