# Mods, patches and stability fixes

The retail game is from 2007 and crashes on modern systems. The community
fixes that make it stable on Windows are also a **specification for this
port**: the port runs the same game code, so the bugs those fixes patch are
ours too.

Nothing in this file is distributed with the repository. The mod binaries
belong to their authors; install them from your own copy (the `game/`
directory is gitignored). What is recorded here is the exact inventory and
hashes, so a run can say which mods were active.

## The reference install (this machine)

The user's own repack ships an ASI loader and four plugins. Inventory is
reproducible with `scripts/mod_inventory.py`:

| file | SHA-256 (first 16) | role |
|---|---|---|
| `dinput8.dll` | `51ff8090fd73df83` | [Ultimate ASI Loader](https://github.com/xan1242/Ultimate-ASI-Loader) (MIT) |
| `scripts/NFSProStreet.GenericFix.asi` | `667689cab8764b06` | Generic Fix v8.01.2023 (xan1242) |
| `scripts/NFSProStreet.GenericFix.ini` | `0b24ba50e8b1789f` | its configuration |
| `scripts/CamDodgeDisabler.asi` | `647434edc6c9fbc3` | disables the camera dodge in races |
| `scripts/NFSPSExtraOptions.asi` | `a9321ae253a38ca2` | extra options / debug hotkeys (NFSMWExOpts family, GPL-3.0) |
| `scripts/NFSPSExtraOptionsSettings.ini` | `fc6ec477a5eeb715` | its configuration |
| `scripts/NFSPSHDReflections.asi` | `fe4fe72aed125970` | higher-resolution vehicle reflections ([GPL-3.0](https://github.com/xan1242/NFSHDReflections)) |
| `scripts/NFSPSHDReflections.ini` | `70a212a28371e634` | its configuration |
| `scripts/NFSPSHUDAdapter.asi` | `5ef61391369d5858` | HUD scaling for non-16:9 modes |

**Licensing.** These plugins are third-party binaries under their own licences
(MIT and GPL-3.0 among them). This project is MIT and does **not** link against
or copy their code; they are named only so the behaviour we reimplement can be
compared. Read them as documentation, not as a source of code to paste.

A useful *reference* (also not vendored) is
[`xan1242/nya-common-nfsps`](https://github.com/xan1242/nya-common-nfsps)
(MPL-2.0), a community reimplementation of ProStreet-era game classes — the
kind of structure documentation that is otherwise missing for a 2007 title.

Mods that only change rendering (HD Reflections, HUD Adapter) are S3 concerns.
Generic Fix is the one that matters for S2/S5 stability.

## Generic Fix — what the port has to reproduce

Source: NFS ProStreet Generic Fix v8.01.2023 (xan1242), distributed via the
NFS modding community; not on GitHub as of this writing. The `.asi` applies
runtime patches to the game process. None of it can be loaded by the port,
so each fix becomes either native runtime behaviour or a patch applied by our
loader.

### Stability (S2/S5)

| option | fix | what the port does |
|---|---|---|
| `AntiTrackStreamerCrash` | memory checks around the track streamer | **needed**: same game code, same bug; port the checks |
| `AntiFEScriptCrash` | checks in `FE::Object::SetScript` | **needed** |
| `DamageMemoryLeakFix` | plugs a leak in damage pools | **needed** for long sessions; port the patch |
| `DisablePunkBuster` | keeps PunkBuster out | automatic: the port has no PB |
| `DisableAchievements` | disables the unused achievement system | automatic if we stub the achievement service; otherwise port |
| `PostRaceFix` | post-race screen stuck on "Continue" (v1.1+) | **needed**: it is a game bug, not a Windows bug |
| `Win11LANFix` | LAN on Windows 11 | irrelevant (S6 reimplements networking) |
| `FramerateUncap` | removes the 30/60 fps lock | replaced by the runtime's frame pacing |

### Rendering / presentation (S3)

`ResDetect`, `FixAspectRatio`, `Scaling`, `FMVWidescreenMode`,
`ShadowRes`, `BrakeLightFix`, `GammaFix`, `FixXenonEffects`,
`DisableMotionBlur`, `SimRate`, `WindowedMode` — the port owns the whole
presentation path, so these become renderer and pacing features rather than
patches. They are still useful as the definition of "correct" output.

### Quality of life

`SkipIntro`, `SkipFEBootflow`, `ConsoleHUDSize`, `LeftStickDeadzone`,
`CustomUserFilesDirectoryInGameDir`, `WriteSettingsToFile`. These are
`Options` in the port, not patches.

## If a mod patches `nfsps.exe` itself

Some mods do; these here do not (they hook at runtime). A patched executable
changes the lift input, and therefore the SHA-256 in `config/reference.json`
and every function address. If that happens:

```bash
sha256sum game/nfsps.exe          # must match config/reference.json
./scripts/lift.sh                 # re-lift the patched image
```

and record the new identity in `config/reference.json` plus a note in this
file. Never mix a lift made from one executable with data from another build.

## Using the mods with a Windows reference run

The S2 gate compares our import and file-open traces with a Windows run. State
whether the mods were active:

* **with** Generic Fix: the reference behaves like the "fixed" game, matching
  what the port intends to implement;
* **without**: closer to the retail binary we lift.

Use **without** mods for the first trace diff, then **with** to check that the
fixes we port produce the same behaviour. The loader DLL and plugins are
Windows-only and do not exist in the browser build.
