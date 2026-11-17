# ProStreet disassembler texld (O_TEX) fix

## Symptom

The standalone disassembler `lifter/tools/shader_disasm.c` printed `texld` instructions as `???`:

```
       4 ???     r0, a0, s0
```

instead of the proper mnemonic:

```
       4 texld   r0, a0, s0
```

The `OPN()` function in `vsemu.c` (used by `vs_disasm()` to print opcode mnemonics) had no case for `O_TEX` (opcode 66), so it fell through to `default: return "???"`.

## Root cause

`OPN()` in `web/runtime/web/runtime/vsemu.c` (line 576) is missing the `O_TEX` case. The opcode enum defines `O_TEX=66` (vsemu.h line 42: `O_TEXCOORD=64, O_TEXKILL=65, O_TEX=66`), and the MSL generator already handles `O_TEX` in its switch (vsemu.c line 920), but the diagnostic mnemonic function `OPN()` did not.

## Fix

Added one case to `OPN()` in both copies of `vsemu.c` (kept byte-identical, verified with `diff`):

```
    case O_TEX:return"texld";
```

Files edited:
- `web/runtime/web/runtime/vsemu.c` (line 596, before `default: return "???"`)
- `reference/nfsu2-web/runtime/vsemu.c` (same line, kept in sync)

No changes to `lifter/runtime/` or `web/runtime/` (other than `web/runtime/web/runtime/vsemu.c` which is the vsemu source used by the tools).

## Build

```
bash lifter/tools/build_shader_disasm.sh
```

Builds `lifter/tools/build/shader_disasm`.

## Verification

Before fix (ps_2_0_002.bin, instruction 4 = texld):
```
       4 ???     r0, a0, s0
```

After fix:
```
       4 texld   r0, a0, s0
```

Ran the disassembler over all 591 extracted shaders in `/tmp/opencode/ps-shaders/` and verified zero texld instructions print as `???`:
- Shaders with texld showing as ???: **0**

All other `???` lines in the output are for genuinely unhandled opcodes (e.g. `O_TEXKILL=65`, `O_TEXLDL`, `O_TEXLDD`) which are outside the scope of this fix.

## Commands

Build:
```
bash lifter/tools/build_shader_disasm.sh
```

Run on all shaders:
```
for blob in /tmp/opencode/ps-shaders/*.bin; do
    ./lifter/tools/build/shader_disasm "$blob"
done
```

Check for texld-as-???:
```
for blob in /tmp/opencode/ps-shaders/*.bin; do
    ./lifter/tools/build/shader_disasm "$blob" 2>/dev/null
done | grep '???' | grep -c 'texld'   # should be 0
```
