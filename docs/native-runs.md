# Running the native build

How to boot the lifted game natively (Linux, x86-64 or aarch64), drive it with scripts, and look at frames. Nothing here contains game data: you need your own unpacked image (`nfs.exe.unpacked`) and your own install.

## Build and run

```bash
scripts/lift_boot.sh /path/to/nfs.exe.unpacked      # lift (seeds, hooks, CALLCHK on by default)
scripts/build_runtime.sh                            # STRICT=1 turns warnings into errors
PROSTREET_GAMEDIR="/path/to/Need for Speed ProStreet" \
PROSTREET_HEADER_IMAGE=/path/to/nfsps.exe \
scripts/run_boot.sh /path/to/nfs.exe.unpacked 300
```

- `PROSTREET_GAMEDIR` may be relative (default `game`) or absolute. The game sees an absolute directory as `C:\...`.
- Saves go to `./savedata` in the working directory. Keep a copy of a saved profile and restore it before each run: with a profile present, boot reaches the main menu at about frame 5840, with no Alias dialogs.
- The frontend runs at 60 frames per second and races at the game's own 30 fps cap. A scripted race takes about 7 minutes.

## Scripted runs (`PROSTREET_SCRIPT`)

`lifter/runtime/autotest.c` steps a script once per Present. It waits for the frontend itself rather than for frame numbers. Steps are comma-separated:

| Step | Meaning |
|---|---|
| `wait=FeMainMenu.fng[:T]` | until that package has input control (give up after T frames, default 3000) |
| `tap=28:PKG[:T]` | tap scan code 28 every 60 frames until PKG has control (`!PKG`: until PKG loses it) |
| `flow=6[:T]` | until the game-flow state (0x00abb510) is N: 3 frontend, 6 racing |
| `dialog[:T]` | until the open dialog accepts (DialogInstance 0x00ab2870, +0x28 == 1) |
| `key=200[:H]`, `key=200+205:H` | hold one to three DirectInput scan codes for H frames (default 6) |
| `text=PLAYER` | type it (WM_CHAR) |
| `frames=N` | wait N frames |
| `dump` | render the next frame in software (needs `PROSTREET_FRAMEDUMP=999999`) |

`PROSTREET_SCRIPT_LOG=1` logs every change of the package with control and of the dialog.

Scan codes: 28 Enter, 200 Up (accelerate), 208 Down, 203 Left, 205 Right, 1 Esc.

From a saved profile into a Race Day race (Freestyle event, SOLO, one car per discipline):

```
frames=5700,key=28,tap=28:FeMainMenu.fng,frames=90,key=208,frames=30,key=28,
frames=300,key=28,wait=FERaceDayHubMap.fng,frames=200,key=205,frames=60,key=28,
wait=FeDialog.fng,frames=100,key=28,wait=Car_Select.fng,frames=300,key=28,
frames=300,key=28,frames=300,key=28,frames=300,key=28,flow=6:9000,
wait=FEHubOnline.fng,tap=28:!FEHubOnline.fng,wait=FeEventMatchup.fng:4000,
frames=120,tap=28:!FeEventMatchup.fng,frames=700,key=200:90,dump
```

Without a profile, the first-run flow needs `PROSTREET_TEXT=<frame>:<name>` (or `text=` in a script) for the Alias name.

## Frames

`PROSTREET_FRAMEDUMP=N` renders frame N with the software rasteriser (`lifter/runtime/swrast.c`) and writes each render target to `out/frame_N_rt*.ppm`. The last target is the composed frame. `_LOG=1` adds per-draw details. The rasteriser exists to check rendering offline; it is not a display.

## Switches

| Variable | Effect |
|---|---|
| `PROSTREET_KEYS=600:28+30,...` | frame-timed key presses (older than `PROSTREET_SCRIPT`) |
| `PROSTREET_TEXT=9000:PLAYER` | frame-timed typing |
| `PROSTREET_CALLCHK=log\|off` | callee returned elsewhere: follow it (default), only report, or ignore |
| `PROSTREET_FOREIGN=stop` | stop at calls into heap code (AEMS audio modules) instead of skipping them |
| `PROSTREET_AUDIO_VOICES=1` | run EA's voice module chains (they fault until AEMS modules are lifted) |
| `PROSTREET_GAMEFIXES=0` | turn off the fixes for bugs in the original game (`gamefixes.c`) |
| `PROSTREET_ATTR_GUARD=1` | main's attribute-tree stand-ins (off: the lifted code loads it) |
| `PROSTREET_HDFONT=0` | no HD-font-pack glyph correction |
| `PROSTREET_D3DX_LOG=1` | one line per D3DX call (very large logs) |
| `PROSTREET_RESTORE=0xLO-0xHI` | restore more `.data` ranges from the original executable |
| `PROSTREET_NULL_STALE=0xLO-0xHI` | null more dead heap pointers in a range |
| `PROSTREET_WATCH_VAL=0xADDR` | report each change of a guest word |
| `PROSTREET_TEXDUMP=N` | dump the textures effects bind in frame N |
| `PROSTREET_COMTRACE=1` | trace COM calls |

## Known limits

- **No sound from EA's AEMS modules.** They are x86 code loaded from the sound data at run time. Calls into them are skipped and logged.
- **No display window.** Frames are checked offline.
- **The process dump carries the dumped session's state.** Each crash so far has been a stale static, a SecuROM call shape the lifter did not know, or a Windows stand-in. `boot.c` documents every range it resets and why.
