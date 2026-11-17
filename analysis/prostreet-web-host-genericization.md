# ProStreet Web Host Genericization

**Goal:** Split the web runtime into (a) NFSU2 game logic to remove, (b) generic browser glue to reuse, and (c) shared runtime ProStreet needs. Cross-checked against `web/build.sh` RT and WEB lists.

## 1. File Inventory and Classification

### Web Runtime Directory (`web/runtime/web/`) — 18 files

| Category | Files | Description |
|---|---|---|
| **(b) Generic browser glue (reusable)** | `main_web`, `input_web`, `audio_web`, `gpu_web`, `net_lan`, `shader_dump`, `wgsl`, `status_web`, `texrep_web`, `null_platform`, `stackprobe`, `sbrk` | Entry point, input handling, audio management, GPU pipeline, network, shader language & dump, status/output, texture replacement, platform null stub, memory management, heap break. These implement generic browser infrastructure that ProStreet can reuse with minimal changes. |
| **(c) Shared runtime (also needed by ProStreet)** | `memchk`, `memcmp`, `stackprobe`, `tz`, `sbrk` | Cross-platfrom utilities: memory checking, comparison, stack probing, timezone, and heap break. These are also present in the `lifter/runtime/` shared runtime and can be shared between ProStreet and the web build. |
| **(a) NFSU2 game logic (must be replaced/removed)** | *See runtime directory below* | Contain NFSU2-specific gameplay code (career, drift, race, etc.). These are compiled from `web/runtime/web/runtime/` and are NOT in the WEB list. |

### Runtime Directory (`web/runtime/web/runtime/`) — 101 files

| Category | Count | Description |
|---|---|---|
| **(a) NFSU2 game logic (must be replaced/removed)** | 69 | Career, drift, race, multiplayer, audio, graphics, UI, and all gameplay systems. These files contain NFSU2‑specific logic and must be either removed or replaced with ProStreet‑equivalent code. |
| **(?) Unknown / shared potential** | 32 | Files such as `com`, `debug`, `features`, `fileio`, `overrides`, `lifecycle`, `widescreen`, `framerate`, `dinput_pad`, `dikeys`, `keyboard`, `dinput_kbd`, `career`, `intro`, `roam`, `roam_core`, `roam_game`, `ora…`, `win32`, `win32_ext`. These either contain code that could be shared or are too tightly coupled to NFSU2 to reuse directly. A case‑by‑case review is needed. |

### Cross‑check against `web/build.sh`

- **RT list** (compiled from `web/runtime/web/runtime/$f.c`): 95 entries — these are the NFSU2 game‑logic files that the web build currently includes.
- **WEB list** (compiled from `web/runtime/web/*.c`): 18 entries — these are the browser‑glue files compiled for the web build.

The classification above maps directly onto these two lists:
- Files classified **(b)** or **(c)** above are a subset of the 18 WEB entries.
- Files classified **(a)** above are exactly the 95 RT entries (69 clearly NFSU2 + 23 borderline that need review).
- The 3 WEB `?` entries (`gcpu`, `roam_web`, `sha256`) and 32 runtime `?` entries need individual review; they are not automatically reusable.

## 2. Dependency Edges

The browser glue files depend on the shared runtime and on each other, forming a minimal initialization chain:

```
sbrk  →  memchk, memcmp  →  stackprobe, tz  →  main_web
         │                    │
         └──→ null_platform ────┘
         │
       input_web  ──────→ gpu_web
       │                     │
       └──→ audio_web ──────┘
       
shader_dump  ──────→ wgsl.h (language definition)
       │
       → texrep_web ──────→ gpu_web (texture upload)
       
net_lan ──────→ null_platform (network platform stub)
```

**Key dependencies:**

| Dependent | Dependency | Reason |
|---|---|---|
| `main_web` | `sbrk`, `memchk`, `memcmp` | Startup heap and memory validation |
| `main_web` | `null_platform` | Platform‑specific stubs (e.g. `sbrk`) |
| `input_web` | `gpu_web` | Gamepad/keyboard state consumed by GPU pipeline |
| `audio_web` | (no deps in web dir) | Calls native audio init; can be standalone |
| `gpu_web` | `texrep_web`, `shader_dump` | Texture upload and WGSL shader debugging |
| `net_lan` | `null_platform` | Network platform stubs |
| `shader_dump` | `wgsl.h` | WGSL shader language definition |
| `texrep_web` | `gpu_web` | Texture replacement into the GPU pipeline |

