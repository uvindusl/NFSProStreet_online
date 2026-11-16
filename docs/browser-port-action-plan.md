# Browser‑Port Action Plan — ProStreet → WebGPU (M0‑M4)

**Purpose:** Drive Need for Speed: ProStreet static recompilation to a browser‑playable WebGPU build as fast as possible. Synthesizes all prior docs, analysis, and pipeline knowledge.

**Current phase:** M0 (foundation) → M1 (boot) → M2 (lift) → M3 (runtime) → M4 (first frame). See phase table in `README.md`.

---

## 1. Current blockers (verified)

| Blocker | Evidence | Owner | Effort |
|---|---|---|---|
| **Native boot — CRT constructors / clean `.data`** | `out/boot-live.log`: stops inside `_initterm_e` at `0x00435390` on stale allocator pointer `0xb4dd18 -> 0x2aed0020`. `out/boot-overlay.log` stops at unresolved indirect call `0x0096aad4` when overlaying clean `.data`. Minimal patch: write `0x0096aae8` at VA `0x00aae110` (one dword). `analysis/securom-data-patch-map.md` classifies this as a Class d patch. | opencode (lifter/runtime/win32.c, lifter/tools/) | 1‑2 days |
| **Native boot — pool allocator / heap brk** | Live dump has `heap_brk=0x200006e0`; guest reads stale pointer from `.data`. The `gh_alloc` bump‑arena in `lifter/runtime/win32.c` must not exhaust before the game's pool allocator runs. | opencode | 2‑3 days |
| **Web build / gen gaps** | `web/gen.sh` lifts the shader/parameter chain; `web/build.sh` compiles to WASM. No D3DX HLE → `Direct3DCreate9` returns 0 (see `docs/d3dx-coverage.md`). No `IDirect3D9` device creation → no rendered frame. | pi (web/) | 3‑5 days |
| **D3D9 + D3DX HLE** | `docs/d3dx-coverage.md`: all 14 `D3DX*` imports stubbed (`r = 0`). `Direct3DCreate9` returns 0. Reference `nfsu2-web` has full HLE in `reference/nfsu2-web/runtime/d3dx_hle.c` and `reference/nfsu2-web/runtime/com.c` (COMSTAT ~36k device calls/frame). | pi (web/) / opencode (lifter/) | 5‑7 days |
| **Import coverage completeness** | `docs/import-coverage.md`: 365 imports across 15 DLLs; `msvcr80` 156/156 done, `kernel32` 89/89 done, `d3dx9_34` 0/14. Gaps ranked by impact. | opencode | Ongoing |

---

## 2. Ordered next actions (file owners)

### M0 → M1: Native boot to first CRT constructor completion
| # | Action | File(s) | Owner |
|---|---|---|---|
| 1 | Apply SecuROM data‑patch: write `0x0096aae8` at VA `0x00aae110` (one dword in `.data`). Re‑verify with `scripts/verify_target.py` and `./out/prostreet`. | `analysis/securom-data-patch-map.md`, `lifter/runtime/win32.c`, `scripts/verify_target.py` | opencode |
| 2 | Fix pool allocator / heap brk exhaustion: ensure `gh_alloc` has enough headroom before the game's pool allocator runs. Trace from `out/boot-live.log` last events before crash. | `lifter/runtime/win32.c` (`gh_alloc`, `g_brk`, `HEAP_END`) | opencode |
| 3 | Bring boot to `SleepEx` loop (currently crashes at `_initterm_e`). Verify `./out/prostreet artifacts/nfs.exe.unpacked` runs past CRT, creates thread, enters SleepEx loop. | `out/boot-live.log`, `scripts/run_boot.sh` | opencode |

**Definition of Done for M1:** `./out/prostreet` brings up CRT, creates guest thread, enters `SleepEx` loop, no SIGSEGV. All `.data` patches applied; verify with `python3 scripts/verify_target.py artifacts/nfs.exe.unpacked` passing all checks except expected sha256.

### M1 → M2: Lift remaining shards & win32 HLE
| # | Action | File(s) | Owner |
|---|---|---|---|
| 4 | Implement `Direct3DCreate9` returning a stub `IDirect3D9` pointer (non‑zero). | `lifter/runtime/win32.c` (add case before `D3DX` fallback) | opencode |
| 5 | Add minimal D3DX9 HLE stubs: `D3DXCreateEffectFromResourceA` → return fake effect pointer; `D3DXMatrixMultiply`, `D3DXVec3Transform` → identity/zero. | `lifter/runtime/win32.c` (add cases for `strncmp(n, "D3DX", 4) == 0`) | opencode |
| 6 | Verify `./out/prostreet` no longer crashes at `_initterm_e`; reaches `Direct3DCreate9` path. | `out/boot-live.log`, `out/boot-overlay.log` | opencode |

**Definition of Done for M2:** Lifted C shards compile; runtime links; `./out/prostreet` reaches past `Direct3DCreate9` and creates a D3D9 device stub.

