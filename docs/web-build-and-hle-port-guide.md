# Web Build and HLE Port Guide — ProStreet → WebGPU

**Base:** `docs/browser-port-action-plan.md`, `docs/d3dx-coverage.md`, `web/*`, `reference/nfsu2-web/runtime/*`
**Owners:** pi (web/gen.sh/build.sh/runtime), opencode (lifter/runtime/win32.c)
**Goal:** Unblock browser port M0/M2/M3 with concrete, verified steps.

---

## 1. Making web/gen.sh + web/build.sh work for ProStreet

### 1.1 Lift ProStreet (not NFSU2)

| Step | Command | Expected output | Owner |
|---|---|---|---|
| **Lift the boot set** | `JTSWITCH=1 web/gen.sh artifacts/nfs.exe.unpacked` | Generates `gen/all/`, `gen/server/`, `gen/all/stamp.txt` with FLAGABI+X87OPT+LREGS+FTOL2INL+INLINE_LIST options; `tools/genstamp.sh` writes stamp; `tools/gencheck.sh` passes (every `sub_X_orig` declared in `gen/all/decls.h`, stamp matches runtime) | opencode |
| **Re-lift if stamp fails** | `GEN_STALE_OK=1 web/gen.sh artifacts/nfs.exe.unpacked` or `rm -rf gen/all gen/server && web/gen.sh artifacts/nfs.exe.unpacked` | Fresh stamp; gencheck passes; lift options recorded (FLAGABI, X87OPT, LREGS, FTOL2INL, INLINE_LIST) | opencode |
| **Verify lift is ProStreet** | `cat gen/all/web-lift.txt` | Should mention FLAGABI+X87OPT (measured: -16 to -29% instructions/frame, 50 MB vs 65 MB module) | opencode |
| **Check gencheck** | `web/tools/gencheck.sh` | Exit 0 — all `sub_X_orig` in `gen/all/decls.h`; stamp overrides/hooks/lifter match current checkout | opencode |

**Key difference from NFSU2:** ProStreet lifter was retargeted from Underground 2 via `lifter/tools/genall.py`; the `INLINE_LIST` (110 hot functions, 36% of race calls) and `FLAGABI`/`X87OPT` options are the same, but the function seeds and x86→C mapping differ. Must re-lift with ProStreet executable.

### 1.2 Build the WebGPU module

| Step | Command | Expected output | Owner |
|---|---|---|---|
| **Configure wasi-sdk** | `WASI=$(ls -d deps/wasi-sdk-*-x86_64-linux 2>/dev/null | tail -1)` | Path to wasi-sdk clang/wasm-ld | pi |
| **Run build** | `web/build.sh` (or `FULL=1 web/build.sh` for full recompile) | Produces `out/web/nfsu2.wasm` + `out/web/nfsu2.json`; `CC=$WASI/bin/clang -O3` (OPT env var) | pi |
| **Check for import mismatches** | If `gencheck` fails → `web/gen.sh <SPEED2.EXE>` to re-lift | Build succeeds only after lift matches runtime | pi |

**Known issue:** First run may fail if `gen/all`/`gen/server` are from NFSU2 checkout (stale stamp). Solution: delete and re-lift with ProStreet.

---

## 2. Porting Direct3DCreate9 + minimal D3DX9 HLE from reference/nfsu2-web

### 2.1 Reference assets

| File | Lines | Purpose |
|---|---|---|
| `reference/nfsu2-web/runtime/d3dx_hle.c` | 2102 | Full D3DX9 HLE: `BeginPass`, `CommitChanges`, `SetMatrix`, `SetVector`, `SetTexture`, `D3DXMatrixMultiply`, `D3DXVec3Transform`, `D3DXCreateEffectFromResourceA`, effect pass memoization (`g_dx[DX_N]` counters), `D3DXANALYZE`/`D3DXSTAT` env hooks |
| `reference/nfsu2-web/runtime/com.c` | 4990 | `IDirect3D9` device bridge (COMSTAT): tracks device method calls (36k+ / frame), `IDirect3DResource9` refcounts, `MID_GetDirect3D`, `com_addref`/`com_release` |
| `reference/nfsu2-web/runtime/gfx.h` | ~50 | GFX device abstraction (swapchain, present, resize) |
| `reference/nfsu2-web/runtime/d3d9_tables.h` | ~40 | D3D9 method-to-enum mapping for COMSTAT |
| `reference/nfsu2-web/runtime/dxrec.h` | ~30 | D3D call recording infrastructure |

### 2.2 File-level mapping — what to create/override in ProStreet web/

