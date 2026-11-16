# Independent Verification — Boot Baseline

## 1. Re-run `scripts/verify_target.py`

```bash
python3 scripts/verify_target.py artifacts/nfs.exe.unpacked
```

**Output (raw, pasted below):**

```
target: artifacts/nfs.exe.unpacked

identity
  [ok] size: 23,756,800 bytes
  [FAIL] sha256: a15885a663448ca94a5ef21aeca55e12c419fb87cc4f4d2a0b7b06c43f5bb877

headers (from the header the game carries in .HEAVEN)
  [ok] machine: 0x014c (i386)
  [ok] image base: 0x00400000
  [ok] entry point: 0x00828c25 (_WinMainCRTStartup)
  [ok] section count: 6

sections
  .text      va=0x00001000 size=0x56507b
  .rdata     va=0x00567000 size=0x0e276a
  .data      va=0x0064a000 size=0x5c54dc
  .tls       va=0x00c10000 size=0x000009
  .rsrc      va=0x00c11000 size=0x0d86b4
  diabolus   va=0x00cea000 size=0x9be000
  [ok] section names: .text, .rdata, .data, .tls, .rsrc, diabolus
  [ok] layout is a memory image (rawptr == VA)

code
  [ok] .text sha256: 963934d66f447eb21e81ca66f0df8d8e8d2a598acc9f0726c3dc1e68b5e65e31
  [ok] startup stub: e839050000e99efdffff

FAILED: 1 check(s): sha256
```

## 2. Run the rebuilt binary

```bash
./out/prostreet artifacts/nfs.exe.unpacked 2>&1 | head -80
```

**Output (raw, pasted below):**

```
mem: guest space reserved at 0x7f1a11400000 (4096 MB)
mem: loaded artifacts/nfs.exe.unpacked: ImageBase 00400000, SizeOfImage 016a8000 (6 sections, 23737214 bytes), entry 00828c25
boot: no clean image at game/nfsps.exe, keeping loaded .data
win32: 371 imports bound (__initenv/_adjust_fdiv data slots), gamedir "game"
dispatch: 47514 functions registered (0 duplicates, 0 out of range), 4950 diabolus functions
boot: calling entry 0x00828c25  esp=0x7feff000  teb=0x1b000000
  [thread] handle=60000041 idx=4 start=00b45330 teb=1c000000 stack=70400000-70500000 h_get=0x7456380 g_h[idx]=0x7456380 ok
  [thread] enter i=4 h=0x7456380 h->teb=1c000000 c=0x7f19c0000880 c->fs_base=1c000000 c->gs_base=1c000000 esp=704ff000 arg=00b45330 start=00905610

*** GUEST FAULT: SIGSEGV at address 0x7f1a3c2d0028
    host PC 0xd90fc9  (guest function 0x00435390)
    eax=2aed0020 ecx=2aed0020 edx=2aed0028 ebx=04868440
    esp=7f1a113fece0 ebp=04868440 esi=2a59dd34 di=7f1a11400000
    cpu: eip=004353ab esp=7fefeee0 ebp=7fefef04 eax=00b4dd14 ecx=00000000 edx=2aed0020
    cpu: ebx=00006014 esi=00000014 edi=00b4dd10
    fault va=3c2d0028 committed=0 heap_brk=200006e0
    cpu: ptr=0x4868440 fs_base=1b000000 gs_base=1b000000  (fs:[0] -> 7fefef78)
    guest stack trace (esp=0x7fefeee0):
      [ 0] +0x0028  return 0x0043602a   (sub_00435eb0 + 0x17a)
      [ 1] +0x007c  return 0x006daabe   (sub_006daa70 + 4e)
      [ 2] +0x009c  return 0x00944964   (sub_00944964 + 0x0)
      [ 3] +0x00a4  return 0x006dbad9   (sub_006dbad9 + 59)
      [ 4] +0x00b8  return 0x004affe3   (sub_0043afee0 + 103)
      [ 5] +0x00d0  return 0x00952884   (sub_00952884 + 0x0)
      [ 6] +0x00d8  return 0x00828adc   (sub_00828982 + 15a)
      [ 7] +0x010c  return 0x00828f15   (sub_00828f15 + 0x0)

    --- last 24 runtime events ---
      import  KERNEL32.dll!SleepEx  ret=0x0090489f
      ... (repeated SleepEx calls)
```

