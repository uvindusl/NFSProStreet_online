# ProStreet disassembler opcode mnemonic fix

## Symptom

`lifter/tools/build/shader_disasm` printed `???` for 306 instruction lines across the 591-blob corpus in `/tmp/opencode/ps-shaders/`. Example before fix:

```
       4 ???     r0, a0, s0
       6 ???     r0
       98 ???     r0.w, r0.wwww, c16.yyyy, c16.zzzz
```

## Root cause

`OPN()` in `web/runtime/web/runtime/vsemu.c` (the mnemonic function used by `vs_disasm()`) was missing cases for opcodes that appear in the ProStreet shader corpus. Additionally, the opcode enum had wrong values for `O_TEXLDD` (95) and `O_TEXLDL` (97) and was missing `O_SETP` (94) and `O_BREAKP` (96).

Per mingw `d3d9types.h`:
- `D3DSIO_DSX = 91`, `D3DSIO_DSY = 92`
- `D3DSIO_TEXLDD = 93`, `D3DSIO_SETP = 94`
- `D3DSIO_TEXLDL = 95`, `D3DSIO_BREAKP = 96`

| Opcode | Hex | Count in corpus | D3D9 name | Mnemonic added |
|--------|-----|-----------------|-----------|----------------|
| 65 | 0x0041 | 38 | O_TEXKILL | texkill |
| 88 | 0x0058 | 187 | O_CMP | cmp |
| 90 | 0x005a | 63 | O_DP2ADD | dp2add |
| 91 | 0x005b | 11 | D3DSIO_DSX | dsx |
| 92 | 0x005c | 7 | D3DSIO_DSY | dsy |
| 93 | 0x005d | — | D3DSIO_TEXLDD | texldd |
| 94 | 0x005e | — | D3DSIO_SETP | setp |
| 95 | 0x005f | — | D3DSIO_TEXLDL | texldl |
| 96 | 0x0060 | — | D3DSIO_BREAKP | breakp |

Opcodes 91/92 (dsx/dsy) read from `v` (vertex input) registers and write to `r` (temp) registers — they are partial derivatives of interpolated inputs, not texcoord. They appear in ps_3_0 shaders (e.g. `ps_3_0_026.bin`).

## Fix

Two changes, applied to both copies of `vsemu.c` (kept byte-identical, verified with `diff`):

**1. Opcode enum** (`vsemu.c` line 44):

```c
O_CMP=88, O_DP2ADD=90, O_TEXLDD=93, O_SETP=94, O_TEXLDL=95, O_BREAKP=96,
```

(was `O_TEXLDD=95, O_TEXLDL=97`)

**2. `OPN()` mnemonic function** (`vsemu.c` line ~596):

```c
case O_TEXKILL:return"texkill";
case O_CMP:return"cmp";
case O_DP2ADD:return"dp2add";
case 91:return"dsx";
case 92:return"dsy";
case O_TEXLDD:return"texldd";
case O_SETP:return"setp";
case O_TEXLDL:return"texldl";
case O_BREAKP:return"breakp";
```

Files edited:
- `web/runtime/web/runtime/vsemu.c` — the copy used by `build_shader_disasm.sh`
- `reference/nfsu2-web/runtime/vsemu.c` — mirror, kept in sync

No changes to `lifter/runtime/`. Change is confined to the opcode enum and `OPN()`.

## Build

```
bash lifter/tools/build_shader_disasm.sh
```

Builds `lifter/tools/build/shader_disasm`.

## Verification — before/after

**Before fix:** 306 `???` lines across 591 shaders.

**After fix:** 0 `???` lines across 591 shaders.

Sample disassembly (`ps_2_0_002.bin`) before:
```
       4 ???     r0, a0, s0
       5 add     r0, r0.wwww, -c0.xxxx
       6 ???     r0
```

After:
```
       4 texld   r0, a0, s0
       5 add     r0, r0.wwww, -c0.xxxx
       6 texkill r0
```

Sample (`ps_3_0_026.bin`) after:
```
       9 dsy     r92, v2
      11 dsx     r91, v2
```

## Commands

Build:
```
bash lifter/tools/build_shader_disasm.sh
```

Count `???` across all shaders:
```
total=0; for blob in /tmp/opencode/ps-shaders/*.bin; do
    out=$(./lifter/tools/build/shader_disasm "$blob" 2>/dev/null);
    total=$((total + $(echo "$out" | grep -c '???' || true)));
done; echo "Total ??? lines: $total"
```
Expected: `Total ??? lines: 0`
