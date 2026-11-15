#!/usr/bin/env python3
"""Infer each import's stdcall argument count, then emit lifter/runtime/impargs.c.

A stdcall callee pops its own arguments, so the emulator has to know how many
bytes each HLE handler removes from esp.  Two independent sources decide it:

  1. The code itself.  At every `call [iat]` (or `call <jmp [iat] thunk>`) the
     walker records how many bytes of arguments were pushed and whether the
     caller adds them back afterwards.  `add esp, N` after the call means the
     callee pops nothing (__cdecl, N bytes of caller cleanup); no cleanup means
     the callee pops them (__stdcall / __thiscall), so nargs = bytes/4.

  2. A table of authoritative counts (KNOWN) from Microsoft's headers and the
     nfsu2-web reference, which cross-checks (1) and supplies never-called
     imports.

The import map is out/imports-table.json: the intact descriptor table in
game/nfsps.exe, whose .text is byte-identical to the image the game decrypts
in memory.  (The packed exe's own IAT is a SecuROM decoy and is not used.)

Usage: scripts/argcount.py [--quiet]
"""
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "artifacts/nfs.exe.unpacked"
IMPORTS = ROOT / "out/imports-table.json"
SEEDS = ROOT / "lifter/seeds-reachable.txt"
OUT = ROOT / "lifter/runtime/impargs.c"

IMAGE_BASE = 0x400000
TEXT_VA, TEXT_SIZE = 0x401000, 0x56507B

