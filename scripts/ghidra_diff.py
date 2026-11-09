#!/usr/bin/env python3
"""Diff the recompiler's function set against Ghidra's analysis.

The lift recovers entries from data pointers + the direct-call graph, because
the image has no base relocations.  Ghidra analyses the same bytes
independently: it follows calls, exception data, switches and its own library
signatures.  Neither set is gospel — the point is to explain every difference.

    scripts/ghidra_diff.py <ghidra-functions.tsv> [gen dir] [exe]

Prints four buckets:

  both            entry in both sets
  lift only       likely data seeds; checked against the call graph and the
                  instruction-boundary test so each one gets a reason
  ghidra only     functions the lift missed -> real gaps, unless they are
                  thunks, library functions or inside a lifted function
  lifted span     ghidra-only entries that fall inside another function's
                  lifted code (usually tail-call targets Ghidra split)

Writes nothing; use the output to decide whether the lift needs another seed
class or Ghidra's extra entries are noise.
"""
import glob
import os
import re
import sys
import collections

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

FUNC = re.compile(r"^void sub_([0-9a-f]{8})\(CPU \*restrict c\)", re.M)
CALL = re.compile(r"\bsub_([0-9a-f]{8})\(c\)")
ADDR = re.compile(r"0x([0-9a-f]{8})u")
MAXLEN = 15


def load_ghidra(path):
    funcs = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 2:
            continue
        try:
            funcs[int(parts[0], 16)] = (parts[1], parts[2] if len(parts) > 2 else "")
        except ValueError:
            pass
    return funcs


def load_lift(gen_dir):
    funcs = set()
    fntab = os.path.join(gen_dir, "fntab.c")
    src = open(fntab, encoding="utf-8", errors="replace").read()
    for m in ADDR.finditer(src.split("g_fn_ptr")[0]):
        funcs.add(int(m.group(1), 16))
    calls = collections.defaultdict(set)
    for path in glob.glob(os.path.join(gen_dir, "shard_*.c")):
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in FUNC.finditer(text):
            start = m.start()
            end = text.find("\nvoid sub_", start + 1)
            body = text[start:end if end > 0 else len(text)]
            owner = int(m.group(1), 16)
            for c in CALL.finditer(body):
                calls[owner].add(int(c.group(1), 16))
    return funcs, calls


def main(ghidra_path, gen_dir, exe):
    pe = pefile.PE(exe)
    ib = pe.OPTIONAL_HEADER.ImageBase
    sec = next(s for s in pe.sections if s.Name.startswith(b".text"))
    tva, data = ib + sec.VirtualAddress, sec.get_data()
    md = Cs(CS_ARCH_X86, CS_MODE_32)

    ghidra = load_ghidra(ghidra_path)
    lift, calls = load_lift(gen_dir)
    called = set()
    for v in calls.values():
        called |= v

    def straddled(addr):
        off = addr - tva
        for back in range(1, MAXLEN):
            if off - back < 0:
                break
            try:
                ins = next(md.disasm(data[off - back:off - back + MAXLEN], addr - back))
            except StopIteration:
                continue
            if ins.address < addr < ins.address + ins.size:
                return True
        return False

    both = lift & set(ghidra)
    lift_only = sorted(lift - set(ghidra))
    ghidra_only = sorted(set(ghidra) - lift)

    print(f"lift entries          : {len(lift)}")
    print(f"ghidra functions      : {len(ghidra)}")
    print(f"both                  : {len(both)}")
    print(f"lift only             : {len(lift_only)}")
    print(f"ghidra only           : {len(ghidra_only)}\n")

    classes = collections.Counter()
    samples = collections.defaultdict(list)
    for a in lift_only:
        if a in called:
            k = "call target (why is Ghidra missing it?)"
        elif straddled(a):
            k = "straddles an instruction (data)"
        else:
            k = "clean boundary, no caller (data or tail-call target)"
        classes[k] += 1
        if len(samples[k]) < 8:
            samples[k].append(a)
    print("lift only, by reason")
    for k, n in classes.most_common():
        print(f"  {n:6}  {k}")
        print("          " + " ".join(f"{a:08x}" for a in samples[k]))

    gclasses = collections.Counter()
    gsamples = collections.defaultdict(list)
    for a in ghidra_only:
        name, kind = ghidra[a]
        if kind == "external":
            k = "external (import)"
        elif kind == "thunk":
            k = "thunk"
        elif a in called:
            k = "CALLED by lifted code (real gap!)"
        elif straddled(a):
            k = "straddles an instruction"
        else:
            k = "not called, clean boundary"
        gclasses[k] += 1
        if len(gsamples[k]) < 10:
            gsamples[k].append((a, name))
    print("\nghidra only, by reason")
    for k, n in gclasses.most_common():
        print(f"  {n:6}  {k}")
        for a, name in gsamples[k]:
            print(f"          {a:08x}  {name}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/opencode/ghidra_funcs.txt",
         sys.argv[2] if len(sys.argv) > 2 else "work/gen",
         sys.argv[3] if len(sys.argv) > 3 else "game/nfsps.exe")
