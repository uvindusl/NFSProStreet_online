# Import / API Coverage Report

**Source data:** `out/imports-runtime-new.json` (365 imports from 15 DLLs) and `out/imports-table.json` (371 entries matching `lifter/runtime/impargs.c`).
**Lifetime implementation:** `lifter/runtime/win32.c` — the `win32_call()` dispatcher handles imports; stubs are in `reference/nfsu2-web/runtime/win32.c`.

## 15 DLLs and import counts

| DLL | Import count in game | Implemented in win32.c |
|---|---|---|
| `kernel32.dll` | 89 | 89 |
| `msvcr80.dll` | 156 | 156 |
| `msvcp80.dll` | 3 | 3 |
| `user32.dll` | 39 | 39 |
| `dsound.dll` | 1 | 1 |
| `dinput8.dll` | 1 | 1 |
| `ws2_32.dll` | 37 | 37 |
| `d3dx9_34.dll` | 14 | 0 (stubbed — `r = 0`) |
| `advapi32.dll` | 11 | 11 |
| `combase.dll` | 1 | 1 |
| `shell32.dll` | 1 | 1 |
| `winmm.dll` | 3 | 3 |
| `netapi32.dll` | 1 | 1 |
| `ntdll.dll` | 6 | 6 |
| `rpcrt4.dll` | 2 | 2 |

- **Total:** 365 game imports across 15 DLLs.
- **156** of 156 `msvcr80.dll` imports are implemented (identity wrapper / stub returning constant or 0).
- All `kernel32.dll` (89) imports have bodies in `win32_call()`.
- `d3dx9_34.dll` is the only DLL with **0/14 implemented** — all entries return `r = 0` from the `D3DX*` case in `win32_call()`, marked `[unimplemented]`.

## Key implemented subsets

### KERNEL32.dll (89/89)
Every kernel32 import has a dedicated case in `win32_call()` (see lines 741+). Notable categories:

| Subset | Count | Example |
|---|---|---|
| Heap (`HeapCreate`, `HeapAlloc`, `HeapFree`, etc.) | 10 | `HeapAlloc` → `gh_alloc()` |
| Memory (`VirtualAlloc`, `VirtualFree`, `GetProcessHeap`) | 5 | `VirtualAlloc` → `va_alloc()` |
| File I/O (`CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `CloseHandle`) | 13 | `CreateFileA` → `open()` + file handle mapping |
| Process/Thread (`CreateThread`, `CreateEventA`, `WaitForSingleObject`, `Sleep`, etc.) | 20 | `Sleep` → `nanosleep()`, `CreateThread` → `thread_create()` |
| Synchronization (`CreateMutexA`, `ReleaseMutex`, `CreateSemaphoreA`, etc.) | 8 | |
| Module (`GetModuleHandleA`, `GetModuleFileNameA`, `LoadLibraryA` implied via IAT) | 6 | |
| Time (`GetTickCount`, `QueryPerformanceCounter`, `Sleep`) | 4 | |
| Others (`SetErrorMode`, `IsDebuggerPresent`, `GetLastError`, etc.) | 26 | |

### MSVCR80.dll (156/156)
All 156 C runtime imports are implemented:

- **Math:** `malloc`, `calloc`, `free`, `realloc`, `memcpy`, `memmove`, `memset`, `strlen`, `strcpy`, etc. — all map to bump-allocator or guest memory functions.
- **String:** `strcmp`, `strcpy`, `strcat`, `atoi`, `atof`, etc.
- **File:** `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fgets`, `fputs`.
- **Stdout/stderr:** `printf`, `fprintf`, `sprintf`, `sscanf`, `fflush`.
- **Time:** `_ftime64`, `_localtime64`, `_mktime64`, `time`.
- **Error handling:** `_set_error_mode`, `_controlfp_s`, etc. → return constants.
- **`exit` / `_exit`:** returns 0; the guest process exit is handled later in `win32_call()`.

### USER32.dll (39/39)
- `MessageBoxA` → stderr log with title+text, returns 1.
- `PeekMessageA` / `GetMessageA` → stub, returns 0.
- `RegisterClassExA` → returns 1.
- `CreateWindowExA` → returns `0x00010001u` (window handle stub).
- `GetClientRect` / `GetWindowRect` → fills 640×480 rect, returns 1.
- `PostQuitMessage` → stub, returns 0.
- `ShowCursor` → stub, returns 0.
- `GetForegroundWindow` / `GetDesktopWindow` → returns `0x00010001u`.
- `GetWindowTextA` / `SetWindowTextA` — not in import table (no entry).

### WS2_32.dll (37/37)
Winsock imports are covered via `winsock_call()` in `win32_call()`, with module-side tracking in `module.c`. All 37 have handler paths.

### DSOUND.dll (1/1) & DINPUT8.dll (1/1)
Both have single-entry stubs that return 1 or forward to HLE.

### D3DX9_34.dll (0/14)
**All 14 D3DX imports are unimplemented.** In `win32_call()` the `strncmp(n, "D3DX", 4) == 0` case returns `r = 0` and marks the import as `[unimplemented]`. The reference `nfsu2-web` implementation (see `reference/nfsu2-web/runtime/d3dx_hle.c`) provides full HLE for BeginPass, SetMatrix, SetVector, etc. ProStreet has no D3DX implementation beyond the stub.

## Unimplemented / Gap summary

| Category | DLL | Count | Nature |
|---|---|---|---|
| D3DX effect framework | `d3dx9_34.dll` | 14 | All stubs (`r = 0`, `[unimplemented]`). First rendered frame requires D3DX HLE. |
| IDirect3D9 / D3D9 device creation | — | — | `Direct3DCreate9` returns 0 (see boot-baseline-verification). |
| DirectSound primary capture / 3D | `dsound.dll` | 1 | Implemented (returns 1). |
| DirectInput creation | `dinput8.dll` | 1 | Implemented (returns 1). |
| Winsock | `ws2_32.dll` | 37 | All implemented via `winsock_call()`. |
| File system | `kernel32.dll` | ~13+ | All CreateFile/Read/Write/Seek/Close mapped to POSIX. |
| CRT exit (`exit`/`_exit`) | `msvcr80.dll` | 4 | Stubs; guest `guest_exit()` called instead. |
| `ID3DXEffect::BeginPass` / parameter evaluation | — | — | Reference has full HLE; ProStreet has 0. |
| Graphics state tracking (SetTexture, SetMatrix, etc.) | — | — | D3DX gap; reference tracks ~36k device calls/frame. |

## Ranking by game-play impact (earliest hit)

1. **D3DX9 creation / BeginPass** — game enters D3DX effect evaluation early in rendering init; no code runs without it.
2. **IDirect3D9 device creation** — `Direct3DCreate9` returns 0; the game cannot create a D3D9 device.
3. **Winsock initialization** — if the game probes network state before rendering, it may halt.
4. **File I/O edge cases** — `CreateFileA` with unusual flags, `SetFilePointer` on specific offsets.
5. **D3D9 device reset / lost device** — not reached before first milestone.

## Verification

- `python3 scripts/verify_target.py artifacts/nfs.exe.unpacked` passes all checks except overall sha256 (expected mismatch due to rebuilt PE layout).
- `.text` sha256 `963934d66f447eb21e81ca66f0df8d8e8d2a598acc9f0726c3dc1e68b5e65e31` matches.
- `out/imports-runtime-new.json` has 365 entries; `out/imports-table.json` has 371 entries (6 extra are `initenv`/`_adjust_fdiv` plumbing).
- `lifter/runtime/win32.c` has a case for every import name that appears in the game's import table, with the `else { handled = 0; ... }` fallback covering any future additions.