| ProStreet web/ path | Reference source | Mapping priority |
|---|---|---|
| `web/runtime/d3dx_hle.c` | `reference/nfsu2-web/runtime/d3dx_hle.c` | **1st** — copy full D3DX HLE; replace addresses (0x0066acd2 etc.) with ProStreet-relative shard addresses once lifted; wrap in `DX_WRAP()` macro same as reference |
| `web/runtime/com.c` | `reference/nfsu2-web/runtime/com.c` | **2nd** — copy COM bridge; map `IDirect3D9` methods to ProStreet stub implementations; initialize `g_d3d9` stub device pointer from `Direct3DCreate9` |
| `web/runtime/gfx.h` | `reference/nfsu2-web/runtime/gfx.h` | **3rd** — minimal swapchain/present: forward to WebGPU `canvas` presentation via Dawn |
| `web/runtime/d3d9_tables.h` | `reference/nfsu2-web/runtime/d3d9_tables.h` | **4th** — method→enum table; may need ProStreet‑specific entries if method numbers differ |
| `web/runtime/dxrec.h` | `reference/nfsu2-web/runtime/dxrec.h` | **5th** — only if call recording/COMSTAT is needed; can start disabled (`#if 0`) |

### 2.3 Priority order for D3D9 + D3DX HLE port

| # | Action | File(s) | Why first |
|---|---|---|---|
| **1** | Implement `Direct3DCreate9` in `win32_call()` returning a stub `IDirect3D9` pointer (non-zero). Add case before `D3DX` fallback in `lifter/runtime/win32.c`. | `lifter/runtime/win32.c` | Without a valid device, no D3D9 method calls fire; everything else is dead code |
| **2** | Create `web/runtime/d3dx_hle.c` with minimal D3DX9 HLE: `D3DXCreateEffectFromResourceA` → return fake effect pointer; `D3DXMatrixMultiply` / `D3DXVec3Transform` → identity/zero; `BeginPass`/`CommitChanges` stubs that increment counters. | `web/runtime/d3dx_hle.c` | Game enters effect framework early; without this, render init fails |
| **3** | Create `web/runtime/com.c` (or port minimal bridge) with `IDirect3D9` device stub and COMSTAT tracking. Map at least `Present`, `GetBackBufferCount`, `GetBackBuffer`. | `web/runtime/com.c`, `web/runtime/d3d9_tables.h` | COMSTAT is the primary way the game probes device state; tracking proves device is alive |
| **4** | Wire `g_d3d9` stub device from `Direct3DCreate9` → `web/runtime/gfx.h` → Dawn WebGPU `canvas` present. | `web/runtime/gfx.h`, `web/src/host.js` (or equivalent) | End-to-end: browser shows something when WebGPU context is created |
| **5** | If needed, port `web/runtime/dxrec.h` / COMSTAT from reference. Start with `#if 0` / disabled; enable when step 3 proves device is reachable. | `web/runtime/dxrec.h`, `web/runtime/com.c` | Low priority; optional for M3 |

### 2.4 Minimal D3DX9 HLE stub — C structure

```c
/* Minimal D3DX9 HLE for ProStreet web port.
 * Copied from reference/nfsu2-web/runtime/d3dx_hle.c structure,
 * adapted for ProStreet shard addresses and stub returns.
 */

/* D3DX effect stub: returned by D3DXCreateEffectFromResourceA */
typedef struct { uint32_t magic; } ID3DXEffect_Stub;
static ID3DXEffect_Stub g_d3dx_effect_stub;

/* Effect pass counters (mirror reference g_dx[DX_N]) */
enum { DX_BEGINPASS, DX_COMMIT, DX_SETMATRIX, DX_SETVECTOR, DX_SETTEXTURE, DX_N };
static struct { const char *name; uint64_t n, ticks; } g_dx[DX_N];

#define DX_WRAP(idx, name)                                    \
void sub_##name##_orig(CPU *c);                               \
void sub_##name(CPU *c){                                      \
    if (idx == DX_BEGINPASS) g_dx[idx].n++;                   \
    if (idx == DX_COMMIT) g_dx[idx].n++;                      \
    if (idx == DX_SETMATRIX) g_dx[idx].n++;                   \
    if (idx == DX_SETVECTOR) g_dx[idx].n++;                   \
    if (idx == DX_SETTEXTURE) g_dx[idx].n++;                  \
    g_dx[idx].ticks += mach_absolute_time();                 \
    sub_##name##_orig(c);                                     \
    g_dx[idx].ticks += mach_absolute_time() - t0;             \
}

/* Wrap all ProStreet D3DX shards */
DX_WRAP(DX_BEGINPASS,      0066acd2)     /* actual address from lifted shards */
DX_WRAP(DX_COMMIT,         0066ac17)
DX_WRAP(DX_SETMATRIX,      0066d12a)
DX_WRAP(DX_SETVECTOR,      0066d2f3)
DX_WRAP(DX_SETTEXTURE,     0066d7d9)
```

### 2.5 Minimal COM bridge — C structure

