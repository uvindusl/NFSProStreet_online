# Working in parallel: native runtime and wasm port

Two people now work on this at the same time: one keeps improving the native runtime (lifter, boot repairs, game fixes, audio), the other ports the game to the browser (wasm + WebGPU). This page says who owns what, where the seam between the two goes, and how changes flow, so that neither blocks the other.

## Where things stand

- **Native** (`lifter/runtime/`, built by `scripts/build_runtime.sh`): the game boots from the process dump, plays the intro movies, goes through the title, the first-run Alias flow and the main menu, and plays a Race Day race. Scripted runs (`PROSTREET_SCRIPT`) and offline frame dumps (`PROSTREET_FRAMEDUMP`) are described in [`native-runs.md`](native-runs.md). Not done: EA's AEMS audio modules (calls into them are skipped).
- **Web** (`web/`, built by `web/build.sh`): it compiles a separate, older runtime (`web/runtime/web/`, inherited from nfsu2-web) and renders a WebGPU triangle; the shader translation passes the whole shader corpus (F6 in [`milestones.md`](milestones.md)). It does not contain any of the native runtime's work: the boot repairs (`boot.c`), the Win32 stand-ins (`win32.c`), D3D9 and the effect system (`com.c`, `d3dx_effect.c`), scripted input (`autotest.c`).

So the wasm port should **build `lifter/runtime` for wasm** and give it web backends, instead of porting the nfsu2-web reference HLE into `web/` (the plan in [`web-build-and-hle-port-guide.md`](web-build-and-hle-port-guide.md) predates the native runtime and would duplicate it).

## Who owns what

| Area | Owner | Files |
|---|---|---|
| Lifter and lifted code: instruction semantics, seeds, hook sites | native | `lifter/tools/`, `scripts/lift_boot.sh`, `scripts/boot_seeds.py` |
| Game-level fixes: boot repairs, game fixes, HD fonts, effects, scripted input, AEMS audio | native | `boot.c`, `gamefixes.c`, `hdfont.c`, `d3dx_effect.c`, `autotest.c`, `aems.c` (new) |
| Platform layer, native | native | `platform_posix.c` (to be split out, see below) |
| Platform layer, web | wasm | `platform_web.c` (new), WebGPU backend for the D3D9 calls |
| Browser host: page, JS/TS, WebGPU device, OPFS, audio and input glue | wasm | `web/` |
| Shared, change with care | both | `mem.c`, `win32.c`, `com.c`, `dispatch.c`, `cpu.h`, `scripts/build_runtime.sh` |

The lift is shared input: both sides must run the same commit's `scripts/lift_boot.sh` on the same `artifacts/nfs.exe.unpacked`, because the boot repairs refer to exact addresses in that dump. Changing seeds or hook sites (`HOOK_SITE`, `HOOK_RETURN_SITE`) needs a re-lift on both sides; say so in the PR.

## The seam: a platform layer

Today the native-only calls are spread over the shared files:

| What | Where |
|---|---|
| Guest address space: `mmap`, `mprotect`, the SIGSEGV handler that commits pages and reports faults | `mem.c` |
| Threads: `pthread_create` | `win32.c` (CreateThread), `boot.c` |
| Time and sleep: `clock_gettime`, `nanosleep` | `win32.c`, `com.c`, `dispatch.c` |
| Files: `fopen` and friends | `win32.c` (game files and saves), `boot.c` (header image, dump), `mem.c`, `com.c`, `swrast.c`, `d3d9_hle.c` (logs, dumps) |
| Graphics output | none yet: `com.c` keeps D3D9 state, `swrast.c` renders a frame offline |
| Sound output | none yet: DirectSound buffers in `com.c` are never played |
| Input | `com.c` (DirectInput `GetDeviceState`) reads keys from `autotest.c` / `PROSTREET_KEYS` |

The first PR of the port moves these behind one header, `lifter/runtime/platform.h`, with **no change in native behaviour**:

- memory: reserve the 4 GB guest space, commit a range, tell whether an address is committed (wasm has no page faults: commit has to be explicit and the fault handler becomes a bounds check);
- files: open, read, write, seek, close, find-first/next, with the game directory and `savedata/` mapped by the host (OPFS or HTTP in the browser);
- time: a monotonic clock and a sleep;
- threads and locks: create a thread, mutex, event (wasm: pthreads on SharedArrayBuffer, which needs COOP/COEP headers from the server);
- frame: present the back buffer (native: nothing or the software rasteriser; web: WebGPU);
- input: poll keyboard and mouse state into the DirectInput buffers;
- audio: queue PCM for a sound buffer (native: nothing yet; web: Web Audio).

`platform_posix.c` keeps today's code; `platform_web.c` implements the same calls for the browser. After that PR, the wasm side mostly edits `platform_web.c` and `web/`, the native side mostly edits game logic, and the shared files change rarely.

## Branches and pull requests

- Each person works in their own fork, on short branches, and opens PRs against `uvindusl/NFSProStreet_online` `main`.
- Merge `main` into your branch at least once a day; keep each PR to one feature or fix.
- A PR that changes a shared file says so in its description, and the other person reviews it.
- Never commit the game image, the unpacked dump, lifted output or anything derived from them (`out/`, `gen/`, `savedata/` are git-ignored).

## A common check

The native scripted runs are the reference. `PROSTREET_SCRIPT` with `PROSTREET_FRAMEDUMP` is deterministic, so the wasm build should take the same script and reach the same states:

| Check | Native evidence |
|---|---|
| Title screen | `PROSTREET_FRAMEDUMP=6700`: the logo and "CLICK to continue" |
| Main menu from a saved profile | `script: frame N: FeMainMenu.fng has control`, N about 5840 |
| First-run Alias flow | the first-run script in `native-runs.md` reaches `FeMainMenu.fng` (frame 12462) |
| Race frame | the Race Day script with `PROSTREET_FRAMEDUMP=999999`: `swrast: frame N: 986 draws ...` |

The unit tests need no game files and should run on both builds: `tests/build_runtime_lifecycle_test.sh`, `tests/build_d3dx_test.sh`. A CI job that builds the runtime and runs them would catch breakage from either side.

## Suggested order

**wasm**
1. The platform seam above (native behaviour unchanged; both test scripts pass).
2. `lifter/runtime` compiled with the wasm toolchain (`deps/` wasi-sdk, as `web/build.sh` uses), graphics off, booting to the first `Present`; compare its log with the native log.
3. Files from the server or OPFS; then input from browser key events into DirectInput.
4. WebGPU behind the D3D9 calls, using the existing shader translation; the title screen at frame 6700 is the first target.

**native**
1. AEMS audio: lift the sound library's module banks at a fixed base, so audio runs as lifted code instead of being skipped.
2. Sound output through the platform layer (gives the Web Audio side something to play).
3. Driving: steering, finishing a lap, the remaining game-state repairs.