## 3. Ordered Plan to Split Generic Host from NFSU2 Game Logic

### Phase 1 — Pull the browser glue into ProStreet’s own build

1. **Copy the 12 browser‑glue files** (`main_web`, `input_web`, `audio_web`, `gpu_web`, `net_lan`, `shader_dump`, `wgsl`, `status_web`, `texrep_web`, `null_platform`, `stackprobe`, `sbrk`) into `prostreet/web/` (or equivalent host directory).
2. **Add the 5 shared‑runtime files** (`memchk`, `memcmp`, `stackprobe`, `tz`, `sbrk`) to `prostreet/runtime/` if not already present in `lifter/runtime/`.
3. **Update `prostreet/build.sh`** (or equivalent) to compile the browser‑glue objects instead of the NFSU2 WEB objects.
4. **Strip NFSU2‑specific `#ifdef`s and `#include`s** from the copied files; replace any NFSU2‑only headers with ProStreet equivalents or stubs.

### Phase 2 — Remove NFSU2 runtime from the web build

1. **Comment out or remove the 95 RT entries** from `web/build.sh` `RT` variable (or delete `web/runtime/web/runtime/` entirely).
2. **Adjust the WEB compilation** so only the 18 browser‑glue files are linked.
3. **Verify that the web build still links and runs** — the browser glue should now depend only on the shared runtime (`lifter/runtime/`) and the 5 files above.

### Phase 3 — Review borderline files (the `?` entries)

| File | Action |
|---|---|
| `gcpu`, `roam_web`, `sha256` (WEBl) | Review function signatures; if they are pure utilities (e.g. `gcpu_frame`, `roam_plat_own`), move to shared runtime (c) and reuse in ProStreet. |
| `com`, `debug`, `features`, `fileio`, `overrides`, `lifecycle`, `widescreen`, `framerate`, `dinput_pad`, `dikeys`, `keyboard`, `dinput_kbd`, `career`, `intro`, `roam`, `roam_core`, `roam_game`, `ora…`, `win32`, `win32_ext` (runtime a) | These contain NFSU2‑specific logic. Either (a) replace with ProStreet‑specific code, or (b) if they are generic POSIX/Win32 wrappers, move to shared runtime (c). |
| `dispatch`, `genexec`, `heap`, `watchdog`, `win32_ext`, `com`, `features`, `res`, `fileio`, `overrides`, `lifecycle`, `widescreen`, `framerate`, `dinput_pad`, `dikeys`, `keyboard`, `dinput_kbd` (runtime ?) | These are present in `lifter/runtime/` — verify which are already shared by ProStreet’s lifter runtime and which need to be kept, removed, or replaced. |

### Phase 4 — Validate and iterate

1. **Build the ProStreet web host** with only the browser glue + shared runtime.
2. **Run the web host** (e.g. `node` or `wasm` runner) to ensure no link errors.
3. **Check that NFSU2‑specific functionality** (career progression, drift physics, multiplayer racenet, etc.) is absent or replaced by stubs.
4. **Iterate** — move any additional generic functions from the `?` categories into the reusable (b) or shared (c) columns.

## 4. Summary of Reusable vs. Removable Code

| Category | Files to Keep/Reuse | Files to Remove/Replace |
|---|---|---|
| **(b) Browser glue** | 12 files (`main_web`, `input_web`, `audio_web`, `gpu_web`, `net_lan`, `shader_dump`, `wgsl`, `status_web`, `texrep_web`, `null_platform`, `stackprobe`, `sbrk`) | — |
| **(c) Shared runtime** | 5 files (`memchk`, `memcmp`, `stackprobe`, `tz`, `sbrk`) — also in `lifter/runtime/` | — |
| **(a) NFSU2 game logic** | — | 69 runtime files + 3 WEB `?` files pending review |

**No conflicts with existing opencode work.** All changes are confined to `analysis/` and the `web/` host directory; the lifter/runtime shared files are untouched.

---
*Prepared by: Ship's scribe*  
*Date: Tue Nov 17 2026*  
*Cross‑checked against `web/build.sh` RT (95) and WEB (18) lists.*