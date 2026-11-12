#!/usr/bin/env python3
"""Decide whether the lift's remaining traps are in code the game can run.

`scripts/audit_lift.py` counts the `unsupported_insn` stubs.  The count alone
does not say whether they matter: the lift also includes every in-.text
pointer, and some of those point at data, so a few garbage "functions" always
trap.

This script builds the call graph out of the generated C (direct `sub_x(c)`
calls) and asks, for each trapped instruction, whether its function is
reachable from the PE entry point.  A trap in reachable code is a bug to fix;
a trap in a function only reachable from a data seed is noise.

    scripts/classify_traps.py [gen dir] [exe]

Prints, per trapped instruction, the disassembly, the runtime reason and the
reachability, then a summary split into `REACHABLE` (must fix) and `dead`.
"""
import glob
import os
import re
import sys
import collections

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

FUNC = re.compile(r"^void sub_([0-9a-f]{8})\(CPU \*restrict c\)", re.M)
TRAP = re.compile(r'unsupported_insn\(c,\s*0x([0-9a-f]+)u,\s*"([^"]*)"\)')
CALL = re.compile(r"\bsub_([0-9a-f]{8})\(c\)")


def load_functions(gen_dir):
    """{addr: (source, [(start, end), ...])} and the call graph."""
    funcs = {}
    calls = collections.defaultdict(set)
    for path in sorted(glob.glob(os.path.join(gen_dir, "shard_*.c"))):
        src = open(path, encoding="utf-8", errors="replace").read()
        starts = [(m.start(), m.group(1)) for m in FUNC.finditer(src)]
        for i, (off, addr) in enumerate(starts):
            end = starts[i + 1][0] if i + 1 < len(starts) else len(src)
            body = src[off:end]
            funcs[int(addr, 16)] = body
            for m in CALL.finditer(body):
                calls[int(addr, 16)].add(int(m.group(1), 16))
    return funcs, calls


def reachable(calls, roots):
    seen = set()
    todo = [r for r in roots if r in calls or r in {}]
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        todo.extend(calls.get(f, ()))
    return seen


def main(gen_dir, exe):
    pe = pefile.PE(exe)
    ib = pe.OPTIONAL_HEADER.ImageBase
    ep = ib + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    # Find code section: prefer .text, fall back to entry point section
    code_sec = next(
        (s for s in pe.sections if s.Name.startswith(b".text")), None)
    if code_sec is None:
        ep = ib + pe.OPTIONAL_HEADER.AddressOfEntryPoint
        code_sec = next(
            (s for s in pe.sections
             if s.VirtualAddress <= ep - ib < s.VirtualAddress + s.Misc_VirtualSize),
            None)
    if code_sec is None:
        raise RuntimeError("cannot find code section in PE")
    text = code_sec
    tva, data = ib + text.VirtualAddress, text.get_data()
    md = Cs(CS_ARCH_X86, CS_MODE_32)

    funcs, calls = load_functions(gen_dir)
    live = reachable(calls, [ep])

    # resolve every trap to its function and instruction
    sites = []
    for addr, body in funcs.items():
        for m in TRAP.finditer(body):
            sites.append((addr, int(m.group(1), 16), m.group(2)))
    sites.sort()

    print(f"functions lifted        : {len(funcs)}")
    print(f"entry point             : {ep:#010x}"
          f"{'' if ep in funcs else '  (NOT lifted)'}")
    print(f"reachable from entry    : {len(live)}")
    print(f"trap sites              : {len(sites)}\n")

    dead = collections.Counter()
    live_sites = []
    for owner, addr, reason in sites:
        off = addr - tva
        txt = ""
        if 0 <= off < len(data):
            ins = next(md.disasm(data[off:off + 15], addr), None)
            txt = f"{ins.mnemonic} {ins.op_str}" if ins else "?"
        if owner in live:
            live_sites.append((owner, addr, reason, txt))
        else:
            dead[reason] += 1

    print("REACHABLE from the entry point  (fix these)")
    if not live_sites:
        print("  none")
    for owner, addr, reason, txt in live_sites:
        print(f"  sub_{owner:08x} @ {addr:#010x}  {reason:12} {txt}")

    print("\ndead  (data seeds / never called)")
    for reason, n in dead.most_common():
        print(f"  {reason:16} {n}")

    print("\nreachable per reason")
    for reason, n in collections.Counter(s[2] for s in live_sites).most_common():
        print(f"  {reason:16} {n}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "work/gen",
         sys.argv[2] if len(sys.argv) > 2 else "game/nfsps.exe")