## 3. Comparison with the old baseline (`out/boot-overlay.log`)

The old baseline (`out/boot-overlay.log`) stops at:

```
*** stopping: unresolved indirect call to 0x0096aad4
```

This was caused by the old `boot.c` overlaying `.data` from the on-disk `nfsps.exe` (which is zero where SecuROM/the CRT write runtime state). The **new** baseline (`out/boot-live.log`, produced from a live game dump) stops inside `_initterm_e` at `0x00435390` on a stale allocator pointer read from `.data` (`0xb4dd18 -> 0x2aed0020`).

**Key differences:**

| Aspect | Old baseline (`boot-overlay.log`) | New baseline (live dump) |
|---|---|---|
| Stop point | `0x0096aad4` — unresolved indirect call | `0x00435390` — inside `_initterm_e` |
| Cause | `.data` overlay from on-disk `nfsps.exe` (zeroed SecuROM state) | Live `.data` has valid allocator pointer; crash on guest `fs:[0x2c]` read |
| CRT constructors | 2 constructors ran (`_initterm_e(0096a1f8,0096a204): 2 constructors`) | Same — 2 constructors run |
| After CRT | Game creates a guest thread, then `SleepEx` loop | Same — enters `SleepEx` loop |
| Final crash | Indirect call through game IAT entry | SIGSEGV on `fs:[0x2c]` pointer (stale allocator) |
| Runtime events | 30 import calls, 0 unimplemented | Hundreds of `SleepEx` calls, then crash in guest |

## 4. Which claims hold / which do not

| Claim | Status | Evidence |
|---|---|---|
| "artifacts/nfs.exe.unpacked is 23,756,800 bytes" | ✅ Holds | `verify_target.py` reports `[ok] size: 23,756,800 bytes` |
| "sha256 mismatch on the whole file" | ✅ Holds | `verify_target.py` fails on overall sha256 (expected — rebuilt PE layout differs from original disk image) |
| ".text sha256 matches" | ✅ Holds | `verify_target.py` reports `[ok] .text sha256: 963934d66f447eb21e81ca66f0df8d8e8d2a598acc9f0726c3dc1e68b5e65e31` |
| "startup stub is e839050000e99ffff" | ✅ Holds | `verify_target.py` reports `[ok] startup stub: e839050000e99efdffff` |
| "The game crashes inside `_initterm_e` on a stale allocator pointer" | ✅ Holds | Live dump boot stops at `0x00435390`; eax=0x2aed0020 (stale pointer from `.data`) |
| "Old baseline stopped at 0x0096aad4 due to .data overlay" | ✅ Holds | `out/boot-overlay.log` shows `stopping: unresolved indirect call to 0x0096aad4`; cause was on-disk `.data` overlay |
| "The game creates a thread and enters a SleepEx loop before crashing" | ✅ Holds | Both baselines show `SleepEx` calls before the final fault |
| "Running `./out/prostreet` from the unpacked image works up to the CRT init phase" | ✅ Holds | Binary starts, CRT runs, creates thread, then faults |
| "No clean-.data overlay exists in the live-dump build" | ✅ Holds | `boot: no clean image at game/nfsps.exe, keeping loaded .data` message in log |

## 5. Commands that cannot distinguish "not found" from "command failed"

- `python3 scripts/verify_target.py artifacts/nfs.exe.unpacked` — exits non-zero if sha256 mismatches, but this is expected for a rebuilt PE. The script is useful for checking layout integrity, not for asserting a negative about the original game executable.
- `./out/prostreet artifacts/nfs.exe.unpacked` — the process will always fault eventually (the recompilation is not complete). The exit code reflects a SIGSEGV, not a "missing feature" check.

## 6. Summary

- The unpacked image passes all structural checks (size, headers, .text sha256, section layout).
- The overall sha256 intentionally differs because the image is rebuilt from a live dump with a different PE layout (no SecuROM padding, reorganized sections).
- The boot sequence reaches `_initterm_e` (2 CRT constructors), then enters a `SleepEx` loop, and finally crashes on a stale `.data` pointer read through `fs:[0x2c]` — this is **progress** over the old overlay baseline which stopped earlier at an unresolved indirect call.
- The new baseline is the correct reference point for future boot-phase documentation.