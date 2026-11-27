# Compiled D3DX effect format (fx_2_0) as used by ProStreet

ProStreet carries 34 compiled effects as `RT_RCDATA` resources named `IDI_*_FX` and loads them
with `D3DXCreateEffectFromResourceA`. The game imports `d3dx9_34.dll`, so the `ID3DXEffect` HLE
has to read this format itself. This note records what `lifter/tools/fx_dump.py` decodes.

Every claim is marked:

- **Confirmed (data)**: holds for all 34 effects, checked by `fx_dump.py --check`.
- **Confirmed (Wine)**: read from the `state_table[]` array in Wine's
  `dlls/d3dx9_36/effect.c`, and consistent with the game data.
- **Inferred**: a reading that fits the data but that nothing checks.

Out of scope here: the preshader VM and the shader bytecode itself. Parameter default values
sit at `value_off` as `rows * cols * max(1, elements)` DWORDs (read by the runtime, not checked
by `fx_dump.py`).

## Reproduce

```
python3 lifter/tools/fx_dump.py game/nfsps.exe --check -v     # needs pefile
python3 lifter/tools/fx_dump_test.py                          # synthetic effect, no game data
```

## File layout

All DWORDs are little-endian. Offsets stored in the file are relative to byte 8.

```
+0   0xFEFF0901
+4   table_offset
+8   data area: typedefs, strings, values (addressed by offset)
+8+table_offset:
     nparams, ntechniques, unknown, nobjects
     nparams     x { typedef_off, value_off, flags, nannotations, annotations... }
     ntechniques x { name_off, nannotations, npasses, annotations...,
                     npasses x { name_off, nannotations, nstates, annotations..., states... } }
     -- object table --
     nsmall, nresources
     nsmall      x { object_id, size, data[size] padded to 4 }
     nresources  x { technique_index, index, element_index, state_index, usage,
                     size, data[size] padded to 4 }
<end of file>
```

Shared records:

```
state        { operation, index, typedef_off, value_off }
annotation   { typedef_off, value_off }
typedef      { type, class, name_off, semantic_off, element_count, [cols, rows] }
string       { length, bytes, NUL-terminated, padded to 4 }
```

`cols, rows` are present for classes 0 to 3 (scalar, vector, matrix). Types
(`D3DXPARAMETER_TYPE`): 1 BOOL, 2 INT, 3 FLOAT, 4 STRING, 5 TEXTURE, 10 SAMPLER, 15 PIXELSHADER,
16 VERTEXSHADER. Class 4 is OBJECT.

### Values

| Typedef | Value at `value_off` | Status |
|---|---|---|
| class 4, type texture / shader | one DWORD, the object id | Confirmed (data) |
| class 4, type sampler | `{ nstates, nstates x state }` | Confirmed (data) |
| state constant | one DWORD; the typedef is class 2 with cols = rows = 1, type INT or BOOL | Confirmed (data): all 461 constant states |

Sampler arrays do not occur (all 122 samplers have `element_count` 0). The tool reads an array as
consecutive sampler blocks, which is **inferred**.

### Header counts

- `nobjects`: object ids run from 1 to `nobjects - 1`. Id 0 never has data. **Confirmed (data).**
- `unknown`: equals shader blobs + passes + `nsmall` in all 34 effects. What it means is not
  known; it is an observed identity, so **inferred**. (Every technique here has exactly one pass,
  so "passes" could equally be "techniques".)

## Object table

The object table starts right after the last technique and runs to the last byte of the file.
**Confirmed (data): all 34 effects parse with zero trailing bytes.**

### Parameter objects (`nsmall` records)

`{ object_id, size, data }`. Each id is the object id of a parameter. In the game all 122 are
texture parameters with size 0: a texture has no data in the file, the game supplies it with
`SetTexture`. Ids are listed in descending order. **Confirmed (data).**

String and shader parameters would presumably carry their bytes here, but no effect has one, so
that is **inferred**.

### State resources (`nresources` records)

Each record gives the value of one state that is not a plain constant.

| Field | Pass state | Sampler state |
|---|---|---|
| `technique_index` | technique | 0xFFFFFFFF |
| `index` | pass | parameter index |
| `element_index` | 0xFFFFFFFF | 0 |
| `state_index` | index into the pass's states | index into the sampler's states |

**Confirmed (data):** every record addresses an existing state (597 pass, 347 sampler).

`usage` selects what the data is:

| usage | Data | Seen | Status |
|---|---|---|---|
| 0, shader state | shader bytecode, from the version token to the END token `0x0000FFFF` | 591 | Confirmed (data) |
| 0, other state | compiled expression, first DWORD `0x46580200`, last DWORD `0x0000FFFF` | 231 | Blob boundaries confirmed (data); contents not decoded |
| 1 | NUL-terminated name of the parameter the state takes its value from | 122 | Confirmed (data): every name is a parameter of the same effect |
| 2 | not seen | 0 | Not decoded; `--check` fails if it appears |

Shader versions: vs_1_1 27, vs_2_0 199, vs_3_0 71, ps_2_0 223, ps_3_0 71.

For an object-class state (shader, texture) the state's value is an object id, and the record's
data is that object's data. Counting those together with the parameter objects, every id from 1
to `nobjects - 1` has exactly one record. **Confirmed (data).**

### Shader states

- `VertexShader` (146): all 297 resolve to a `vs_*` blob. **Confirmed (data).**
- `PixelShader` (147): 294 resolve to a `ps_*` blob. **Confirmed (data).**
- The other 3 are `RenderLiSPSM`, `RenderLiSPSMMatrixPalette` and `RenderLiSPSMAlphaTex` in
  `IDI_WORLDBONE_FX`. Their typedef is INT, the value is the constant 0, and no resource record
  addresses them. **Confirmed (data)** that it is a literal constant, not an object. Reading the
  0 as `PixelShader = NULL` (a depth-only shadow pass) is **inferred**.

