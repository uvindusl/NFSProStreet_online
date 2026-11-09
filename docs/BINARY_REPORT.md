# Binary report — `nfsps.exe` (Need for Speed: ProStreet, PC v1.1)

Everything below was measured on the reference executable with the scripts in
`scripts/`. Reproduce with:

```bash
.venv/bin/python scripts/analyze_exe.py game/nfsps.exe
.venv/bin/python scripts/seed_scan.py  game/nfsps.exe 16 work/seeds.txt
./scripts/lift.sh
```

## Identity

| | |
|---|---|
| File | `nfsps.exe` (EA Black Box, Dec 2007) |
| Size | 28,739,656 bytes |
| SHA-256 | `ee8a71c7c1e42923deb728e39a89317645cbcf26fc87cc48d3e07ade4dce2f72` |
| Format | PE32, i386, GUI |
| ImageBase | `0x00400000` |
| SizeOfImage | `0x01b687e2` |
| Entry point | `0x00828c25` (VA; RVA `0x00428c25`) — stock MSVC 2005 `_WinMainCRTStartup` |
| Timestamp | `1197239854` (2007-12-09) |
| Base relocations | **none** (directory zeroed) |
| Imports | 371 from 14 DLLs |

Sections:

| name | VA | virtual size | entropy | characteristics |
|---|---|---|---|---|
| `.text` | `0x001000` | `0x566000` | 6.65 | X R |
| `.rdata` | `0x567000` | `0x0e3000` | 5.56 | R |
| `.data` | `0x64a000` | `0x5c6000` | 0.38 | RW |
| `.tls` | `0xc10000` | `0x001000` | – | RW |
| `.rsrc` | `0xc11000` | `0x0d9000` | 3.74 | R |
| `ars` | `0xcea000` | `0x5d0000` | 6.55 | XWR |
| `est` | `0x12ba000` | `0x008000` | 6.19 | X R |
| `artem` | `0x12c2000` | `0x2e9000` | 5.69 | XWR |
| `celare` | `0x15ab000` | `0x006000` | 5.66 | WR |
| `.securom` | `0x15b1000` | `0x5b77e2` | 2.15 | XWR |

`ars`, `est`, `artem`, `celare` and `.securom` are SecuROM 7 artefacts. `ars`
carries 211 pointer-looking values that point into `.text`, and the string
`SecuROM` occurs 46 times in the image. They are dead weight for the port: the
game's own code is in `.text`, which is decrypted in this build (the entry point
is ordinary MSVC code and 1.8 M instructions lift out of it cleanly).