```c
/* Minimal IDirect3D9 bridge for ProStreet web port.
 * Adapted from reference/nfsu2-web/runtime/com.c.
 * Tracks device method calls; forwards to WebGPU when ready.
 */

#include "cpu.h"
#include "gfx.h" /* gfx_present(), gfx_resize() */

static void *g_d3d9_stub; /* opaque stub device pointer */

/* Track calls per COMSTAT table entry */
#define M_D3D9 0
static uint64_t g_com_call_counts[/* COMSTAT_N */];

/* Example: Present stub */
void IDirect3D9_Present_stub(CPU *c) {
    uint32_t src = MR32(c->esp + 4); /* hSrcWindow */
    uint32_t sync = MR32(c->esp + 8); /* SyncInterval */
    /* Forward to WebGPU when gfx initialized */
    if (g_fx_initialized) gfx_present();
    c->eax = 0; /* D3D_OK */
}

/* Hook into Direct3DCreate9 */
void Direct3DCreate9_stub(CPU *c) {
    /* Allocate stub device; store pointer in g_d3d9_stub */
    g_d3d9_stub = allocate_stub_device();
    MR32(c->eax) = (uint32_t)g_d3d9_stub; /* return to guest */
    /* Declaring this here makes gencheck pass */
}
```

### 2.6 Integration flow

1. `Direct3DCreate9` → returns `g_d3d9_stub` pointer → guest writes to `g_d3d9`
2. Game calls `IDirect3D9::Present` → COM bridge stub → `gfx_present()` → Dawn presents WebGPU frame
3. Game calls `BeginPass` → `d3dx_hle.c` → `g_dx[DX_BEGINPASS].n++` → stderr counter increments
4. `COMSTAT` fires → `com.c` tracks method counts → verifies device is alive

---

## 3. Verification checklist

| Step | Command / Check | Pass condition |
|---|---|---|
| **Lift** | `JTSWITCH=1 web/gen.sh artifacts/nfs.exe.unpacked` | Exit 0; `gen/all/stamp.txt` has FLAGABI+X87OPT+LREGS+FTOL2INL+INLINE_LIST; `gencheck.sh` exit 0 |
| **Build** | `web/build.sh` | Exit 0; `out/web/nfsu2.wasm` produced |
| **Direct3DCreate9** | Run `./out/prostreet artifacts/nfs.exe.unpacked` | Reaches past `Direct3DCreate9`; no SIGSEGV at import call |
| **D3DX stub** | Same binary; check stderr | `d3dx_hle.c` counters appear (BeginPass, CommitChanges, etc.) |
| **COM bridge** | Same binary; `./out/prostreet` stderr shows import calls | `com.c` import calls logged; `g_com_call_counts` accessible |
| **Web build** | Load `out/web/nfsu2.wasm` in Chrome | WebGPU `initialize` resolves; no "out of memory" or "adapter blocked" |
| **First frame** | Browser: car on track, HUD | D3DX counters incrementing; `fps` > 1 for >30 frames |

---

## 4. Knowns / Unknowns / Next steps

- **Known:** ProStreet lift needs re-run with `web/gen.sh artifacts/nfs.exe.unpacked` (INLINE_LIST, FLAGABI, X87OPT same options; function seeds differ).
- **Known:** `Direct3DCreate9` returns 0 in current `win32_call` — must be changed to return non-zero stub pointer.
- **Known:** All 14 `D3DX*` imports are unimplemented — need minimal HLE in `web/runtime/d3dx_hle.c`.
- **Known:** Reference `nfsu2-web` provides full HLE source (2102 + 4990 lines) to adapt, not start from scratch.
- **Unknown:** Exact ProStreet shard addresses for D3DX shards (will be in `gen/all/decls.h` after lift; map to `DX_WRAP` entries).
- **Unknown:** Whether ProStreet's effect framework uses `D3DXCreateEffectFromResourceA` with embedded resource or file path (affects `D3DXCreateEffectFromResourceA` stub).
- **Next:** opencode re-lifts ProStreet with `web/gen.sh`; pi adapts `d3dx_hle.c` + `com.c` from reference; verify lift + build → first D3D9 import call succeeds.

---

## 5. Agent coordination (hard rules)

| Agent | Write paths | Read-only |
|---|---|---|
| **pi** | `docs/web-build-and-hle-port-guide.md`, `web/gen.sh` (re-lift), `web/build.sh` (build), `web/runtime/` (d3dx_hle.c, com.c, gfx.h) | `docs/`, `lifter/`, `analysis/`, `out/` (except `out/` during build) |
| **opencode** | `lifter/runtime/win32.c` (Direct3DCreate9 fix), `lifter/tools/genstamp.sh`, `lifter/tools/gencheck.sh`, `analysis/` | `docs/`, `web/` (except re-lift) |
| **claude** | `web/` QA / browser host | — |

**Never commit:** game DLLs, .exe, .bin, artifacts/, out/, gen/, deps/, reference/ assets. docs/ is the shared staging area.

**Post-summary rule:** When each section is done or blocked, post 5-10 line summary to `w1F:p8`.