# Authoritative counts.  Win32 entries are the reference port's, tested against
# the real DLLs; the CRT entries follow the x86 conventions:
#   MSVCR80  every export is __cdecl -> the caller pops, nargs 0.
#   MSVCP80  C++ member functions are __thiscall -> nargs = stack args.
KNOWN = {
    # ---- kernel32 ----
    "MultiByteToWideChar": 6, "SuspendThread": 1, "ResumeThread": 1,
    "GetVersionExA": 1, "GetLongPathNameA": 3, "GetDriveTypeA": 1,
    "GetLogicalDrives": 0, "FindClose": 1, "FindNextFileA": 2,
    "FindFirstFileA": 2, "FreeLibrary": 1, "CreateDirectoryA": 2,
    "GetModuleHandleA": 1, "GetSystemInfo": 1, "Sleep": 1,
    "Process32Next": 2, "Process32First": 2, "CreateToolhelp32Snapshot": 2,
    "SetErrorMode": 1, "GetTickCount": 0, "SetThreadPriority": 2,
    "GetSystemTimeAsFileTime": 1, "GetCurrentProcessId": 0,
    "GetCurrentThreadId": 0, "SetUnhandledExceptionFilter": 1,
    "UnhandledExceptionFilter": 1, "GetCurrentProcess": 0,
    "TerminateProcess": 2, "CloseHandle": 1, "InterlockedExchange": 2,
    "ReleaseMutex": 1, "CreateMutexW": 3, "GetSystemDirectoryA": 2,
    "LoadLibraryA": 1, "LeaveCriticalSection": 1,
    "TryEnterCriticalSection": 1, "EnterCriticalSection": 1,
    "DeleteCriticalSection": 1, "InitializeCriticalSection": 1,
    "WaitForSingleObject": 2, "CreateEventA": 4, "GetDiskFreeSpaceExA": 4,
    "SetWaitableTimer": 5, "SystemTimeToFileTime": 2, "GetSystemTime": 1,
    "CreateWaitableTimerA": 3, "DuplicateHandle": 7, "GetCurrentThread": 0,
    "TerminateThread": 2, "SleepEx": 2, "GetThreadPriority": 1,
    "GetExitCodeThread": 2, "WriteFile": 5, "SetFilePointer": 4,
    "DeleteFileA": 1, "MoveFileA": 2, "ReadFile": 5, "GetFileSize": 2,
    "GetProcAddress": 2, "OutputDebugStringA": 1,
    "QueryPerformanceFrequency": 1, "SetEvent": 1,
    "InterlockedCompareExchange": 3, "PulseEvent": 1, "GetPriorityClass": 1,
    "SetPriorityClass": 2, "RemoveDirectoryW": 1, "CreateDirectoryW": 2,
    "FindNextFileW": 2, "FindFirstFileW": 2, "WideCharToMultiByte": 8,
    "MoveFileW": 2, "DeleteFileW": 1, "SetEndOfFile": 1, "CreateFileW": 7,
    "IsDebuggerPresent": 0, "GetLastError": 0, "IsBadReadPtr": 2,
    "QueryPerformanceCounter": 1, "CreateThread": 6, "GetFileSizeEx": 2,
    "SetFilePointerEx": 4, "RaiseException": 4, "ReleaseSemaphore": 3,
    "InterlockedDecrement": 1, "CreateSemaphoreA": 4,
    "InterlockedExchangeAdd": 2, "SetThreadIdealProcessor": 2,
    "TlsGetValue": 1, "TlsSetValue": 2, "TlsAlloc": 0, "CreateMutexA": 3,
    "VirtualProtect": 4, "GetCurrentDirectoryA": 2, "ResetEvent": 1,
    "CreateFileA": 7,
    # ---- user32 ----
    "DispatchMessageA": 1, "TranslateMessage": 1, "GetMessageA": 4,
    "ShowCursor": 1, "PeekMessageA": 5, "GetClientRect": 2,
    "SetWindowLongA": 3, "DestroyWindow": 1, "SendInput": 3, "SetCapture": 1,
    "SystemParametersInfoA": 4, "EnumDisplaySettingsA": 3, "EndPaint": 2,
    "BeginPaint": 2, "PostQuitMessage": 1, "MapVirtualKeyA": 2, "ToUnicode": 6,
    "GetForegroundWindow": 0, "GetDesktopWindow": 0,
    "GetKeyboardLayout": 1, "MapVirtualKeyExA": 3, "GetKeyState": 1,
    "IsIconic": 1, "MessageBoxA": 4, "ReleaseCapture": 0, "wvsprintfA": 3,
    "LoadIconA": 2, "LoadCursorA": 2, "RegisterClassExA": 1,
    "AdjustWindowRect": 3, "CreateWindowExA": 12, "UpdateWindow": 1,
    "SetCursor": 1, "SetFocus": 1, "SetForegroundWindow": 1,
    "GetWindowRect": 2, "ShowWindow": 2, "GetWindowLongA": 2,
    "DefWindowProcA": 4, "PostMessageA": 4,
    # ---- advapi32 ----
    "CheckTokenMembership": 3, "FreeSid": 1, "RegCreateKeyA": 3,
    "RegOpenKeyExA": 5, "RegCloseKey": 1, "RegSetValueExA": 6,
    "RegQueryValueExA": 6, "CryptReleaseContext": 2, "CryptGenRandom": 4,
    "CryptAcquireContextA": 5, "AllocateAndInitializeSid": 11,
    # ---- ws2_32 (ordinals are fixed by the Winsock 2 spec) ----
    "WSACreateEvent": 0, "WSACloseEvent": 1, "WSASetEvent": 1, "WSARecv": 7,
    "WSARecvFrom": 9, "WSAResetEvent": 1, "WSAWaitForMultipleEvents": 4,
    "WSAGetOverlappedResult": 4, "WSAIoctl": 9,
    "#1": 3, "#2": 3, "#3": 1, "#4": 3, "#5": 3, "#6": 3, "#7": 5,
    "#8": 1, "#9": 1, "#10": 3, "#11": 1, "#12": 1, "#13": 2, "#14": 1,
    "#15": 1, "#16": 4, "#17": 6, "#18": 5, "#19": 4, "#20": 6, "#21": 5,
    "#22": 2, "#23": 3, "#51": 3, "#52": 1, "#53": 1, "#54": 1, "#55": 2,
    "#56": 2, "#57": 2, "#111": 0, "#112": 1, "#115": 2, "#116": 0,
    # ---- d3dx9 ----
    "D3DXCreateEffectFromResourceA": 8, "D3DXMatrixMultiplyTranspose": 3,
    "D3DXCreateEffectPool": 1, "D3DXSaveSurfaceToFileA": 5,
    "D3DXGetVertexShaderProfile": 1, "D3DXGetPixelShaderProfile": 1,
    "D3DXVec3Transform": 3, "D3DXVec4Transform": 3, "D3DXMatrixMultiply": 3,
    "D3DXSaveSurfaceToFileInMemory": 5,
    "D3DXCreateCubeTextureFromFileInMemory": 4,
    "D3DXCreateVolumeTextureFromFileInMemory": 4,
    "D3DXMatrixTranspose": 2, "D3DXMatrixInverse": 3,
    # ---- everything else ----
    "timeEndPeriod": 1, "timeGetTime": 0, "timeBeginPeriod": 1,
    "UuidToStringA": 2, "RpcStringFreeA": 1, "Direct3DCreate9": 1,
    "DirectInput8Create": 5, "SHGetFolderPathA": 5, "CoCreateGuid": 1,
    "Netbios": 1, "#1@dsound": 3,
    # ---- msvcp80: __thiscall, stack args only ----
    "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z": 1,
    "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBD@Z": 1,
    "??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ": 0,
}