## State operations

The operation is an index into D3DX's effect state table. The names below are **Confirmed
(Wine)**; the "Evidence" column is what the game data adds. The D3D9 enum values are from
`d3d9types.h`.

### Pass states

`index` is 0 for every pass state.

| Op | State | D3DRS | Values seen | Evidence in the game data |
|---|---|---|---|---|
| 0 | `D3DRS_ZENABLE` | 7 | 1 | BOOL or INT; depth passes of WORLDDEPTH |
| 3 | `D3DRS_ZWRITEENABLE` | 14 | 0, 1 | 1 in WORLDDEPTH, 0 in the PARTICLES passes |
| 6 | `D3DRS_SRCBLEND` | 19 | 5 (SRCALPHA) | PARTICLES `streak_flares`, next to op 13 = 1 |
| 7 | `D3DRS_DESTBLEND` | 20 | 2 (ONE) | same pass: additive blending for flares |
| 8 | `D3DRS_CULLMODE` | 22 | 1 (NONE), expression | NONE on 9 of the 11 SCREENEFFECT full-screen passes |
| 9 | `D3DRS_ZFUNC` | 23 | 4 (LESSEQUAL) | a valid `D3DCMPFUNC` |
| 13 | `D3DRS_ALPHABLENDENABLE` | 27 | 0, 1 | 0 in WORLDDEPTH, 1 in `streak_flares` |
| 14 | `D3DRS_FOGENABLE` | 28 | 0 | BOOL |
| 73 | `D3DRS_COLORWRITEENABLE` | 168 | 1, 7, expression | 1 (red only) in WORLDDEPTH, 7 (RGB) in PARTICLES `no_fuzzz` |
| 99 | `D3DRS_SEPARATEALPHABLENDENABLE` | 206 | 1 | BOOL; SMOKE only |
| 100 | `D3DRS_SRCBLENDALPHA` | 207 | 2 (ONE) | SMOKE, with op 99 = 1 |
| 101 | `D3DRS_DESTBLENDALPHA` | 208 | 2 (ONE) | SMOKE, with op 99 = 1 |
| 146 | `VertexShader` | | object | typedef type 16; data is `vs_*` bytecode |
| 147 | `PixelShader` | | object, or 0 | typedef type 15; data is `ps_*` bytecode |

The evidence is consistent with the names but would not prove them alone (for example ops 0 and
3 both take 0/1). The Wine table is what fixes them.

Ops 8 and 73 are expressions only in `IDI_UCAP_FX` (3 passes each).

### Sampler states

`D3DSAMP_*` states do not appear in passes. They are the states inside sampler parameters, where
`index` is 256 for every state. The meaning of 256 is not known.

| Op | State | D3DSAMP | Values seen | Evidence in the game data |
|---|---|---|---|---|
| 164 | `Texture` | | parameter name | all 122 are usage-1 references to a texture parameter |
| 165 | `D3DSAMP_ADDRESSU` | 1 | 1 to 4 | valid `D3DTEXTUREADDRESS` (WRAP, MIRROR, CLAMP, BORDER) |
| 166 | `D3DSAMP_ADDRESSV` | 2 | 1 to 4 | as 165 |
| 167 | `D3DSAMP_ADDRESSW` | 3 | 1, 3 | only on the 4 samplers of `VOLUMEMAP_TEXTURE` |
| 168 | `D3DSAMP_BORDERCOLOR` | 4 | 0xFFFFFFFF | present on exactly the 21 samplers with U = V = 4 (BORDER); all are `SHADOWMAP_TEXTURE` |
| 169 | `D3DSAMP_MAGFILTER` | 5 | 1, 2, expression | POINT, LINEAR |
| 170 | `D3DSAMP_MINFILTER` | 6 | 1, 2, expression | POINT, LINEAR |
| 171 | `D3DSAMP_MIPFILTER` | 7 | 0, 1, 2, expression | NONE, POINT, LINEAR |
| 174 | `D3DSAMP_MAXANISOTROPY` | 10 | expression | 6 samplers |

Ops 169, 170 and 171 are expressions on 73 samplers each, so the HLE cannot apply those samplers
correctly until the expression VM exists.

## Expressions

All 231 expression blobs have the same shape: a `CTAB` naming one parameter, an empty `CLIT`,
and an `FXLC` block with one instruction, a one-component `mov` (opcode `0x10000001`) from
that constant to the output. They are the compiled form of `MinFilter = <ciMinFilter>;` and
similar, so the state's value is the parameter's first component. **Confirmed (data):**
`lifter/runtime/d3dx_effect.c` reports any expression that is not of this shape when it loads
an effect, and reports none for the 32 effects the game creates.

Every shader blob has a `PRES` comment, but in 508 of the 591 it is the bare tag with no body.
The other 83 carry a real preshader (308 to 532+ bytes), which is not decoded.

## Runtime

`lifter/runtime/d3dx_effect.c` implements `ID3DXEffect` on this format: parameters and their
default values, techniques, passes, annotations, handles, and `Begin`/`BeginPass`/
`CommitChanges`, which send render states, shaders, named shader constants (matched through
each shader's `CTAB`), textures and sampler states to the effect's state manager (the game
installs its own with `SetStateManager`) or to the device. The dumped image has an empty
`.rsrc`, so `boot.c` maps that section from the original executable.

## Not decoded

- The 83 real `PRES` preshaders inside the shader blobs (the constants they compute are not set).
- Parameter default values, and the `flags` DWORD of a parameter.
- The meaning of the `unknown` header count and of state `index` 256.
- Resource `usage` 2, sampler arrays, and string or shader parameter objects: none occur in the
  game.