### M2 → M3: Web gen/build to first WebGPU frame
| # | Action | File(s) | Owner |
|---|---|---|---|
| 7 | Port `Direct3DCreate9` stub to `web/gen.sh` / `web/build.sh` pipeline: return a valid `IDirect3D9` handle from the WASM module init. | `web/gen.sh`, `web/build.sh`, `web/runtime/` | pi |
| 8 | Port minimal D3DX9 HLE from `reference/nfsu2-web/runtime/d3dx_hle.c`: `BeginPass`, `CommitChanges`, `SetMatrix`, `SetVector`, `SetTexture`, `D3DXMatrixMultiply`, `D3DXVec3Transform`. | `web/runtime/d3dx_hle.c` (new file, mirror nfsu2-web structure) | pi |
| 9 | Add `IDirect3D9` method tracking (COMSTAT) to `web/runtime/com.c` (mirror nfsu2-web `com.c` counters). | `web/runtime/com.c` | pi |
| 10 | Run `web/gen.sh` → `out/` → build → exercise the first `BeginPass` → verify no “unimplemented” flood in stderr. | `web/gen.sh`, `web/build.sh`, `out/` | pi |

**Definition of Done for M3:** Web build runs in Chrome/Firefox WebGPU; a single `BeginPass` commit changes no errors beyond “unimplemented” stubs; stderr shows D3DX counters incrementing.

### M3 → M4: Game‑playable race frame
| # | Action | File(s) | Owner |
|---|---|---|---|
| 11 | Wire up `IDirect3DDevice9` present / swapchain to WebGPU `canvas` presentation. | `web/runtime/gfx.h`, `web/runtime/guest.h`, `web/runtime/cpu.h` | pi |
| 12 | Implement remaining import gaps (winsock, file I/O, input) minimal HLE so the race timer proceeds. | `lifter/runtime/win32.c` (winsock/fileio stubs), `scripts/` | opencode |
| 13 | End‑to-end: `./out/prostreet` → WebGPU canvas shows race frame (car, track, HUD). | All above | pi / opencode |

**Definition of Done for M4:** Browser tab displays a running race (car on track, HUD visible, no fatal crashes). `console` shows D3DX counters incrementing; `fps` > 1 for >30 frames.

---

## 3. Agent ownership matrix (hard rules)

| Agent | Allowed write paths | Rationale |
|---|---|---|
| **pi** | `docs/**`, `web/**` (gen/build/runtime), `docs/phase*.md` updates | Owns browser‑port pipeline; web/gen.sh/build.sh; D3DX HLE reference |
| **opencode** | `lifter/**` (win32.c, imargs.c, stubs), `scripts/`, `analysis/`, `docs/import*.md`, `docs/boot*.md` | Owns native lifter/runtime; boot verification; import coverage |
| **Claude** | `web/browser‑port` QA, shaders, WebGPU setup | Working on M0 browser pipeline (separate pane) |

**No‑write rule:** Never commit game DLLs, .exe, .bin, artifacts/, out/, gen/, deps/, reference/ assets. docs/ is the shared documentation staging area.

---

## 4. Concrete 'definition of done' per milestone

| Milestone | Success criteria | How measured |
|---|---|---|
| **M1** (native boot past CRT) | `./out/prostreet` runs: CRT constructors → thread create → `SleepEx` loop, no SIGSEGV. `verify_target.py` passes all layout checks. | Read `out/boot-live.log`; re‑run `python3 scripts/verify_target.py artifacts/nfs.exe.unpacked` |
| **M2** (lift + D3D9 stub) | `./out/prostreet` reaches `Direct3DCreate9` → stub device; no import‑unimplemented flood. Lifted shards compile with `-w`. | Check `out/boot-live.log` for "calling entry" → "SleepEx" transition; `git build` exit code 0 |
| **M3** (web gen/build first frame) | `web/build.sh` produces `out/prostreet.wasm`; Chrome loads page; WebGPU `initialize` resolves; `BeginPass` HLE stub called once (stderr log). | `web/gen.sh` stdout; `out/prostreet.wasm` exists; `chrome://gpu` shows WebGPU active |
| **M4** (race frame in browser) | Browser tab: car on track, HUD visible, no crashes >30 frames. stderr shows D3DX counters (BeginPass, CommitChanges, etc.) incrementing. | Manual observation + `console.log` counters; `out/prostreet` stderr replay |

---

## 5. Unknowns / risks

- **Exact import order** during boot (ProStreet init sequence not fully traced; may differ from NFSU2).
- **SecuROM‑decrypted `.rdata` vs clean overlay**: the `analysis/securom-data-patch-map.md` minimal patch (one dword at `0x00aae110`) is verified, but other `.data` differences exist (18,746 runs). Leaving them clean is actionable, but edge cases may appear later.
- **Pool allocator vs bump arena**: `gh_alloc` in `lifter/runtime/win32.c` may conflict with the game's custom pool allocator once D3DX HLE starts allocating textures/structs.
- **WebGPU adapter compatibility**: Dawn / WebGPU on test hardware may not support the required D3D9 feature level; may need fallback to D3D11/12 backend.
- **D3DX effect parameter evaluation**: the reference nfsu2-web evaluates preshaders and expression trees; ProStreet's exact expression format is unknown until the first `BeginPass` is reached.

---

## 6. Next immediate action

**Today:** opencode applies the SecuROM patch (one dword at `0x00aae110`) and fixes `gh_alloc` headroom; verify `./out/prostreet` passes `verify_target.py` and reaches the `SleepEx` loop. Then pi ports `Direct3DCreate9` stub + minimal D3DX9 HLE to `web/`. Follow-up: weekly sync via `herdr agent prompt w1F:p8` / `w1F:pC`.