# MSVCR80 is entirely __cdecl: the caller pops every argument, so the HLE must
# pop none.  (The lifted code contains the caller's own `add esp, N`.)
CDECL_DLLS = {"MSVCR80.dll"}

CC = {"jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja", "js", "jns",
      "jp", "jnp", "jl", "jge", "jle", "jg", "jcxz", "jecxz"}
# Instructions that do not touch esp and are not arguments.
SKIP = {"mov", "lea", "xor", "test", "cmp", "movzx", "movsx", "inc", "dec",
        "and", "or", "add", "adc", "sub", "sbb", "not", "neg", "shl", "shr",
        "sar", "imul", "mul", "div", "idiv", "cdq", "cwde", "nop", "xchg",
        "sete", "setne", "fld", "fstp", "fild", "fistp", "pushf", "popf",
        "sahf", "lahf", "movss", "movsd", "xorps", "movaps", "movups"}


def key(r):
    return "#1@dsound" if (r["dll"] == "DSOUND.dll" and r["name"] == "#1") else r["name"]


def main():
    rows = json.loads(IMPORTS.read_text())
    iat = {r["va"]: r for r in rows}
    data = EXE.read_bytes()

    def rd32(va):
        return int.from_bytes(data[va - IMAGE_BASE:va - IMAGE_BASE + 4], "little")

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    # `jmp dword ptr [iat]` thunks: address -> slot
    thunk = {}
    pos = TEXT_VA
    while pos < TEXT_VA + TEXT_SIZE - 6:
        if data[pos - IMAGE_BASE] == 0xFF and data[pos - IMAGE_BASE + 1] == 0x25:
            a = rd32(pos + 2)
            if a in iat:
                thunk[pos] = a
                pos += 6
                continue
        pos += 1

    seeds = [int(x, 16) for x in SEEDS.read_text().split()]

    def next_insns(va, n):
        off = va - IMAGE_BASE
        out = []
        for j in md.disasm(data[off:off + 24], va):
            out.append(j)
            if len(out) >= n:
                break
        return out

    # per import: Counter of (bytes_pushed, bytes_cleaned)
    ev = defaultdict(Counter)

    def count_args(recent):
        """bytes of arguments pushed since the last barrier, or None."""
        n = 0
        for prev in reversed(recent):
            m, s = prev.mnemonic, prev.op_str.replace(" ", "")
            if m == "mov" and s == "ebp,esp":
                return None
            if m == "push":
                n += 4
            elif m == "sub" and s.startswith("esp,"):
                # `sub esp, 8` just before the call is space for a double
                # argument; right after the prologue it is locals.
                try:
                    v = int(s.split(",")[1], 0)
                except ValueError:
                    return None
                if v > 64:
                    return None
                n += v
            elif m in SKIP:
                continue
            else:
                return None
        return n

    def cleanup_after(va):
        """bytes the caller adds back immediately after the call."""
        ins = next_insns(va, 4)
        for k, j in enumerate(ins):
            if j.mnemonic == "add" and j.op_str.replace(" ", "").startswith("esp,"):
                try:
                    return int(j.op_str.split(",")[1], 0)
                except ValueError:
                    return 0
            if j.mnemonic in ("pop", "leave", "ret", "jmp", "call"):
                return 0
            if k >= 1:
                return 0
        return 0

    cov = bytearray(TEXT_SIZE)
    seen = set(seeds)
    stack = list(seeds)
    while stack:
        va = stack.pop()
        recent = []
        while TEXT_VA <= va < TEXT_VA + TEXT_SIZE:
            off = va - TEXT_VA
            if cov[off]:
                break
            try:
                i = next(iter(md.disasm(data[off:off + 16], va)))
            except StopIteration:
                break
            for k in range(i.size):
                if off + k < TEXT_SIZE:
                    cov[off + k] = 1
            m, ops = i.mnemonic, i.operands
            o = ops[0] if ops else None

            slot = None
            if m == "call":
                if o is not None and o.type == X86_OP_MEM and o.mem.base == 0 and o.mem.index == 0:
                    if o.mem.disp in iat:
                        slot = o.mem.disp
                elif o is not None and o.type == X86_OP_IMM and o.imm in thunk:
                    slot = thunk[o.imm]
            if slot is not None:
                pushed = count_args(recent)
                if pushed is not None:
                    ev[slot][(pushed, cleanup_after(i.address + i.size))] += 1

            recent.append(i)
            if len(recent) > 24:
                recent.pop(0)

            if m == "call":
                if o is not None and o.type == X86_OP_IMM and TEXT_VA <= o.imm < TEXT_VA + TEXT_SIZE:
                    if o.imm not in seen:
                        seen.add(o.imm)
                        stack.append(o.imm)
                va = i.address + i.size
                recent = []
                continue
            if m == "jmp":
                if o is not None and o.type == X86_OP_IMM:
                    va = o.imm
                    recent = []
                    continue
                break
            if m in CC:
                if o is not None and o.type == X86_OP_IMM and TEXT_VA <= o.imm < TEXT_VA + TEXT_SIZE:
                    if o.imm not in seen:
                        seen.add(o.imm)
                        stack.append(o.imm)
                va = i.address + i.size
                continue
            if m in ("ret", "retf", "int3", "hlt", "ud2"):
                break
            va = i.address + i.size

    agree = disagree = inferred = 0
    final = []
    for r in rows:
        cnt = ev.get(r["va"], Counter())
        k = key(r)
        cdecl = r["dll"] in CDECL_DLLS
        # Decide from the evidence: no caller cleanup -> callee pops.
        stdcall_sites = sum(c for (p, cl), c in cnt.items() if cl == 0 and p > 0)
        cdecl_sites = sum(c for (p, cl), c in cnt.items() if cl > 0 and cl == p)
        by_evidence = (stdcall_sites > cdecl_sites)
        pushed = Counter()
        for (p, cl), c in cnt.items():
            if (cl == 0) == by_evidence:
                pushed[p] += c
        guess = 0
        if pushed and not cdecl:
            guess = max(pushed, key=lambda p: pushed[p]) // 4

        if cdecl:
            n = 0
            if k in KNOWN and KNOWN[k] != 0:
                print(f"  NOTE {r['dll']}!{k}: KNOWN says {KNOWN[k]}, cdecl rule wins (0)")
        elif k in KNOWN:
            n = KNOWN[k]
            if cnt and n != guess:
                disagree += 1
                print(f"  DISAGREE {r['dll']}!{k}: evidence {guess} {dict(cnt)}, KNOWN {n}")
            else:
                agree += 1
        else:
            n = guess
            inferred += 1
            if cnt and len(pushed) > 1:
                print(f"  AMBIGUOUS {r['dll']}!{k}: {dict(cnt)} -> {n}")
        final.append((r["va"], r["dll"], r["name"], n))

    print(f"\narg counts: {agree} known+agree, {disagree} known+disagree, "
          f"{inferred} from evidence only, "
          f"{sum(1 for r in rows if not ev.get(r['va']))} never called (0)")

    OUT.parent.mkdir(exist_ok=True)
    with OUT.open("w") as f:
        f.write("/* Generated by scripts/argcount.py -- do not edit.\n"
                " * Import table: out/imports-table.json (nfsps.exe descriptors).\n"
                " * nargs is the stdcall argument count in dwords: win32_call pops\n"
                " * 4*nargs after the handler.  __cdecl imports are 0. */\n"
                "#include <stdint.h>\n"
                "typedef struct { uint32_t iat; const char*dll; const char*name; int nargs; } ImpEnt;\n")
        f.write(f"const uint32_t g_imp_count = {len(final)};\n")
        f.write("const ImpEnt g_imp[] = {\n")
        for va, dll, name, n in sorted(final):
            f.write(f'  {{0x{va:08x}u,"{dll}","{name}",{n}}},\n')
        f.write("};\n")
    print(f"-> {OUT.relative_to(ROOT)} ({len(final)} entries)")


if __name__ == "__main__":
    main()