**Consequence:** function entry points cannot come from a relocation table, so
they are recovered from dwords in the non-executable sections that point into
`.text` (13,342 unique with 16-byte alignment; 17,787 without), plus the
direct-call graph and jump tables. This is exactly what
[pcrecomp](https://github.com/sp00nznet/pcrecomp)-based ports do for images
without relocations.

## Imports the runtime must provide

| DLL | # | Notes |
|---|---|---|
| `KERNEL32.dll` | 96 | files, heap, threads, sync, modules, time |
| `MSVCR80.dll` | 156 | **MSVC 2005 CRT** (UG2 uses MSVCR71 — names change) |
| `MSVCP80.dll` | 3 | `std::string` ctor/dtor/copy — C++ ABI |
| `USER32.dll` | 40 | window, message loop, cursor, `SendInput` |
| `WS2_32.dll` | 40 | Winsock 2 (online) |
| `ADVAPI32.dll` | 11 | registry, crypto, `CheckTokenMembership` |
| `d3dx9_34.dll` | 14 | **effect framework** + matrix/vector helpers + surface save |
| `d3d9.dll` | 1 | `Direct3DCreate9` |
| `WINMM.dll` | 3 | `timeGetTime`, `timeBeginPeriod/EndPeriod` |
| `RPCRT4.dll` | 2 | GUID → string |
| `DINPUT8.dll` | 1 | `DirectInput8Create` |
| `DSOUND.dll` | 1 | `DirectSoundCreate` (ordinal 1) |
| `ole32.dll` | 1 | `CoCreateGuid` |
| `SHFOLDER.dll` | 1 | `SHGetFolderPathA` |
| `NETAPI32.dll` | 1 | `Netbios` |

### `d3dx9_34.dll` is the crux

```
D3DXCreateEffectFromResourceA   D3DXCreateEffectPool
D3DXGetVertexShaderProfile      D3DXGetPixelShaderProfile
D3DXCreateCubeTextureFromFileInMemory
D3DXCreateVolumeTextureFromFileInMemory
D3DXSaveSurfaceToFileA / InMemory
D3DXMatrixMultiply / MultiplyTranspose / Transpose / Inverse
D3DXVec3Transform / D3DXVec4Transform
```

Underground 2 has the D3DX effect framework **statically linked into the exe**,
which is why nfsu2-web can HLE it at fixed guest addresses. ProStreet calls the
framework in `d3dx9_34.dll` instead, so there are two options:

1. **Lift the DLL too.** The lifter is generic; `d3dx9_34.dll` is PE32 with
   relocations, and `nfsu2-web` already lifts a second module (`server.dll`)
   into the same guest and binds its exports. Generated C stays local, exactly
   like the lifted game code.
2. **Write an HLE of the effect framework.** Parse the FX9 binary (state
   blocks, annotations, preshaders), evaluate state expressions and feed WebGPU.

Option 1 is the pragmatic one for a first playable build; option 2 is the
long-term performance path (nfsu2-web measured effect-state evaluation at ~48 %
of a race frame before memoising it).

## Shader models

Shader bytecode is embedded in the image (resources and the static D3DX code).
Version-dword occurrence counts over the whole file, so they are indicative
rather than a shader census:

| vs version | count | | ps version | count |
|---|---|---|---|---|
| `vs_1_1` | 54 | | `ps_1_1` | 10 |
| `vs_2_0` | 398 | | `ps_2_0` | 447 |
| `vs_2_x` (0201) | 4 | | `ps_2_x` (0201) | 4 |
| `vs_3_0` | 142 | | `ps_3_0` | 178 |

The strings `vs_3_0`/`ps_3_0` also occur 72 times each, i.e. the game asks the
device for SM3 profiles at run time.

So the port needs **shader model 2 and 3**, not just the ps_1_x/vs_1_1 pair the
existing translators were written for. `vsemu.c` already tokenises ps_2_0 and
has an interpreter with `if/ifc/rep/loop`, but the WGSL emitter must grow
`ps_3_0` coverage (dynamic branching, `texldl`, `dsx/dsy`, more registers).

## Lift result

| | |
|---|---|
| Function set | **27,097** (25,750 main + 1,347 closure) |
| Instructions lifted | **1,800,234** |
| Generated C | **98.1 MB** in 32 shards |
| Time | ~100 s on 8 cores |
| Lift failures | **0** |
| Unsupported-instruction stubs | 234 sites in 24 functions, **0 reachable from the entry point** |
| Truncated branches | 0 |
| Jump tables recovered | 570 |
| Traps per reason (all dead) | `lcall` 208, `les` 6, `das` 4, `lift-error` 2, `ljmp` 2, `iretd` 2, `aaa` 2, `int1`/`int`/`aam`/`sldt`/`daa`/`aas`/`salc`/`base register 8` 1 each |

The first audit ("297 sites in 48 functions") counted every stub. The
reachability audit (`scripts/classify_traps.py`, which builds the call graph
out of the generated C) shows that no stub is in code reachable from
`0x00828c25`. The residue lives in function-shaped data: the `lcall` sites are
almost all at 0x800/0x1000 boundaries (`0x00820800`, `0x0091f320`, …) whose
bytes decode as far calls, i.e. pointer-table entries that are not code.

All eight lifter gaps found in real code are fixed in `tools/` (see
`tools/README.md`): `lock`-prefixed `xadd`/`cmpxchg`/`add`, `cmpxchg8b`,
`pshufd` with a memory source, `fstpnce`, and `repz ret`. The `pshufd` sites
were the last reachable ones (`sub_0090c7f0`), and 0x8efe80 alone had 8 of
them.

## Toolchain validation

Two independent checks, both part of `scripts/lift.sh` and CI-adjacent:

* `scripts/audit_lift.py` counts the stubs; `scripts/classify_traps.py`
  separates reachable from dead by building the call graph out of the
  generated C.
* `scripts/host_compile_check.sh` compiles **every** shard with
  `clang -O1 -c -I ref-nfsu2-web/runtime`; the generated C is ordinary,
  portable C over the `CPU *restrict c` state struct. A shard takes ~10 s.

The one 1.8 M-instruction sample that used to fail to compile (`pshufd` with a
memory operand) was fixed in the lifter; the check is now the compile gate for
future lifter changes.

## Game data layout (from the installer)

`CARS`, `SOUND`, `SUBTITLES`, `AIRACELINES`, `NIS`, `LANGUAGES`, `GLOBAL`,
`TRACKS`, `MOVIES`, `ONLINE`, `MEMCARD`, `PB`, `FRONTEND`, `MINI_MAPS`,
`scripts`, plus `server.dll` and the PunkBuster folder. The runtime will need to
serve these through the file layer, as nfsu2-web does.

## Open questions

1. **Are all 27,097 entries real functions?** The reachability audit says no
   trap is in code the entry point can reach, but that does not enumerate the
   set independently. The Ghidra headless analysis + address diff is the
   remaining S1 item; the data seeds are expected to show up as extra entries
   with no callers.
2. Which SecuROM sections, if any, are referenced by reachable code? (None
   expected; they are left unmapped in the runtime plan.)
3. Does the reference build match the retail v1.1 image after SecuROM is
   stripped? The `.text` is expected to be identical; this should be confirmed
   with a clean copy before publishing hashes.
