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
- `PROSTREET_HEADER_IMAGE` is the original executable: the PE header page (SecuROM reads constants from it), `.rsrc` (the compiled effects) and `.data` (the clean values the boot repairs compare against) come from it. Without it the boot looks for `<gamedir>/nfsps.exe`, then `<gamedir>/nfs.exe`. A retail `nfs.exe` is the SecuROM launcher, with no `.data` and no game `.rsrc`: the boot then repairs `.data` against zeros (restored ranges are zeroed) and keeps the unpacked image's own `.rsrc`.
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

From a saved profile into a Race Day race (Freestyle event, SOLO, one car per discipline). Steps may be wrapped over lines as below: the spaces and newlines around each step are dropped. Run it with `PROSTREET_FRAMEDUMP=999999` so the final `dump` renders the race frame:

```
frames=5700,key=28,tap=28:FeMainMenu.fng,frames=90,key=208,frames=30,key=28,
frames=300,key=28,wait=FERaceDayHubMap.fng,frames=200,key=205,frames=60,key=28,
wait=FeDialog.fng,frames=100,key=28,wait=Car_Select.fng,frames=300,key=28,
frames=300,key=28,frames=300,key=28,frames=300,key=28,flow=6:9000,
wait=FEHubOnline.fng,tap=28:!FEHubOnline.fng,wait=FeEventMatchup.fng:4000,
frames=120,tap=28:!FeEventMatchup.fng,frames=700,key=200:90,dump
```

Without a profile (no `./savedata`), the game opens the first-run flow: "Create a new Alias?", the autosave notices, the Alias name, driver aids and the sponsor notice. This script answers YES, names the Alias `PLAYER` and ends at the main menu (frame 12462 in a checked run). The steps are frame-timed because the dialogs keep the same package (`FeDialog.fng`) in control:

```
frames=5700,key=28:20,frames=660,key=205:15,frames=170,key=28:20,frames=760,key=28:20,
frames=760,key=28:20,frames=710,text=PLAYER,frames=125,key=28:20,frames=810,key=28:20,
frames=1110,key=28:20,frames=360,key=28:20,frames=860,key=28:20,wait=FeMainMenu.fng:6000
```

The profile is then saved in `./savedata/NFS ProStreet/` (`ALIAS_PLAYER`, `CAREER_01`). Copy that directory once (`cp -a savedata savedata.base`) and restore it before each run (`rm -rf savedata && cp -a savedata.base savedata`).

## Frames

`PROSTREET_FRAMEDUMP=N` renders frame N with the software rasteriser (`lifter/runtime/swrast.c`) and writes each render target to `out/frame_N_rt*.ppm`. The last target is the composed frame. `_LOG=1` adds per-draw details. The rasteriser exists to check rendering offline; it is not a display.

## Audio module banks (AEMS)

EA's sound library loads "module banks" from the sound data: relocatable x86 code and data that it copies into memory and resolves itself (`sub_008b9e60`, `sub_008b9bf0`). A resolved bank depends only on the address it sits at, so `lifter/runtime/aems.c` pins every bank to a fixed address in `0x6c000000-0x6ff00000` (the address comes from a hash of the bank's bytes) and the bank's code is lifted ahead of time like the image's:

1. Run with `PROSTREET_AEMS_DUMP=1`. Each bank is written to `out/aems/bank_<hash>_<base>.bin` and listed in `out/aems/banks.txt`; every call that reaches a bank address with no lifted function is added to `out/aems/seeds.txt` as bank hash and offset.
2. `scripts/lift_aems.py` lifts those entry points (and what they call directly) into `out/lift-aems/`, with a table of the banks' addresses.
3. `scripts/build_runtime.sh` compiles `out/lift-aems/` when it exists. The next run pins each known bank at its recorded address, and dispatch runs the lifted functions; a call to a bank function that is still unlifted is skipped and logged (`foreign code: call to an unlifted AEMS bank`) and lands in `seeds.txt` for the next lift.

A few rounds of run and lift cover what a script reaches; the Race Day script loads about 20 banks and reaches about 25 entry points. Some banks change from run to run (the same slot held five different banks in five runs, likely the opponents' car sounds), so a new one can still show up as unlifted. Everything in `out/aems/` and `out/lift-aems/` is game data and stays out of git. `PROSTREET_AEMS_LOG=1` logs each bank without dumping; `PROSTREET_AEMS_PIN=0` leaves banks where the game puts them.

`PROSTREET_WAVDUMP=1` appends what the mixer writes into each DirectSound buffer to `out/audio/dsbuf_<buffer>.pcm` (format in the `.txt` next to it). The first non-silent write is logged either way (`audio: first non-silent mix ...`).

## Switches

| Variable | Effect |
|---|---|
| `PROSTREET_KEYS=600:28+30,...` | frame-timed key presses (older than `PROSTREET_SCRIPT`) |
| `PROSTREET_TEXT=9000:PLAYER` | frame-timed typing |
| `PROSTREET_CALLCHK=log\|off` | callee returned elsewhere: follow it (default), only report, or ignore |
| `PROSTREET_FOREIGN=stop` | stop at calls into heap code (AEMS audio modules) instead of skipping them |
| `PROSTREET_AUDIO_VOICES=1` | run EA's voice module chains (they still fault in a race, see Known limits) |
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
