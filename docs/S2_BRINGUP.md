# S2 — headless CPU bring-up

Goal: the lifted `nfsps.exe` runs on a host CPU, headless, long enough to see
the CRT initialise, threads start and the game ask for a window and a
Direct3D 9 device. No rendering, no audio, no input: every Win32/D3D entry
point is a stub that logs and returns.

This is the first stage where the lift is *executed*, so it is also the real
test of the generated C.

## What the entry point actually is

`AddressOfEntryPoint` is `0x428c25` → VA `0x00828c25` (ImageBase `0x400000`).
It is stock MSVC 2005 `_WinMainCRTStartup`, not a SecuROM wrapper:

```
00828c25  call 0x00829163      ; __security_init_cookie
00828c2a  jmp  0x008289cd      ; __tmainCRTStartup
```

`__tmainCRTStartup` installs SEH through `fs:[0]` and reads the PEB through
`fs:[0x18]`, then runs `_ioinit`/`_initterm` and calls `WinMain`. The DRM
sections (`ars`, `est`, `artem`, `celare`, `.securom`) are never entered.

Consequences for the runtime:

* `fs` must be set up: `fs:[0x18]` = TEB pointer, `fs:[0]` = SEH chain, and
  the TEB's `+0x04` (stack base) / `+0x08` (stack limit) are used before
  anything else.
* `VirtualAlloc`, `GetCommandLineA`, `GetModuleHandleA`, `GetVersion`,
  `HeapCreate`, `GetStartupInfoA`, `QueryPerformanceCounter` are needed
  before the game's own code even starts.

## Memory

| Guest range | Use |
| --- | --- |
| `0x00400000` + `SizeOfImage` | the image's sections, mapped at the preferred base (no relocations exist) |
| `0x00100000`–`0x00400000` | header/zerofill slack, keep unmapped |
| above the image to `0x10000000` | `VirtualAlloc` arena + guest heap |
| top of the low 2 GB | guest threads' stacks |

On wasm, guest address == linear-memory offset and the runtime lives above
`GUEST_END` (nfsu2-web uses `0x28000000`; ProStreet's image ends at
`0x01F687E2`, so the same split works). On a native host, `mmap` the whole
4 GiB as `PROT_NONE` and commit regions as the guest allocates, so a bad guest
pointer is a diagnosable SIGSEGV instead of host corruption.

`ReadProcessMemory`-style self-inspection and `VirtualQuery` need the same
region table.

## Imports

The exe imports **371 names from 14 DLLs** (see `docs/BINARY_REPORT.md`).
The guest IAT slots must contain 32-bit values that route back into C:

```
IAT slot = 0xF0000000 | index          (nfsu2-web's IMP_MAGIC scheme)
```

`dispatch_indirect()` sees the magic value and calls `imp_call(index)`, which

1. reads the arguments from the guest stack (`[esp+4]`, …),
2. calls the C implementation (or the logging stub),
3. writes the result to `c->eax` (and `edx:eax` for 64-bit returns),
4. for **stdcall** imports pops the arguments, for **cdecl** leaves them —
   lifted call sites already emit the matching `add esp, N`.

Getting that split wrong corrupts the guest stack immediately, so the
calling convention is part of the import table, not a guess at runtime.

### Calling conventions

| DLL | Convention |
| --- | --- |
| KERNEL32, USER32, ADVAPI32, ole32, WINMM, WS2_32, NETAPI32, DINPUT8, DSOUND, d3d9 | stdcall |
| MSVCR80, MSVCP80 | cdecl (with a few `_stdcall` exceptions such as `_except_handler4_common`) |
| `d3dx9_34` | stdcall |

Argument counts come from `tools/argcount.py` (nfsu2-web's analyser: it
disassembles every call site, counts the pushes, and cross-checks the
disagreements), not from the export tables — the import directory does not
carry arity.

### MSVC 2005 runtime

MSVCR80 is the biggest new surface (156 imports): `malloc/free/realloc`,
`_initterm`, `_iob`, `fopen/fread/fwrite/fseek`, `sprintf` family,
`_beginthreadex`, `_ctime64`, `setlocale`, and the security cookie. MSVCP80
adds three mangled C++ entry points reached from exception handling
(`__CxxFrameHandler3`, `_CxxThrowException`, `__RTDynamicCast`), which the
game's C++ objects need as soon as a `std::string`-using constructor runs.

Implement them at the ABI level:

* allocation from the same guest heap as `HeapAlloc`, so pointers the game
  mixes between the two stay valid;
* `FILE *` is an index into a guest-addressable table; `_iob` is a real guest
  array of 20 `FILE` structs;
* printf-family formatting in C on the host, writing into guest memory.

## File layer

The game opens its data through both the CRT (`fopen`) and Win32
(`CreateFileA`). Both funnel into one file layer keyed by the guest path:

* case-insensitive lookup (the game was built for Windows),
* `GLOBAL/`, `TRACKS/`, `CARS/`, `FRONTEND/`, `SOUND/`, `MOVIES/`, … come
  from the user's install directory,
* every open is logged so a Windows run's file trace can be diffed against
  ours (that is the S2 gate).

## Threads

The game creates worker threads for streaming (and the render thread). On
wasm, threads are `SharedArrayBuffer` workers with one shared guest memory;
on a native host, pthreads. Guest `WaitForSingleObject`/`EnterCriticalSection`
map to the host primitives. Lifted code assumes the CPU struct is
thread-local (nfsu2-web's `cpu.h` design), so each guest thread gets its own.

## Progress metric

A frame counter in the null renderer plus an import-call histogram. The gate
is: boots for N frames without trapping, with an import trace and a file-open
trace that match a Windows run's shape.
